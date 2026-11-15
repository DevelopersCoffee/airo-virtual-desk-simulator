// BenchLink: the hardware-in-the-loop channel (WebSocket /bench on the board,
// or the same path on `airo-host --adapter esp32`).
//
// It carries pin-level signals between the board's Esp32HardwareAdapter and
// the laptop's desk model (src/packages/airo-link/pin-bridge.ts), so the
// e2e scenario table runs against the real adapter code with nothing wired
// to the desk. It is refused unless BenchIo is in bench mode.
//
// Laptop -> board:
//   {"t":"hello","case":"A1|A2|B","manual_only":bool,"powered":bool}
//   {"t":"in","s":{"sense_up":true,...}}         input signal levels (asserted)
//   {"t":"rx","line":"htx|dtx","bytes":[..]}     UART bytes into the board
//   {"t":"power","on":bool}                      bench "cut Airo power"
//   {"t":"hang","ms":n}                          freeze the core loop (<= 3000 ms)
//   {"t":"halt"}                                 stop the core until reboot
//   {"t":"reboot"}
// Board -> laptop:
//   {"t":"bench", ...}                           state, on open and on change
//   {"t":"out","s":{"heartbeat":true,...}}       output changes, one write per message
//   {"t":"tx","line":"htx","bytes":[..]}         UART bytes the core sent
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "airo/board/bench_io.h"
#include "airo/board/esp32_hardware_adapter.h"
#include "airo/json.h"

namespace airo::board {

class BenchLink {
 public:
  using Send = std::function<void(const std::string&)>;

  enum class Result : uint8_t { None, Hello, PowerChanged, Hang, Halt, Reboot, Invalid };

  BenchLink(BenchIo& io, Esp32HardwareAdapter& hw);

  /// false (with a reason) unless the board is in bench mode, or a link is already open.
  bool open(Send sender, std::string* why = nullptr);
  void close();
  bool isOpen() const { return static_cast<bool>(send_); }
  bool helloReceived() const { return hello_; }

  Result onMessage(const Json& msg);
  /// For Result::Hang: how long to freeze.
  uint32_t hangMs() const { return hang_ms_; }

  Json state() const;
  void sendState();

 private:
  void send(const Json& j);

  BenchIo& io_;
  Esp32HardwareAdapter& hw_;
  Send send_;
  bool hello_ = false;
  uint32_t hang_ms_ = 0;
};

}  // namespace airo::board
