// ESP-IDF implementations of the board's Gpio / Uart / Clock interfaces.
#pragma once

#include <set>

#include "airo/board/io.h"
#include "airo/hardware_adapter.h"
#include "driver/uart.h"

namespace airo::esp {

struct EspClock : Clock {
  uint64_t nowMs() override;
};

class EspGpio : public board::Gpio {
 public:
  void drive(int gpio, bool level) override;
  void release(int gpio) override;
  void input(int gpio, bool pull_up) override;
  bool read(int gpio) override;
  /// One register write per bank, so encoded lines change together.
  void driveMany(const std::vector<std::pair<int, bool>>& levels) override;

 private:
  std::set<int> outputs_;
};

class EspUart : public board::Uart {
 public:
  explicit EspUart(uart_port_t port) : port_(port) {}
  bool begin(int rx_gpio, uint32_t baud) override;
  void attachTx(int gpio) override;
  void write(const std::vector<uint8_t>& bytes) override;
  std::vector<uint8_t> read() override;

 private:
  uart_port_t port_;
  bool installed_ = false;
  int rx_ = -1;
  int tx_ = -1;
};

}  // namespace airo::esp
