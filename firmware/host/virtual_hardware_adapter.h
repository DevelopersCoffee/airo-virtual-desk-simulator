// VirtualHardwareAdapter: the laptop stand-in for Esp32HardwareAdapter.
//
// It speaks the JSON bridge protocol (docs/LAPTOP_SIMULATION.md) to the
// simulated Airo Interface Board running inside the 3D simulator. Port C
// signals map to bridge messages: HEARTBEAT -> "hb", ARM -> "arm",
// injector / UART TX -> "inject" / "uart", sense / BYPASS_STATE / UART RX
// <- "sense" / "bypass" / "uart".
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "airo/frames.h"
#include "airo/hardware_adapter.h"
#include "airo/json.h"

namespace airo::host {

class VirtualHardwareAdapter : public HardwareAdapter {
 public:
  using Send = std::function<void(const std::string&)>;

  explicit VirtualHardwareAdapter(Clock& clock, uint32_t heartbeat_ms = 50);

  void attach(Send send);
  void detach();
  bool connected() const { return static_cast<bool>(send_); }
  bool helloReceived() const { return hello_; }
  bool powered() const { return powered_; }
  std::optional<InterfaceCase> interfaceCase() const { return hello_ ? std::optional(case_) : std::nullopt; }

  enum class BridgeResult { None, Hello, PowerChanged, CaseChanged, Invalid };
  BridgeResult onBridgeMessage(const Json& msg);
  /// Heartbeat, UART repeat while held, tap timing. Call every loop.
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

  /// A2 codes on HS2..HS0 (inverted logic shown as numbers) [COMMUNITY].
  static int encodedCode(Action a);
  static const char* lineName(Action a);
  static std::optional<uint8_t> frameCommand(Action a);

 private:
  void send(const Json& j);
  void pushInject();
  void sendFrame(uint8_t cmd);

  Clock& clock_;
  uint32_t heartbeat_ms_;
  Send send_;
  bool hello_ = false;
  bool powered_ = true;
  InterfaceCase case_ = InterfaceCase::A1;
  bool manual_only_ = false;

  uint32_t keypad_lines_ = 0;  // A1 bitmask of pressed buttons
  int keypad_hs_ = 0;          // A2 code
  uint64_t last_keypad_frame_at_ = 0;
  bool keypad_frame_seen_ = false;
  frames::Decoder htx_decoder_;
  frames::Decoder dtx_decoder_;
  std::optional<uint16_t> height_;
  uint64_t height_at_ = 0;

  bool armed_ = false;
  std::optional<Action> asserted_;
  uint64_t tap_until_ = 0;
  uint64_t last_hb_ = 0;
  uint64_t last_repeat_ = 0;
  std::deque<DeskEvent> events_;
};

}  // namespace airo::host
