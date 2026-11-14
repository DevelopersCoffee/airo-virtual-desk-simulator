#include "airo/board/bench_link.h"

#include <utility>

namespace airo::board {

BenchLink::BenchLink(BenchIo& io, Esp32HardwareAdapter& hw) : io_(io), hw_(hw) {}

bool BenchLink::open(Send sender, std::string* why) {
  if (!io_.benchMode()) {
    if (why) *why = "not in bench mode";
    return false;
  }
  if (send_) {
    if (why) *why = "a bench link is already open";
    return false;
  }
  send_ = std::move(sender);
  hello_ = false;
  io_.linkClear();
  io_.onOutput = [this](const std::vector<std::pair<Signal, bool>>& changed) {
    Json s = Json::object();
    for (const auto& [sig, on] : changed) s.set(toString(sig), on);
    Json j = Json::object();
    j.set("t", "out");
    j.set("s", s);
    this->send(j);
  };
  io_.onTx = [this](const std::vector<uint8_t>& bytes) {
    Json arr = Json::array();
    for (uint8_t b : bytes) arr.push(static_cast<unsigned>(b));
    Json j = Json::object();
    j.set("t", "tx");
    j.set("line", "htx");
    j.set("bytes", arr);
    this->send(j);
  };
  sendState();
  return true;
}

void BenchLink::close() {
  send_ = nullptr;
  hello_ = false;
  io_.onOutput = nullptr;
  io_.onTx = nullptr;
  io_.linkClear();
  if (io_.inputs() == InputSource::Link) hw_.setInputsReady(false);
}

void BenchLink::send(const Json& j) {
  if (send_) send_(j.dump());
}

Json BenchLink::state() const {
  Json j = Json::object();
  j.set("bench_mode", io_.benchMode());
  j.set("mode", toString(io_.mode()));
  j.set("drive_enabled", io_.driveEnabled());
  j.set("inputs", toString(io_.inputs()));
  j.set("desk_mode_compiled", io_.deskModeCompiled());
  j.set("pins_verified", io_.pins().verified);
  j.set("interface_case", toString(hw_.interfaceCase()));
  // Logical output values, driven or not: what the core is asking for.
  Json outs = Json::object();
  for (Signal s : signalsFor(hw_.interfaceCase()))
    if (isOutput(s)) outs.set(toString(s), io_.asserted(s));
  j.set("outputs", outs);
  return j;
}

void BenchLink::sendState() {
  Json j = state();
  j.set("t", "bench");
  send(j);
}

BenchLink::Result BenchLink::onMessage(const Json& m) {
  if (!send_ || !io_.benchMode()) return Result::Invalid;
  const std::string& t = m["t"].asString();
  if (t == "hello") {
    auto c = parseInterfaceCase(m["case"].asString());
    if (!c) return Result::Invalid;
    io_.linkClear();
    hw_.setPowered(m.has("powered") ? m["powered"].asBool() : true);
    if (*c != hw_.interfaceCase()) hw_.setInterfaceCase(*c);
    io_.linkSet(Signal::BypassState, m["manual_only"].asBool());
    hw_.setInputsReady(true);
    hello_ = true;
    sendState();
    return Result::Hello;
  }
  if (!hello_) return Result::Invalid;
  if (t == "in") {
    if (!m["s"].isObject()) return Result::Invalid;
    for (const auto& [name, v] : m["s"].asObject()) {
      auto s = parseSignal(name);
      if (!s || isOutput(*s)) return Result::Invalid;
      io_.linkSet(*s, v.asBool());
    }
    return Result::None;
  }
  if (t == "rx") {
    const std::string& line = m["line"].asString();
    if (line != "htx" && line != "dtx") return Result::Invalid;
    std::vector<uint8_t> bytes;
    for (const Json& b : m["bytes"].asArray()) bytes.push_back(static_cast<uint8_t>(b.asNumber(0)));
    io_.linkRx(line == "htx" ? UartLine::HtxRx : UartLine::DtxRx, bytes);
    return Result::None;
  }
  if (t == "power") {
    hw_.setPowered(m["on"].asBool());
    return Result::PowerChanged;
  }
  if (t == "hang") {
    const double ms = m["ms"].asNumber(0);
    hang_ms_ = ms <= 0 ? 0 : ms > 3000 ? 3000 : static_cast<uint32_t>(ms);
    return Result::Hang;
  }
  if (t == "halt") return Result::Halt;
  if (t == "reboot") return Result::Reboot;
  return Result::Invalid;
}

}  // namespace airo::board
