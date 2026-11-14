#include "airo/board/esp32_hardware_adapter.h"

#include "airo/keypad_codes.h"

namespace airo::board {

namespace {
constexpr Signal kHs[] = {Signal::InjHs0, Signal::InjHs1, Signal::InjHs2, Signal::InjHs3};
constexpr Signal kSenseHs[] = {Signal::SenseHs0, Signal::SenseHs1, Signal::SenseHs2, Signal::SenseHs3};
constexpr Signal kSenseA1[] = {Signal::SenseUp, Signal::SenseDown, Signal::SenseSit, Signal::SenseStand,
                               Signal::SenseReset};
}  // namespace

Esp32HardwareAdapter::Esp32HardwareAdapter(BenchIo& io, Clock& clock, InterfaceCase c, uint32_t heartbeat_ms)
    : io_(io), clock_(clock), case_(c), heartbeat_ms_(heartbeat_ms) {
  io_.configure(case_);
}

void Esp32HardwareAdapter::setInterfaceCase(InterfaceCase c) {
  releaseAll();
  arm(false);
  case_ = c;
  io_.configure(c);
  keypad_frame_seen_ = false;
  height_.reset();
  htx_decoder_ = {};
  dtx_decoder_ = {};
}

void Esp32HardwareAdapter::setInputsReady(bool ready) {
  inputs_ready_ = ready;
  if (!ready) {
    keypad_frame_seen_ = false;
    height_.reset();
  }
}

void Esp32HardwareAdapter::setPowered(bool on) {
  powered_ = on;
  asserted_.reset();
  tap_until_ = 0;
  armed_ = false;
  io_.releaseOutputs();  // an unpowered core drives nothing, heartbeat included
  hb_level_ = false;
}

void Esp32HardwareAdapter::service() {
  if (!powered_) return;
  const uint64_t now = clock_.nowMs();
  if (now - last_hb_ >= heartbeat_ms_) {
    last_hb_ = now;
    hb_level_ = !hb_level_;
    io_.write(Signal::Heartbeat, hb_level_);  // the board watchdog retriggers on each edge
  }
  if (tap_until_ != 0 && now >= tap_until_) {
    tap_until_ = 0;
    assertAction(std::nullopt);
  }
  if (case_ == InterfaceCase::B) {
    readUart(now);
    if (asserted_ && tap_until_ == 0 && now - last_repeat_ >= keypad::kFrameRepeatMs) {
      if (auto cmd = keypad::frameCommand(*asserted_)) sendFrame(*cmd);
      last_repeat_ = now;
    }
  }
}

void Esp32HardwareAdapter::readUart(uint64_t now) {
  for (const auto& f : htx_decoder_.feed(io_.uartRead(UartLine::HtxRx))) {
    last_keypad_frame_at_ = now;
    keypad_frame_seen_ = true;
    // K2 energised: Airo is the single driver of controller HTX and forwards
    // every keypad frame byte-for-byte (spec 6.5).
    if (armed_) io_.uartWrite(frames::encode(f.addr, f.cmd, f.params));
  }
  for (const auto& f : dtx_decoder_.feed(io_.uartRead(UartLine::DtxRx))) {
    if (auto h = frames::heightOf(f)) {
      height_ = h;
      height_at_ = now;
    }
  }
}

std::string Esp32HardwareAdapter::selfTestProblem() {
  if (!powered_) return "unpowered";
  if (!inputs_ready_) return "no bench link and inputs are not physical";
  const auto errs = validate(io_.pins(), case_);
  if (!errs.empty()) return errs.front();
  return {};
}

bool Esp32HardwareAdapter::selfTest() { return selfTestProblem().empty(); }

void Esp32HardwareAdapter::arm(bool on) {
  armed_ = on && powered_;
  io_.write(Signal::Arm, armed_);
}

void Esp32HardwareAdapter::assertAction(std::optional<Action> a) {
  asserted_ = a;
  switch (case_) {
    case InterfaceCase::A1: {
      std::vector<std::pair<Signal, bool>> v;
      for (Action x : {Action::Up, Action::Down, Action::Sitting, Action::Standing}) v.emplace_back(*injectorFor(x), a == x);
      io_.writeGroup(v);
      break;
    }
    case InterfaceCase::A2: {
      const int code = a ? keypad::encodedCode(*a) : 0;
      std::vector<std::pair<Signal, bool>> v;
      for (int bit = 0; bit < 4; ++bit) v.emplace_back(kHs[bit], ((code >> bit) & 1) != 0);
      io_.writeGroup(v);
      break;
    }
    case InterfaceCase::B: break;  // UART: releasing = stop sending frames
  }
}

void Esp32HardwareAdapter::sendFrame(uint8_t cmd) { io_.uartWrite(frames::handset(cmd)); }

void Esp32HardwareAdapter::press(Action a) {
  if (a == Action::Reset || !powered_) return;  // reset is never driven
  tap_until_ = 0;
  if (case_ == InterfaceCase::B) {
    asserted_ = a;
    if (auto cmd = keypad::frameCommand(a)) sendFrame(*cmd);
    last_repeat_ = clock_.nowMs();
  } else {
    // Release first so two codes are never on the lines at once.
    if (asserted_ && *asserted_ != a) assertAction(std::nullopt);
    assertAction(a);
  }
}

void Esp32HardwareAdapter::tap(Action a, uint16_t ms) {
  if (a == Action::Reset || !powered_) return;
  if (case_ == InterfaceCase::B) {
    if (auto cmd = keypad::frameCommand(a)) sendFrame(*cmd);  // one frame is a tap
    asserted_.reset();
    tap_until_ = 0;
    return;
  }
  if (asserted_ && *asserted_ != a) assertAction(std::nullopt);
  assertAction(a);
  tap_until_ = clock_.nowMs() + ms;
}

void Esp32HardwareAdapter::releaseAll() {
  tap_until_ = 0;
  assertAction(std::nullopt);
}

void Esp32HardwareAdapter::stopMotion(std::optional<Direction> travel) {
  if (case_ == InterfaceCase::B) {
    sendFrame(frames::kCmdStop);
    return;
  }
  // Switch lines: tap the direction opposite to travel [ASSUMPTION], as the
  // virtual adapter does. Bench stage T6 has to verify this on the desk.
  const Direction d = travel ? opposite(*travel) : Direction::Down;
  tap(d == Direction::Up ? Action::Up : Action::Down, keypad::kStopTapMs);
}

bool Esp32HardwareAdapter::manualActive() {
  if (!powered_ || !inputs_ready_) return false;
  switch (case_) {
    case InterfaceCase::A1:
      for (Signal s : kSenseA1)
        if (io_.read(s)) return true;
      return false;
    case InterfaceCase::A2:
      for (Signal s : kSenseHs)
        if (io_.read(s)) return true;
      return false;
    case InterfaceCase::B:
      return keypad_frame_seen_ && clock_.nowMs() - last_keypad_frame_at_ < keypad::kFrameActiveMs;
  }
  return false;
}

bool Esp32HardwareAdapter::bypassActive() { return inputs_ready_ && io_.read(Signal::BypassState); }

std::optional<uint16_t> Esp32HardwareAdapter::heightMm() {
  if (!height_ || clock_.nowMs() - height_at_ > keypad::kHeightValidMs) return std::nullopt;
  return height_;
}

std::optional<uint32_t> Esp32HardwareAdapter::heightAgeMs() {
  if (!heightMm()) return std::nullopt;
  return static_cast<uint32_t>(clock_.nowMs() - height_at_);
}

std::optional<DeskEvent> Esp32HardwareAdapter::pollEvent() {
  if (events_.empty()) return std::nullopt;
  DeskEvent e = events_.front();
  events_.pop_front();
  return e;
}

}  // namespace airo::board
