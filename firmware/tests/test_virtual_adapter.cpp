#include <vector>

#include "fakes.h"
#include "test.h"
#include "virtual_hardware_adapter.h"

using namespace airo;
using airo::host::VirtualHardwareAdapter;

namespace {
struct Wire {
  airo_test::FakeClock clock;
  VirtualHardwareAdapter hw{clock};
  std::vector<Json> sent;
  void connect(const char* c) {
    hw.attach([this](const std::string& s) { sent.push_back(*Json::parse(s)); });
    Json hello = Json::object();
    hello.set("t", "hello");
    hello.set("case", c);
    hw.onBridgeMessage(hello);
  }
  void msg(const std::string& text) { hw.onBridgeMessage(*Json::parse(text)); }
  std::vector<Json> of(const std::string& t) const {
    std::vector<Json> out;
    for (const auto& j : sent)
      if (j["t"].asString() == t) out.push_back(j);
    return out;
  }
};
}  // namespace

TEST("virtual adapter: self-test needs a connected, powered board") {
  Wire w;
  CHECK(!w.hw.selfTest());
  w.connect("A1");
  CHECK(w.hw.selfTest());
  w.msg(R"({"t":"sim.power","on":false})");
  CHECK(!w.hw.selfTest());
}

TEST("virtual adapter: A1 drives one line per button and senses keypad lines") {
  Wire w;
  w.connect("A1");
  w.hw.arm(true);
  w.hw.press(Action::Up);
  auto inj = w.of("inject");
  CHECK(!inj.empty() && inj.back()["lines"].asArray().size() == 1 &&
        inj.back()["lines"].asArray()[0].asString() == "up");
  w.hw.releaseAll();
  CHECK(w.of("inject").back()["lines"].asArray().empty());
  CHECK(!w.hw.manualActive());
  w.msg(R"({"t":"sense","lines":["down"]})");
  CHECK(w.hw.manualActive());
}

TEST("virtual adapter: A2 uses encoded HS codes") {
  Wire w;
  w.connect("A2");
  w.hw.press(Action::Down);
  CHECK_EQ(w.of("inject").back()["hs"].asNumber(), 1.0);
  w.hw.tap(Action::Sitting, 150);
  CHECK_EQ(w.of("inject").back()["hs"].asNumber(), 3.0);
  w.clock.t += 151;
  w.hw.service();
  CHECK_EQ(w.of("inject").back()["hs"].asNumber(), 0.0);
  w.msg(R"({"t":"sense","hs":2})");
  CHECK(w.hw.manualActive());
}

TEST("virtual adapter: B repeats frames while held and stops when released") {
  Wire w;
  w.connect("B");
  w.hw.press(Action::Up);
  for (int i = 0; i < 10; ++i) {
    w.clock.t += 10;
    w.hw.service();
  }
  const size_t n = w.of("uart").size();
  CHECK(n >= 2 && n <= 3);
  w.hw.releaseAll();
  for (int i = 0; i < 10; ++i) {
    w.clock.t += 10;
    w.hw.service();
  }
  CHECK_EQ(w.of("uart").size(), n);
  w.hw.stopMotion(std::nullopt);
  auto last = w.of("uart").back()["bytes"].asArray();
  CHECK(last.size() == 6 && last[2].asNumber() == 0x2B);
}

TEST("virtual adapter: B reads height from DTX and forwards keypad frames when armed") {
  Wire w;
  w.connect("B");
  w.msg(R"({"t":"uart","line":"dtx","bytes":[242,242,1,3,5,6,7,22,126]})");
  CHECK(w.hw.heightMm() == std::optional<uint16_t>(1286));
  w.clock.t += 1500;
  CHECK(!w.hw.heightMm().has_value());  // stale
  w.hw.arm(true);
  const size_t before = w.of("uart").size();
  w.msg(R"({"t":"uart","line":"htx","bytes":[241,241,2,0,2,126]})");
  CHECK(w.hw.manualActive());
  CHECK_EQ(w.of("uart").size(), before + 1);  // forwarded byte-for-byte
  w.clock.t += 200;
  CHECK(!w.hw.manualActive());
}

TEST("virtual adapter: heartbeat every watchdog period; nothing sent when unpowered") {
  Wire w;
  w.connect("A1");
  for (int i = 0; i < 100; ++i) {
    w.clock.t += 1;
    w.hw.service();
  }
  CHECK(w.of("hb").size() >= 2);
  w.msg(R"({"t":"sim.power","on":false})");
  const size_t n = w.sent.size();
  for (int i = 0; i < 100; ++i) {
    w.clock.t += 1;
    w.hw.service();
  }
  w.hw.press(Action::Up);
  CHECK_EQ(w.sent.size(), n);
}

TEST("virtual adapter: MANUAL ONLY switch reads as bypass") {
  Wire w;
  w.connect("A2");
  CHECK(!w.hw.bypassActive());
  w.msg(R"({"t":"bypass","on":true})");
  CHECK(w.hw.bypassActive());
}
