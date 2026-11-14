// In-memory Gpio / Uart for running Esp32HardwareAdapter on the laptop
// (`airo-host --adapter esp32`) and in the unit tests. Nothing here touches
// real hardware; it records what the chip would have been asked to do.
#pragma once

#include <map>
#include <vector>

#include "airo/board/io.h"

namespace airo::host {

struct HostGpio : board::Gpio {
  enum class PinMode { Unconfigured, HiZ, Input, Output };
  struct Pin {
    PinMode mode = PinMode::Unconfigured;
    bool level = false;
    bool pull_up = false;
  };
  std::map<int, Pin> pins;
  std::map<int, bool> inputs;  // what a test wires onto an input pin
  int drives = 0;

  void drive(int g, bool level) override {
    pins[g].mode = PinMode::Output;
    pins[g].level = level;
    ++drives;
  }
  void release(int g) override { pins[g] = Pin{PinMode::HiZ, false, false}; }
  void input(int g, bool pull_up) override { pins[g] = Pin{PinMode::Input, false, pull_up}; }
  bool read(int g) override {
    auto it = inputs.find(g);
    if (it != inputs.end()) return it->second;
    return pins[g].pull_up;  // floating with pull-up reads high
  }
  bool driven(int g) const {
    auto it = pins.find(g);
    return it != pins.end() && it->second.mode == PinMode::Output;
  }
};

struct HostUart : board::Uart {
  int rx = -1;
  int tx = -1;
  std::vector<uint8_t> written;
  std::vector<uint8_t> incoming;
  bool begin(int rx_gpio, uint32_t) override {
    rx = rx_gpio;
    return true;
  }
  void attachTx(int g) override { tx = g; }
  void write(const std::vector<uint8_t>& b) override { written.insert(written.end(), b.begin(), b.end()); }
  std::vector<uint8_t> read() override {
    std::vector<uint8_t> out;
    out.swap(incoming);
    return out;
  }
};

}  // namespace airo::host
