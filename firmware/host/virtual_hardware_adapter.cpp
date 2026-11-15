#include "virtual_hardware_adapter.h"

#include "airo/keypad_codes.h"

namespace airo::host {

namespace {
using keypad::kFrameActiveMs;
using keypad::kFrameRepeatMs;
using keypad::kHeightValidMs;

uint32_t bitOf(Action a) { return 1u << static_cast<unsigned>(a); }
}  // namespace

VirtualHardwareAdapter::VirtualHardwareAdapter(Clock& clock, uint32_t heartbeat_ms)
    : clock_(clock), heartbeat_ms_(heartbeat_ms) {}

void VirtualHardwareAdapter::attach(Send send) {
  send_ = std::move(send);
  hello_ = false;
  powered_ = true;
  keypad_lines_ = 0;
  keypad_hs_ = 0;
  keypad_frame_seen_ = false;
  height_.reset();
  asserted_.reset();
  armed_ = false;
  events_.clear();
}

void VirtualHardwareAdapter::detach() {
  send_ = nullptr;
  hello_ = false;
  keypad_lines_ = 0;
  keypad_hs_ = 0;
  keypad_frame_seen_ = false;
  height_.reset();
  asserted_.reset();
  armed_ = false;
}

int VirtualHardwareAdapter::encodedCode(Action a) { return keypad::encodedCode(a); }

const char* VirtualHardwareAdapter::lineName(Action a) { return keypad::lineName(a); }

std::optional<uint8_t> VirtualHardwareAdapter::frameCommand(Action a) { return keypad::frameCommand(a); }

void VirtualHardwareAdapter::send(const Json& j) {
  if (send_ && powered_) send_(j.dump());
}

VirtualHardwareAdapter::BridgeResult VirtualHardwareAdapter::onBridgeMessage(const Json& m) {
  const std::string& t = m["t"].asString();
  const uint64_t now = clock_.nowMs();
  if (t == "hello") {
    auto c = parseInterfaceCase(m["case"].asString());
    if (!c) return BridgeResult::Invalid;
    case_ = *c;
    manual_only_ = m["manual_only"].asBool();
    powered_ = m.has("powered") ? m["powered"].asBool() : true;
    hello_ = true;
    return BridgeResult::Hello;
  }
  if (!hello_) return BridgeResult::Invalid;
  if (t == "case") {
    auto c = parseInterfaceCase(m["case"].asString());
    if (!c) return BridgeResult::Invalid;
    case_ = *c;
    keypad_lines_ = 0;
    keypad_hs_ = 0;
    height_.reset();
    asserted_.reset();
    return BridgeResult::CaseChanged;
  }
  if (t == "sim.power") {
    powered_ = m["on"].asBool();
    asserted_.reset();
    armed_ = false;
    return BridgeResult::PowerChanged;
  }
  if (!powered_) return BridgeResult::None;  // an unpowered core sees nothing
  if (t == "bypass") {
    manual_only_ = m["on"].asBool();
  } else if (t == "sense") {
    if (case_ == InterfaceCase::A1) {
      uint32_t bits = 0;
      for (const Json& l : m["lines"].asArray()) {
        for (Action a : {Action::Up, Action::Down, Action::Sitting, Action::Standing, Action::Reset})
          if (l.asString() == lineName(a)) bits |= bitOf(a);
      }
      keypad_lines_ = bits;
    } else if (case_ == InterfaceCase::A2) {
      keypad_hs_ = static_cast<int>(m["hs"].asNumber(0));
    }
  } else if (t == "uart") {
    std::vector<uint8_t> bytes;
    for (const Json& b : m["bytes"].asArray()) bytes.push_back(static_cast<uint8_t>(b.asNumber(0)));
    if (m["line"].asString() == "htx") {
      // Keypad -> Airo (only reaches us through the sense tap / RX buffer).
      for (const auto& f : htx_decoder_.feed(bytes)) {
        last_keypad_frame_at_ = now;
        keypad_frame_seen_ = true;
        // K2 energised: Airo is the single driver of controller HTX and must
        // forward every keypad frame byte-for-byte (spec 6.5).
        if (armed_) {
          Json fwd = Json::object();
          fwd.set("t", "uart");
          fwd.set("line", "htx");
          Json arr = Json::array();
          for (uint8_t b : frames::encode(f.addr, f.cmd, f.params)) arr.push(static_cast<unsigned>(b));
          fwd.set("bytes", arr);
          send(fwd);
        }
      }
    } else if (m["line"].asString() == "dtx") {
      for (const auto& f : dtx_decoder_.feed(bytes)) {
        if (auto h = frames::heightOf(f)) {
          height_ = h;
          height_at_ = now;
        }
      }
    }
  } else if (t == "controller.error") {
    events_.push_back({DeskEventKind::ControllerError, std::nullopt, m["detail"].asString()});
  } else {
    return BridgeResult::Invalid;
  }
  return BridgeResult::None;
}

void VirtualHardwareAdapter::service() {
  const uint64_t now = clock_.nowMs();
  if (!connected() || !hello_ || !powered_) return;
  if (now - last_hb_ >= heartbeat_ms_) {
    last_hb_ = now;
    Json hb = Json::object();
    hb.set("t", "hb");
    send(hb);
  }
  if (tap_until_ != 0 && now >= tap_until_) {
    tap_until_ = 0;
    asserted_.reset();
    pushInject();
  }
  if (case_ == InterfaceCase::B && asserted_ && tap_until_ == 0 && now - last_repeat_ >= kFrameRepeatMs) {
    if (auto cmd = frameCommand(*asserted_)) sendFrame(*cmd);
    last_repeat_ = now;
  }
}

bool VirtualHardwareAdapter::selfTest() { return connected() && hello_ && powered_; }

void VirtualHardwareAdapter::arm(bool on) {
  armed_ = on;
  Json j = Json::object();
  j.set("t", "arm");
  j.set("on", on);
  send(j);
}

void VirtualHardwareAdapter::pushInject() {
  if (case_ == InterfaceCase::B) return;  // UART: releasing = stop sending frames
  Json j = Json::object();
  j.set("t", "inject");
  if (case_ == InterfaceCase::A1) {
    Json lines = Json::array();
    if (asserted_) lines.push(lineName(*asserted_));
    j.set("lines", lines);
  } else {
    j.set("hs", asserted_ ? encodedCode(*asserted_) : 0);
  }
  send(j);
}

void VirtualHardwareAdapter::sendFrame(uint8_t cmd) {
  Json j = Json::object();
  j.set("t", "uart");
  j.set("line", "htx");
  Json arr = Json::array();
  for (uint8_t b : frames::handset(cmd)) arr.push(static_cast<unsigned>(b));
  j.set("bytes", arr);
  send(j);
}

void VirtualHardwareAdapter::press(Action a) {
  if (a == Action::Reset) return;
  asserted_ = a;
  tap_until_ = 0;
  if (case_ == InterfaceCase::B) {
    if (auto cmd = frameCommand(a)) sendFrame(*cmd);
    last_repeat_ = clock_.nowMs();
  } else {
    pushInject();
  }
}

void VirtualHardwareAdapter::tap(Action a, uint16_t ms) {
  if (a == Action::Reset) return;
  if (case_ == InterfaceCase::B) {
    // One frame is a tap.
    if (auto cmd = frameCommand(a)) sendFrame(*cmd);
    asserted_.reset();
    tap_until_ = 0;
    return;
  }
  asserted_ = a;
  tap_until_ = clock_.nowMs() + ms;
  pushInject();
}

void VirtualHardwareAdapter::releaseAll() {
  const bool was = asserted_.has_value();
  asserted_.reset();
  tap_until_ = 0;
  if (was || case_ != InterfaceCase::B) pushInject();
}

void VirtualHardwareAdapter::stopMotion(std::optional<Direction> travel) {
  if (case_ == InterfaceCase::B) {
    sendFrame(frames::kCmdStop);
    return;
  }
  // Switch lines: tap the direction opposite to travel [ASSUMPTION]; any key
  // stops a preset on the simulated controller.
  const Direction d = travel ? opposite(*travel) : Direction::Down;
  tap(d == Direction::Up ? Action::Up : Action::Down, 150);
}

bool VirtualHardwareAdapter::manualActive() {
  if (!hello_ || !powered_) return false;
  switch (case_) {
    case InterfaceCase::A1: return keypad_lines_ != 0;
    case InterfaceCase::A2: return keypad_hs_ != 0;
    case InterfaceCase::B: return keypad_frame_seen_ && clock_.nowMs() - last_keypad_frame_at_ < kFrameActiveMs;
  }
  return false;
}

bool VirtualHardwareAdapter::bypassActive() { return hello_ && manual_only_; }

std::optional<uint16_t> VirtualHardwareAdapter::heightMm() {
  if (!height_ || clock_.nowMs() - height_at_ > kHeightValidMs) return std::nullopt;
  return height_;
}

std::optional<uint32_t> VirtualHardwareAdapter::heightAgeMs() {
  if (!heightMm()) return std::nullopt;
  return static_cast<uint32_t>(clock_.nowMs() - height_at_);
}

std::optional<DeskEvent> VirtualHardwareAdapter::pollEvent() {
  if (events_.empty()) return std::nullopt;
  DeskEvent e = events_.front();
  events_.pop_front();
  return e;
}

}  // namespace airo::host
