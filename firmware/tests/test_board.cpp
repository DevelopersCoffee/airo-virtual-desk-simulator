// Esp32HardwareAdapter, BenchIo, BenchLink and the pin map, on in-memory pins.
// These are the desk-facing signal rules for the board build: which line each
// function drives, that bench mode never drives a pin, and that the board
// adapter produces the same codes as the virtual one.
#include <vector>

#include "airo/board/bench_link.h"
#include "airo/command_service.h"
#include "airo/keypad_codes.h"
#include "fakes.h"
#include "host_io.h"
#include "test.h"
#include "virtual_hardware_adapter.h"

using namespace airo;
using namespace airo::board;
using airo::host::HostGpio;
using airo::host::HostUart;

namespace {
struct Rig {
  airo_test::FakeClock clock;
  HostGpio gpio;
  HostUart htx, dtx;
  BenchIo io;
  Esp32HardwareAdapter hw;
  BenchLink link{io, hw};
  std::vector<Json> sent;

  explicit Rig(InterfaceCase c, bool desk_compiled = false)
      : io(gpio, &htx, &dtx, defaultPinMap(), desk_compiled), hw(io, clock, c) {}

  void connect(const char* c, bool manual_only = false) {
    CHECK(link.open([this](const std::string& s) { sent.push_back(*Json::parse(s)); }));
    Json hello = Json::object();
    hello.set("t", "hello");
    hello.set("case", c);
    hello.set("manual_only", manual_only);
    CHECK(link.onMessage(hello) == BenchLink::Result::Hello);
  }
  BenchLink::Result msg(const std::string& text) { return link.onMessage(*Json::parse(text)); }
  int gpioOf(Signal s) const { return io.pins()[s].gpio; }
  /// Last logical value the link reported for an output.
  bool out(Signal s) const {
    for (auto it = sent.rbegin(); it != sent.rend(); ++it)
      if ((*it)["t"].asString() == "out" && (*it)["s"].has(toString(s))) return (*it)["s"][toString(s)].asBool();
    return false;
  }
  int outMessages() const {
    int n = 0;
    for (const auto& j : sent) n += j["t"].asString() == "out";
    return n;
  }
  int hs() const {
    int code = 0;
    const Signal bits[] = {Signal::InjHs0, Signal::InjHs1, Signal::InjHs2, Signal::InjHs3};
    for (int b = 0; b < 4; ++b) code |= io.asserted(bits[b]) ? (1 << b) : 0;
    return code;
  }
};

bool anyOutputDriven(const Rig& r) {
  for (int i = 0; i < kFirstInput; ++i) {
    const int g = r.io.pins()[static_cast<Signal>(i)].gpio;
    if (g >= 0 && r.gpio.driven(g)) return true;
  }
  return r.htx.tx >= 0;
}
}  // namespace

TEST("pin map: placeholder map is valid for every case and marked unverified") {
  const PinMap m = defaultPinMap();
  for (InterfaceCase c : {InterfaceCase::A1, InterfaceCase::A2, InterfaceCase::B}) CHECK(validate(m, c).empty());
  CHECK(!m.verified);
  bool flagged = false;
  for (const auto& w : warnings(m, InterfaceCase::A1)) flagged = flagged || w.find("UNVERIFIED") != std::string::npos;
  CHECK(flagged);
}

TEST("pin map: strapping, USB, flash pins and duplicates are rejected") {
  PinMap m = defaultPinMap();
  m[Signal::Arm].gpio = 0;    // strapping
  m[Signal::InjUp].gpio = 19; // native USB
  m[Signal::SenseUp].gpio = m[Signal::SenseDown].gpio;
  m[Signal::Heartbeat].gpio = -1;
  const auto errs = validate(m, InterfaceCase::A1);
  CHECK(errs.size() == 4);
  // A2 does not use InjUp/SenseUp, only the arm/heartbeat problems remain.
  CHECK(validate(m, InterfaceCase::A2).size() == 2);
}

TEST("pin map: names round-trip") {
  for (int i = 0; i < kSignalCount; ++i) {
    const auto s = static_cast<Signal>(i);
    CHECK(parseSignal(toString(s)) == s);
  }
  CHECK(!parseSignal("inj_reset"));  // there is no reset injector at all
}

TEST("bench mode: default, and no output pin is ever driven") {
  Rig r(InterfaceCase::A1);
  CHECK(r.io.benchMode());
  CHECK(!r.io.driveEnabled());
  r.connect("A1");
  r.hw.arm(true);
  r.hw.press(Action::Up);
  for (int i = 0; i < 20; ++i) {
    r.clock.t += 10;
    r.hw.service();
  }
  CHECK(r.io.asserted(Signal::InjUp));  // logically asserted...
  CHECK(!anyOutputDriven(r));           // ...but nothing physical
  CHECK_EQ(r.io.physicalWrites(), 0u);
  CHECK_EQ(r.gpio.drives, 0);
  r.msg(R"({"t":"hello","case":"B"})");
  r.hw.press(Action::Down);
  CHECK(!r.htx.written.size());  // UART TX not attached either
  CHECK_EQ(r.htx.tx, -1);
}

TEST("bench mode: desk mode cannot be entered unless compiled in") {
  Rig r(InterfaceCase::A1);
  std::string why;
  CHECK(!r.io.setMode(Mode::Desk, &why));
  CHECK(why.find("AIRO_ALLOW_DESK_MODE") != std::string::npos);
  CHECK(r.io.benchMode());
  Rig d(InterfaceCase::A1, true);
  CHECK(d.io.setMode(Mode::Desk));
  CHECK(d.io.driveEnabled());
  CHECK(d.io.inputs() == InputSource::Physical);
  // And the bench link is refused outside bench mode.
  CHECK(!d.link.open([](const std::string&) {}, &why));
  CHECK(why == "not in bench mode");
}

TEST("bench drive: explicit opt-in drives breadboard pins, off releases them to Hi-Z") {
  Rig r(InterfaceCase::A1);
  r.connect("A1");
  CHECK(r.io.setBenchDrive(true));
  r.hw.press(Action::Up);
  const int up = r.gpioOf(Signal::InjUp);
  CHECK(r.gpio.driven(up) && r.gpio.pins[up].level);  // active-high LED
  CHECK(r.gpio.driven(r.gpioOf(Signal::InjDown)) && !r.gpio.pins[r.gpioOf(Signal::InjDown)].level);
  CHECK(r.io.setBenchDrive(false));
  CHECK(!anyOutputDriven(r));
  CHECK(!r.io.asserted(Signal::InjUp));  // switching drive releases everything first
}

TEST("esp32 adapter A1: one injector per button, never two") {
  Rig r(InterfaceCase::A1);
  r.connect("A1");
  r.hw.press(Action::Up);
  CHECK(r.io.asserted(Signal::InjUp) && r.out(Signal::InjUp));
  r.hw.press(Action::Down);
  CHECK(!r.io.asserted(Signal::InjUp) && r.io.asserted(Signal::InjDown));
  // The link never reported two injectors asserted at once.
  bool up = false, down = false, both = false;
  for (const auto& j : r.sent) {
    if (j["t"].asString() != "out") continue;
    if (j["s"].has("inj_up")) up = j["s"]["inj_up"].asBool();
    if (j["s"].has("inj_down")) down = j["s"]["inj_down"].asBool();
    both = both || (up && down);
  }
  CHECK(!both);
  r.hw.releaseAll();
  for (Signal s : {Signal::InjUp, Signal::InjDown, Signal::InjSit, Signal::InjStand}) CHECK(!r.io.asserted(s));
  r.hw.press(Action::Reset);
  CHECK(!r.hw.manualActive());
  for (int i = 0; i < kFirstInput; ++i)
    if (static_cast<Signal>(i) != Signal::Heartbeat) CHECK(!r.io.asserted(static_cast<Signal>(i)));
}

TEST("esp32 adapter A2: encoded codes match the virtual adapter, set in one step") {
  Rig r(InterfaceCase::A2);
  r.connect("A2");
  for (Action a : {Action::Up, Action::Down, Action::Sitting, Action::Standing}) {
    const int before = r.outMessages();
    r.hw.press(a);
    CHECK_EQ(r.hs(), host::VirtualHardwareAdapter::encodedCode(a));
    CHECK_EQ(r.hs(), keypad::encodedCode(a));
    r.hw.releaseAll();
    CHECK_EQ(r.hs(), 0);
    // press = one message, release = one message: no intermediate code on the wire.
    CHECK_EQ(r.outMessages() - before, 2);
  }
}

TEST("esp32 adapter: sense lines and MANUAL ONLY come from the bench link") {
  Rig r(InterfaceCase::A1);
  CHECK(!r.hw.selfTest());  // no link, link inputs: refuse
  r.connect("A1");
  CHECK(r.hw.selfTest());
  CHECK(!r.hw.manualActive());
  r.msg(R"({"t":"in","s":{"sense_down":true}})");
  CHECK(r.hw.manualActive());
  r.msg(R"({"t":"in","s":{"sense_down":false,"bypass_state":true}})");
  CHECK(!r.hw.manualActive());
  CHECK(r.hw.bypassActive());
  CHECK(r.msg(R"({"t":"in","s":{"inj_up":true}})") == BenchLink::Result::Invalid);  // outputs are not inputs
  r.link.close();
  CHECK(!r.hw.selfTest());
  CHECK(!r.hw.bypassActive());
}

TEST("esp32 adapter A2: encoded sense") {
  Rig r(InterfaceCase::A2);
  r.connect("A2");
  r.msg(R"({"t":"in","s":{"sense_hs1":true}})");
  CHECK(r.hw.manualActive());
}

TEST("esp32 adapter: physical inputs read the pins with polarity") {
  Rig r(InterfaceCase::A1);
  CHECK(r.io.setInputs(InputSource::Physical));
  r.hw.setInputsReady(true);
  CHECK(r.hw.selfTest());
  CHECK(!r.hw.manualActive());  // pull-up, released button reads high = not pressed
  r.gpio.inputs[r.gpioOf(Signal::SenseUp)] = false;  // button to GND
  CHECK(r.hw.manualActive());
}

TEST("esp32 adapter: heartbeat toggles every period and stops when unpowered") {
  Rig r(InterfaceCase::A2);
  r.connect("A2");
  int edges = 0;
  bool last = r.io.asserted(Signal::Heartbeat);
  for (int i = 0; i < 100; ++i) {
    r.clock.t += 5;
    r.hw.service();
    if (r.io.asserted(Signal::Heartbeat) != last) {
      ++edges;
      last = !last;
    }
  }
  CHECK(edges >= 9 && edges <= 11);  // 500 ms / 50 ms
  r.hw.arm(true);
  r.hw.press(Action::Up);
  CHECK(r.msg(R"({"t":"power","on":false})") == BenchLink::Result::PowerChanged);
  CHECK(!r.hw.selfTest());
  for (int i = 0; i < kFirstInput; ++i) CHECK(!r.io.asserted(static_cast<Signal>(i)));
  const int before = r.outMessages();
  for (int i = 0; i < 40; ++i) {
    r.clock.t += 5;
    r.hw.service();
  }
  CHECK_EQ(r.outMessages(), before);
}

TEST("esp32 adapter: tap releases after its time") {
  Rig r(InterfaceCase::A1);
  r.connect("A1");
  r.hw.tap(Action::Standing, 150);
  CHECK(r.io.asserted(Signal::InjStand));
  r.clock.t += 149;
  r.hw.service();
  CHECK(r.io.asserted(Signal::InjStand));
  r.clock.t += 1;
  r.hw.service();
  CHECK(!r.io.asserted(Signal::InjStand));
}

TEST("esp32 adapter B: frames out, repeats while held, keypad frames forwarded only when armed, height in") {
  Rig r(InterfaceCase::B);
  r.connect("B");
  auto txFrames = [&]() {
    std::vector<std::vector<uint8_t>> v;
    for (const auto& j : r.sent) {
      if (j["t"].asString() != "tx") continue;
      std::vector<uint8_t> b;
      for (const auto& x : j["bytes"].asArray()) b.push_back(static_cast<uint8_t>(x.asNumber()));
      v.push_back(b);
    }
    return v;
  };
  r.hw.press(Action::Up);
  CHECK(txFrames().size() == 1 && txFrames()[0] == frames::handset(frames::kCmdUp));
  for (int i = 0; i < 10; ++i) {
    r.clock.t += 10;
    r.hw.service();
  }
  CHECK(txFrames().size() == 3);  // 100 ms of hold at a 50 ms repeat
  r.hw.releaseAll();
  r.clock.t += 200;
  r.hw.service();
  CHECK(txFrames().size() == 3);
  // Keypad frame while not armed: sensed, not forwarded.
  Json rx = Json::object();
  rx.set("t", "rx");
  rx.set("line", "htx");
  Json arr = Json::array();
  for (uint8_t b : frames::handset(frames::kCmdDown)) arr.push(static_cast<unsigned>(b));
  rx.set("bytes", arr);
  r.link.onMessage(rx);
  r.hw.service();
  CHECK(r.hw.manualActive());
  CHECK(txFrames().size() == 3);
  r.hw.arm(true);
  r.link.onMessage(rx);
  r.hw.service();
  CHECK(txFrames().size() == 4 && txFrames()[3] == frames::handset(frames::kCmdDown));
  // Height report on DTX.
  Json h = Json::object();
  h.set("t", "rx");
  h.set("line", "dtx");
  Json hb = Json::array();
  for (uint8_t b : frames::heightReport(812)) hb.push(static_cast<unsigned>(b));
  h.set("bytes", hb);
  r.link.onMessage(h);
  r.hw.service();
  CHECK(r.hw.heightMm() == std::optional<uint16_t>(812));
  r.clock.t += 1500;
  CHECK(!r.hw.heightMm());
  // Stop method on UART is the stop frame.
  r.hw.stopMotion(Direction::Up);
  CHECK(txFrames().back() == frames::handset(frames::kCmdStop));
}

TEST("esp32 adapter: stop method on switch lines taps the opposite direction") {
  Rig r(InterfaceCase::A2);
  r.connect("A2");
  r.hw.stopMotion(Direction::Up);
  CHECK_EQ(r.hs(), keypad::encodedCode(Action::Down));
  r.clock.t += keypad::kStopTapMs;
  r.hw.service();
  CHECK_EQ(r.hs(), 0);
}

TEST("esp32 adapter + CommandService: a physical key releases the app command on the next tick") {
  Rig r(InterfaceCase::A1);
  r.connect("A1");
  Config cfg;
  cfg.boot_quiet_ms = 0;
  CommandService core(r.hw, r.clock, cfg);
  core.begin();
  for (int i = 0; i < 10; ++i) {
    r.clock.t += 5;
    r.hw.service();
    core.tick();
  }
  CHECK(core.status().state == DeskState::Idle);
  CHECK(core.submit({cfg.device_id, "desk.up", "mobile_app", "r1", 400, "c1"}).accepted());
  for (int i = 0; i < 20; ++i) {
    r.clock.t += 5;
    r.hw.service();
    core.tick();
  }
  CHECK(r.io.asserted(Signal::InjUp));
  r.msg(R"({"t":"in","s":{"sense_down":true}})");
  r.clock.t += 1;
  core.tick();
  CHECK(!r.io.asserted(Signal::InjUp));
  CHECK(core.status().state == DeskState::ManualActive);
  CHECK_EQ(r.gpio.drives, 0);
}

TEST("bench link: refuses messages before hello and a second link") {
  Rig r(InterfaceCase::A1);
  CHECK(r.link.open([](const std::string&) {}));
  CHECK(r.msg(R"({"t":"in","s":{"sense_up":true}})") == BenchLink::Result::Invalid);
  std::string why;
  CHECK(!r.link.open([](const std::string&) {}, &why));
  CHECK(r.msg(R"({"t":"hang","ms":99999})") == BenchLink::Result::Invalid);
  r.msg(R"({"t":"hello","case":"A1"})");
  CHECK(r.msg(R"({"t":"hang","ms":99999})") == BenchLink::Result::Hang);
  CHECK_EQ(r.link.hangMs(), 3000u);
  CHECK(r.link.state()["bench_mode"].asBool());
  CHECK(!r.link.state()["drive_enabled"].asBool());
  CHECK(!r.link.state()["pins_verified"].asBool());
  r.hw.press(Action::Up);
  CHECK(r.link.state()["outputs"]["inj_up"].asBool());
  CHECK(!r.link.state()["outputs"]["inj_down"].asBool());
  CHECK(!r.link.state()["outputs"].has("inj_hs0"));  // only the case's own signals
}
