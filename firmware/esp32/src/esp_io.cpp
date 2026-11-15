#include "esp_io.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

namespace airo::esp {

uint64_t EspClock::nowMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

void EspGpio::drive(int g, bool level) { driveMany({{g, level}}); }

void EspGpio::driveMany(const std::vector<std::pair<int, bool>>& levels) {
  uint32_t set_lo = 0, clr_lo = 0, set_hi = 0, clr_hi = 0;
  for (const auto& [g, level] : levels) {
    if (g < 32) (level ? set_lo : clr_lo) |= (1u << g);
    else (level ? set_hi : clr_hi) |= (1u << (g - 32));
  }
  // Output latches first (all at once), then enable any pin that was not yet
  // an output, so no pin ever shows a stale level.
  REG_WRITE(GPIO_OUT_W1TC_REG, clr_lo);
  REG_WRITE(GPIO_OUT_W1TS_REG, set_lo);
  REG_WRITE(GPIO_OUT1_W1TC_REG, clr_hi);
  REG_WRITE(GPIO_OUT1_W1TS_REG, set_hi);
  for (const auto& [g, level] : levels) {
    (void)level;
    if (outputs_.insert(g).second) {
      const gpio_num_t pin = static_cast<gpio_num_t>(g);
      gpio_set_pull_mode(pin, GPIO_FLOATING);
      gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
    }
  }
}

void EspGpio::release(int g) {
  const gpio_num_t pin = static_cast<gpio_num_t>(g);
  outputs_.erase(g);
  gpio_reset_pin(pin);  // GPIO function, output disabled
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(pin, GPIO_FLOATING);  // reset enables a pull-up: remove it
}

void EspGpio::input(int g, bool pull_up) {
  const gpio_num_t pin = static_cast<gpio_num_t>(g);
  outputs_.erase(g);
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(pin, pull_up ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
}

bool EspGpio::read(int g) { return gpio_get_level(static_cast<gpio_num_t>(g)) != 0; }

bool EspUart::begin(int rx_gpio, uint32_t baud) {
  if (!installed_) {
    if (uart_driver_install(port_, 1024, 0, 0, nullptr, 0) != ESP_OK) return false;
    installed_ = true;
  }
  uart_config_t cfg = {};
  cfg.baud_rate = static_cast<int>(baud);
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  uart_param_config(port_, &cfg);
  rx_ = rx_gpio;
  // RX only: the TX pin is routed by attachTx() when drive is allowed.
  uart_set_pin(port_, UART_PIN_NO_CHANGE, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_flush_input(port_);
  return true;
}

void EspUart::attachTx(int g) {
  if (!installed_) return;
  if (g >= 0) {
    uart_set_pin(port_, g, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  } else if (tx_ >= 0) {
    const gpio_num_t pin = static_cast<gpio_num_t>(tx_);
    gpio_reset_pin(pin);  // back to plain GPIO: the UART no longer drives it
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_FLOATING);
  }
  tx_ = g;
}

void EspUart::write(const std::vector<uint8_t>& bytes) {
  if (!installed_ || tx_ < 0 || bytes.empty()) return;
  uart_write_bytes(port_, bytes.data(), bytes.size());
}

std::vector<uint8_t> EspUart::read() {
  std::vector<uint8_t> out;
  if (!installed_) return out;
  size_t n = 0;
  uart_get_buffered_data_len(port_, &n);
  if (n == 0) return out;
  out.resize(n);
  const int got = uart_read_bytes(port_, out.data(), n, 0);
  out.resize(got > 0 ? static_cast<size_t>(got) : 0);
  return out;
}

}  // namespace airo::esp
