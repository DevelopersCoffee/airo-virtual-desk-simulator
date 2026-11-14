#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "airo/command_service.h"

namespace airo_test {

struct FakeClock : airo::Clock {
  uint64_t t = 1000;
  uint64_t nowMs() override { return t; }
};

/// Records every call so tests can check ordering and timing.
struct FakeAdapter : airo::HardwareAdapter {
  struct Call {
    std::string what;
    uint64_t at;
  };

  explicit FakeAdapter(FakeClock& c) : clock(c) {}

  FakeClock& clock;
  bool self_test_ok = true;
  bool manual = false;
  bool bypass = false;
  std::optional<uint16_t> height;
  std::deque<airo::DeskEvent> events;

  bool armed = false;
  std::optional<airo::Action> asserted;
  int presses = 0;
  int taps = 0;
  int stops = 0;
  bool both_directions_ever = false;
  std::vector<Call> calls;

  void log(const std::string& s) { calls.push_back({s, clock.t}); }

  bool selfTest() override { return self_test_ok; }
  void arm(bool on) override {
    armed = on;
    log(on ? "arm" : "disarm");
  }
  void press(airo::Action a) override {
    if (asserted && airo::directionOf(*asserted) && airo::directionOf(a) && *asserted != a) both_directions_ever = true;
    asserted = a;
    ++presses;
    log(std::string("press ") + airo::toString(a));
  }
  void tap(airo::Action a, uint16_t) override {
    ++taps;
    log(std::string("tap ") + airo::toString(a));
  }
  void releaseAll() override {
    asserted.reset();
    log("release");
  }
  void stopMotion(std::optional<airo::Direction>) override {
    ++stops;
    log("stop_method");
  }
  bool manualActive() override { return manual; }
  bool bypassActive() override { return bypass; }
  std::optional<uint16_t> heightMm() override { return height; }
  std::optional<airo::DeskEvent> pollEvent() override {
    if (events.empty()) return std::nullopt;
    auto e = events.front();
    events.pop_front();
    return e;
  }

  std::optional<uint64_t> lastCallAt(const std::string& what) const {
    for (auto it = calls.rbegin(); it != calls.rend(); ++it)
      if (it->what == what) return it->at;
    return std::nullopt;
  }
};

struct Rig {
  FakeClock clock;
  FakeAdapter hw{clock};
  airo::CommandService core;
  int next_id = 1;
  std::vector<airo::CoreEvent> events;

  explicit Rig(airo::Config cfg = {}) : core(hw, clock, cfg) {}

  void boot() {
    core.begin();
    advance(core.config().boot_quiet_ms + 10);
  }

  /// Advance time in 1 ms ticks.
  void advance(uint64_t ms) {
    for (uint64_t i = 0; i < ms; ++i) {
      clock.t += 1;
      core.tick();
      for (auto& e : core.takeEvents()) events.push_back(e);
    }
  }

  airo::CommandResult send(const std::string& cmd, const std::string& source = "test",
                           std::optional<uint32_t> lease = std::nullopt, std::string request_id = {},
                           std::string client = {}) {
    airo::CommandEnvelope e;
    e.device_id = core.config().device_id;
    e.command = cmd;
    e.source = source;
    e.request_id = request_id.empty() ? "req-" + std::to_string(next_id++) : request_id;
    e.lease_ms = lease;
    e.client_id = client;
    auto r = core.submit(e);
    for (auto& ev : core.takeEvents()) events.push_back(ev);
    return r;
  }

  bool sawEvent(airo::CoreEventKind k) const {
    for (const auto& e : events)
      if (e.kind == k) return true;
    return false;
  }

  airo::DeskState state() { return core.status().state; }
};

}  // namespace airo_test
