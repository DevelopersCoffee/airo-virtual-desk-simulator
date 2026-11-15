#include "airo/board/bench_io.h"

#include <utility>

namespace airo::board {

const char* toString(Mode m) { return m == Mode::Bench ? "bench" : "desk"; }
const char* toString(InputSource s) { return s == InputSource::Link ? "link" : "physical"; }

BenchIo::BenchIo(Gpio& gpio, Uart* htx, Uart* dtx, PinMap map, bool desk_mode_compiled)
    : gpio_(gpio), htx_(htx), dtx_(dtx), map_(std::move(map)), desk_mode_compiled_(desk_mode_compiled) {}

void BenchIo::configure(InterfaceCase c) {
  // Release whatever the previous case used before touching the new pins.
  releaseOutputs();
  if (case_) {
    for (Signal s : signalsFor(*case_))
      if (map_[s].gpio >= 0) gpio_.release(map_[s].gpio);
    if (htx_) htx_->attachTx(-1);
  }
  case_ = c;
  for (Signal s : signalsFor(c)) {
    const PinAssignment& p = map_[s];
    if (p.gpio < 0) continue;
    if (isOutput(s)) gpio_.release(p.gpio);  // Hi-Z until apply() decides
    else gpio_.input(p.gpio, p.active_low);  // pull-up for active-low buttons
  }
  if (c == InterfaceCase::B) {
    if (htx_) htx_->begin(map_.uart(UartLine::HtxRx), map_.uart_baud);
    if (dtx_) dtx_->begin(map_.uart(UartLine::DtxRx), map_.uart_baud);
  }
  applyAll();
}

bool BenchIo::setMode(Mode m, std::string* why) {
  if (m == Mode::Desk && !desk_mode_compiled_) {
    if (why) *why = "desk mode is not compiled into this firmware (AIRO_ALLOW_DESK_MODE)";
    return false;
  }
  if (m == mode_) return true;
  releaseOutputs();
  mode_ = m;
  bench_drive_ = false;
  applyAll();
  return true;
}

bool BenchIo::setBenchDrive(bool on, std::string* why) {
  if (mode_ != Mode::Bench) {
    if (why) *why = "not in bench mode";
    return false;
  }
  if (on == bench_drive_) return true;
  releaseOutputs();  // never switch drive with a line asserted
  bench_drive_ = on;
  applyAll();
  return true;
}

bool BenchIo::setInputs(InputSource s, std::string* why) {
  if (mode_ != Mode::Bench) {
    if (why) *why = "inputs are always physical outside bench mode";
    return false;
  }
  inputs_ = s;
  return true;
}

bool BenchIo::used(Signal s) const {
  if (!case_ || map_[s].gpio < 0) return false;
  for (Signal u : signalsFor(*case_))
    if (u == s) return true;
  return false;
}

void BenchIo::apply(Signal s) {
  if (!used(s)) return;
  const PinAssignment& p = map_[s];
  if (driveEnabled()) {
    gpio_.drive(p.gpio, asserted(s) != p.active_low);
    ++physical_writes_;
  } else {
    gpio_.release(p.gpio);
  }
}

void BenchIo::applyAll() {
  for (int i = 0; i < kFirstInput; ++i) apply(static_cast<Signal>(i));
  if (htx_ && case_ == InterfaceCase::B) htx_->attachTx(driveEnabled() ? map_.uart(UartLine::HtxTx) : -1);
}

void BenchIo::writeGroup(const std::vector<std::pair<Signal, bool>>& values) {
  std::vector<std::pair<Signal, bool>> changed;
  for (const auto& [s, on] : values) {
    if (!isOutput(s)) continue;
    const size_t i = static_cast<size_t>(s);
    if (shadow_[i] == on) continue;
    shadow_[i] = on;
    changed.emplace_back(s, on);
  }
  if (changed.empty()) return;
  if (driveEnabled()) {
    std::vector<std::pair<int, bool>> levels;
    for (const auto& [s, on] : changed)
      if (used(s)) levels.emplace_back(map_[s].gpio, on != map_[s].active_low);
    if (!levels.empty()) {
      gpio_.driveMany(levels);
      ++physical_writes_;
    }
  }
  // Drive off: the pins were released when drive was turned off or the case
  // configured, and stay high-impedance. Only the logical value changes.
  if (onOutput) onOutput(changed);
}

bool BenchIo::read(Signal s) {
  if (isOutput(s)) return asserted(s);
  if (inputs() == InputSource::Link) return link_in_[static_cast<size_t>(s)];
  const PinAssignment& p = map_[s];
  if (p.gpio < 0) return false;
  return gpio_.read(p.gpio) != p.active_low;
}

void BenchIo::uartWrite(const std::vector<uint8_t>& bytes) {
  if (driveEnabled() && htx_) {
    htx_->write(bytes);
    ++physical_writes_;
  }
  if (onTx) onTx(bytes);
}

std::vector<uint8_t> BenchIo::uartRead(UartLine rx) {
  if (inputs() == InputSource::Link) {
    std::vector<uint8_t> out;
    out.swap(link_rx_[static_cast<int>(rx)]);
    return out;
  }
  Uart* u = rx == UartLine::DtxRx ? dtx_ : rx == UartLine::HtxRx ? htx_ : nullptr;
  return u ? u->read() : std::vector<uint8_t>{};
}

void BenchIo::releaseOutputs() {
  std::vector<std::pair<Signal, bool>> all;
  for (int i = 0; i < kFirstInput; ++i) all.emplace_back(static_cast<Signal>(i), false);
  writeGroup(all);
}

void BenchIo::linkSet(Signal s, bool on) {
  if (!isOutput(s)) link_in_[static_cast<size_t>(s)] = on;
}

void BenchIo::linkRx(UartLine rx, const std::vector<uint8_t>& bytes) {
  auto& buf = link_rx_[static_cast<int>(rx)];
  buf.insert(buf.end(), bytes.begin(), bytes.end());
  if (buf.size() > 4096) buf.erase(buf.begin(), buf.end() - 4096);
}

void BenchIo::linkClear() {
  link_in_.fill(false);
  for (auto& b : link_rx_) b.clear();
}

}  // namespace airo::board
