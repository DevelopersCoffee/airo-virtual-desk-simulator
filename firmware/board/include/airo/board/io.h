// The only two things the board code needs from the chip. EspGpio / EspUart
// (firmware/esp32) implement them with ESP-IDF drivers; the laptop build and
// the unit tests use in-memory fakes.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace airo::board {

struct Gpio {
  virtual ~Gpio() = default;
  /// Push-pull output at `level`.
  virtual void drive(int gpio, bool level) = 0;
  /// High-impedance input with no pull: the pin drives nothing.
  virtual void release(int gpio) = 0;
  /// Input, optionally with the internal pull-up.
  virtual void input(int gpio, bool pull_up) = 0;
  virtual bool read(int gpio) = 0;
  /// Set several outputs at once. The ESP32 driver does it with one register
  /// write so an encoded code never passes through another code on the way.
  virtual void driveMany(const std::vector<std::pair<int, bool>>& levels) {
    for (const auto& [g, level] : levels) drive(g, level);
  }
};

struct Uart {
  virtual ~Uart() = default;
  /// Open with RX on `rx_gpio` and no TX pin attached (-1 = none).
  virtual bool begin(int rx_gpio, uint32_t baud) = 0;
  /// Attach (gpio >= 0) or detach (-1) the TX pin. Detached = Hi-Z.
  virtual void attachTx(int gpio) = 0;
  virtual void write(const std::vector<uint8_t>& bytes) = 0;
  /// Bytes received since the last call.
  virtual std::vector<uint8_t> read() = 0;
};

}  // namespace airo::board
