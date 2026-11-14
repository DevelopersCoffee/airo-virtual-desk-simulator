// Esp32HardwareAdapter: the board build's HardwareAdapter (spec section 8.1).
//
// Maps the five keypad functions, the selector and the case B UART onto the
// signals in PinMap, through BenchIo. It is portable C++ so the same code is
// unit-tested on the laptop and runs in `airo-host --adapter esp32`; the
// ESP32-S3 firmware only supplies the Gpio/Uart drivers.
//
// Signal semantics mirror VirtualHardwareAdapter exactly (same codes from
// airo/keypad_codes.h), so the laptop e2e matrix and the hardware-in-the-loop
// run test the same behaviour.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "airo/board/bench_io.h"
#include "airo/frames.h"
#include "airo/hardware_adapter.h"

namespace airo::board {

class Esp32HardwareAdapter : public HardwareAdapter {
 public:
  Esp32HardwareAdapter(BenchIo& io, Clock& clock, InterfaceCase c, uint32_t heartbeat_ms = 50);

  /// Bench only: the board under test models another case. Releases all.
  void setInterfaceCase(InterfaceCase c);
  InterfaceCase interfaceCase() const { return case_; }
  /// Inputs are meaningful (bench link attached, or physical inputs).
  void setInputsReady(bool ready);
  /// Bench "cut Airo power": outputs released, heartbeat stops.
  void setPowered(bool on);
  bool powered() const { return powered_; }

  /// Heartbeat, tap timing, UART repeat and RX. Call every loop (<= 5 ms).
  void service();

  bool selfTest() override;
  void arm(bool on) override;
  void press(Action a) override;
  void tap(Action a, uint16_t ms) override;
  void releaseAll() override;
  void stopMotion(std::optional<Direction> travel) override;
  bool manualActive() override;
  bool bypassActive() override;
  std::optional<uint16_t> heightMm() override;
  std::optional<uint32_t> heightAgeMs() override;
  std::optional<DeskEvent> pollEvent() override;

  /// Why selfTest() would fail; empty when it passes.
  std::string selfTestProblem();

 private:
  void assertAction(std::optional<Action> a);
  void sendFrame(uint8_t cmd);
  void readUart(uint64_t now);

  BenchIo& io_;
  Clock& clock_;
  InterfaceCase case_;
  uint32_t heartbeat_ms_;
  bool inputs_ready_ = false;
  bool powered_ = true;

  bool armed_ = false;
  std::optional<Action> asserted_;
  uint64_t tap_until_ = 0;
  bool hb_level_ = false;
  uint64_t last_hb_ = 0;
  uint64_t last_repeat_ = 0;

  frames::Decoder htx_decoder_;
  frames::Decoder dtx_decoder_;
  uint64_t last_keypad_frame_at_ = 0;
  bool keypad_frame_seen_ = false;
  std::optional<uint16_t> height_;
  uint64_t height_at_ = 0;
  std::deque<DeskEvent> events_;
};

}  // namespace airo::board
