// Unit tests for the firmware command core against a recording fake adapter.
// Each block cites the spec rule it checks (docs/RETROFIT_SPEC.md).
#include "fakes.h"
#include "test.h"

using namespace airo;
using airo_test::Rig;

// --- Boot and self-test (7.1 brown-out row, 8.2) ---------------------------

TEST("boot: no driving until boot_quiet_ms of keypad idle") {
  Rig r;
  r.core.begin();
  CHECK(r.state() == DeskState::SelfTest);
  CHECK(r.send("desk.up").error == ErrorCode::NotReady);
  CHECK_EQ(r.hw.presses, 0);
  r.advance(1000);
  r.hw.manual = true;  // keypad in use during boot restarts the quiet time
  r.advance(100);
  r.hw.manual = false;
  r.advance(1950);
  CHECK(r.state() == DeskState::SelfTest);
  r.advance(100);
  CHECK(r.state() == DeskState::Idle);
}

TEST("boot: failed self-test enters FAULT; selftest clears it") {
  Rig r;
  r.hw.self_test_ok = false;
  r.core.begin();
  r.advance(3000);
  CHECK(r.state() == DeskState::Fault);
  CHECK(r.send("desk.up").error == ErrorCode::Fault);
  CHECK(r.send("desk.stop").accepted());
  CHECK(!r.core.runSelfTest());
  r.hw.self_test_ok = true;
  CHECK(r.core.runSelfTest());
  r.advance(2100);
  CHECK(r.state() == DeskState::Idle);
}

TEST("boot: reboot mid-move releases and does not resume") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up").accepted());
  CHECK(r.hw.asserted == Action::Up);
  r.core.begin();  // brown-out / power cycle
  CHECK(!r.hw.asserted.has_value());
  CHECK(!r.hw.armed);
  r.advance(5000);
  CHECK(!r.hw.asserted.has_value());
  CHECK(r.state() == DeskState::Idle);
}

// --- Hold-to-move leases (7.3) ---------------------------------------------

TEST("lease: desk.up asserts only while renewed") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up", "web_dashboard").accepted());
  CHECK(r.hw.armed);
  CHECK(r.hw.asserted == Action::Up);
  CHECK(r.state() == DeskState::AppActive);
  for (int i = 0; i < 13; ++i) {
    r.advance(150);
    CHECK(r.send("desk.renew", "web_dashboard").accepted());
    CHECK(r.hw.asserted == Action::Up);
  }
  CHECK(r.send("desk.release", "web_dashboard").accepted());
  CHECK(!r.hw.asserted.has_value());
  CHECK(!r.hw.armed);
  CHECK(r.state() == DeskState::Idle);
}

TEST("lease: missing renewals release at lease_ms (network drop)") {
  Rig r;
  r.boot();
  const uint64_t t0 = r.clock.t;
  CHECK(r.send("desk.down").accepted());
  r.advance(399);
  CHECK(r.hw.asserted == Action::Down);
  r.advance(2);
  CHECK(!r.hw.asserted.has_value());
  CHECK(r.sawEvent(CoreEventKind::LeaseExpired));
  CHECK(*r.hw.lastCallAt("release") - t0 <= 401);
  CHECK(r.state() == DeskState::Idle);
}

TEST("lease: lease_ms is capped at lease_max_ms") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up", "test", 60000).accepted());
  r.advance(1001);
  CHECK(!r.hw.asserted.has_value());
}

TEST("lease: no automatic restart after the owner reconnects") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up", "mobile_app", std::nullopt, {}, "ws:1").accepted());
  r.core.clientGone("ws:1");
  CHECK(!r.hw.asserted.has_value());
  r.advance(5000);
  CHECK(r.sawEvent(CoreEventKind::OwnerGone));  // the client reconnects as ws:2 and only reads status
  CHECK(r.send("desk.get_status", "mobile_app", std::nullopt, {}, "ws:2").accepted());
  r.advance(1000);
  CHECK(!r.hw.asserted.has_value());
  CHECK_EQ(r.hw.presses, 1);
}

TEST("lease: another client closing does not release the owner") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up", "mobile_app", 1000, {}, "ws:1").accepted());
  r.core.clientGone("ws:9");
  CHECK(r.hw.asserted == Action::Up);
}

TEST("max_hold_ms: a renewed command still ends at the cap") {
  Config c;
  c.max_hold_ms = 3000;
  Rig r(c);
  r.boot();
  CHECK(r.send("desk.up").accepted());
  for (int i = 0; i < 30; ++i) {
    r.advance(100);
    r.send("desk.renew");
  }
  CHECK(!r.hw.asserted.has_value());
  CHECK(r.sawEvent(CoreEventKind::MaxDuration));
}

// --- Arbitration (7.2) ------------------------------------------------------

TEST("one direction at a time: reversal releases, waits 50 ms, then applies") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up").accepted());
  const uint64_t t_switch = r.clock.t;
  CHECK(r.send("desk.down").accepted());
  CHECK(!r.hw.asserted.has_value());
  r.advance(49);
  CHECK(!r.hw.asserted.has_value());
  r.advance(2);
  CHECK(r.hw.asserted == Action::Down);
  CHECK(*r.hw.lastCallAt("press down") - t_switch >= 50);
  CHECK(!r.hw.both_directions_ever);
}

TEST("STOP: accepted from a non-owner and wins over movement") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up", "mobile_app").accepted());
  CHECK(r.send("desk.stop", "home_assistant").accepted());
  CHECK(!r.hw.asserted.has_value());
  CHECK(r.state() == DeskState::Idle);
  CHECK(r.sawEvent(CoreEventKind::Stopped));
}

TEST("STOP: cancels a pending (settling) movement") {
  Rig r;
  r.boot();
  r.send("desk.up");
  r.send("desk.down");
  r.send("desk.stop", "alexa");
  r.advance(200);
  CHECK(!r.hw.asserted.has_value());
}

TEST("busy: a second source cannot take over, but can stop") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up", "mobile_app", 1000).accepted());
  CHECK(r.send("desk.down", "web_dashboard").error == ErrorCode::Busy);
  CHECK(r.send("desk.renew", "web_dashboard").error == ErrorCode::NotOwner);
  CHECK(r.send("desk.release", "web_dashboard").error == ErrorCode::NotOwner);
  CHECK(r.hw.asserted == Action::Up);
  CHECK(r.send("desk.stop", "web_dashboard").accepted());
  CHECK(!r.hw.asserted.has_value());
}

TEST("dedupe: the same request_id executes once") {
  Rig r;
  r.boot();
  auto a = r.send("desk.up", "test", std::nullopt, "same-id");
  r.send("desk.release");
  auto b = r.send("desk.up", "test", std::nullopt, "same-id");
  CHECK(a.accepted() && !a.duplicate);
  CHECK(b.accepted() && b.duplicate);
  CHECK_EQ(r.hw.presses, 1);
  CHECK(!r.hw.asserted.has_value());
}

TEST("dedupe: repeated desk.up renews and never stacks") {
  Rig r;
  r.boot();
  for (int i = 0; i < 10; ++i) {
    CHECK(r.send("desk.up").accepted());
    r.advance(100);
  }
  CHECK_EQ(r.hw.presses, 1);
  CHECK(r.hw.asserted == Action::Up);
  r.advance(401);
  CHECK(!r.hw.asserted.has_value());
}

TEST("rate limit: >20 commands/s from one source; STOP still accepted") {
  Rig r;
  r.boot();
  int limited = 0;
  for (int i = 0; i < 25; ++i)
    if (r.send("desk.up").error == ErrorCode::RateLimited) ++limited;
  CHECK_EQ(limited, 5);
  CHECK(r.send("desk.stop").accepted());
  CHECK(r.send("desk.up", "mobile_app").accepted());  // per source
}

// --- Physical keypad wins (7.1, 7.2, test matrix "physical and app together")

TEST("manual: keypad press releases the app command on the next tick") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up", "mobile_app", 1000).accepted());
  const uint64_t t_press = r.clock.t;
  r.hw.manual = true;
  r.advance(1);
  CHECK(!r.hw.asserted.has_value());
  CHECK(!r.hw.armed);
  CHECK(*r.hw.lastCallAt("release") - t_press <= r.core.config().manual_release_ms);
  CHECK(r.sawEvent(CoreEventKind::ManualOverride));
  CHECK(r.state() == DeskState::ManualActive);
  CHECK(r.send("desk.up", "mobile_app").error == ErrorCode::ManualActive);
  CHECK(r.send("desk.stop", "mobile_app").accepted());
}

TEST("manual: commands rejected for manual_quiet_ms after the key is released") {
  Rig r;
  r.boot();
  r.hw.manual = true;
  r.advance(500);
  r.hw.manual = false;
  r.advance(200);  // inside pre_command_quiet_ms (300)
  CHECK(r.send("desk.up").error == ErrorCode::ManualActive);
  r.advance(700);  // 900 ms after release, still inside manual_quiet_ms
  CHECK(r.send("desk.up").error == ErrorCode::ManualActive);
  CHECK(r.state() == DeskState::ManualActive);
  r.advance(150);
  CHECK(r.state() == DeskState::Idle);
  CHECK(r.send("desk.up").accepted());
}

TEST("manual: keypad during a preset tap cancels it and clears preset tracking") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.standing").accepted());
  r.advance(50);
  r.hw.manual = true;
  r.advance(1);
  CHECK(!r.hw.armed);
  r.hw.manual = false;
  r.advance(1100);
  CHECK(r.send("desk.stop").accepted());
  CHECK_EQ(r.hw.stops, 0);  // the keypad already owns the desk; no stop tap
}

// --- MANUAL ONLY bypass (6.1 R6, 7.1) --------------------------------------

TEST("bypass: rejects movement, releases, STOP still accepted") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up").accepted());
  r.hw.bypass = true;
  r.advance(1);
  CHECK(r.state() == DeskState::Bypass);
  CHECK(!r.hw.asserted.has_value());
  CHECK(!r.hw.armed);
  for (const char* c : {"desk.up", "desk.down", "desk.sitting", "desk.standing"})
    CHECK(r.send(c, "mobile_app").error == ErrorCode::BypassActive);
  CHECK(r.send("desk.stop").accepted());
  CHECK(r.core.status().bypass);
  r.hw.bypass = false;
  r.advance(1);
  CHECK(r.state() == DeskState::Idle);
  CHECK(r.send("desk.up", "mobile_app").accepted());
}

// --- Limits (7.1 controller limit row) --------------------------------------

TEST("limit: releases, blocks that direction, a reversal clears the block") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.up", "test", 1000).accepted());
  r.hw.events.push_back({DeskEventKind::LimitReached, Direction::Up, {}});
  r.advance(1);
  CHECK(!r.hw.asserted.has_value());
  CHECK(r.sawEvent(CoreEventKind::LimitReached));
  CHECK(r.send("desk.up").error == ErrorCode::DirectionBlocked);
  CHECK(r.core.status().blocked_direction == Direction::Up);
  CHECK(r.send("desk.down").accepted());
  CHECK(!r.core.status().blocked_direction.has_value());
}

TEST("limit: the block lapses after limit_block_ms") {
  Rig r;
  r.boot();
  r.hw.events.push_back({DeskEventKind::LimitReached, Direction::Down, {}});
  r.advance(1);
  CHECK(r.send("desk.down").error == ErrorCode::DirectionBlocked);
  r.advance(2001);
  CHECK(r.send("desk.down").accepted());
}

TEST("limit: with height feedback, no progress while moving counts as a limit") {
  Rig r;
  r.boot();
  r.hw.height = 1000;
  CHECK(r.send("desk.up", "test", 1000).accepted());
  for (int i = 0; i < 10; ++i) {  // moving: height changes
    r.advance(50);
    r.hw.height = static_cast<uint16_t>(*r.hw.height + 2);
  }
  CHECK(r.hw.asserted == Action::Up);
  r.advance(399);  // stalled at the top
  CHECK(r.hw.asserted == Action::Up);
  r.advance(2);
  CHECK(!r.hw.asserted.has_value());
  CHECK(r.sawEvent(CoreEventKind::LimitReached));
  CHECK(r.send("desk.up").error == ErrorCode::DirectionBlocked);
}

TEST("controller error: releases and reports desk_error") {
  Rig r;
  r.boot();
  r.send("desk.down");
  r.hw.events.push_back({DeskEventKind::ControllerError, std::nullopt, "E01"});
  r.advance(1);
  CHECK(!r.hw.asserted.has_value());
  CHECK(r.sawEvent(CoreEventKind::DeskError));
}

// --- Presets (C4 / Q14) ----------------------------------------------------

TEST("presets: controller-native presets allowed without height feedback (default)") {
  Rig r;
  r.boot();
  CHECK(!r.core.config().presets_require_height_feedback);
  CHECK(r.send("desk.sitting").accepted());
  CHECK_EQ(r.hw.taps, 1);
  CHECK(r.state() == DeskState::AppActive);
  r.advance(151);
  CHECK(r.state() == DeskState::Idle);
  CHECK(!r.hw.armed);
  CHECK(r.send("desk.standing").accepted());
  CHECK_EQ(r.hw.taps, 2);
}

TEST("presets: presets_require_height_feedback=true refuses without feedback") {
  Config c;
  c.presets_require_height_feedback = true;
  Rig r(c);
  r.boot();
  CHECK(r.send("desk.sitting").error == ErrorCode::HeightFeedbackRequired);
  CHECK(r.send("desk.standing").error == ErrorCode::HeightFeedbackRequired);
  CHECK(r.send("desk.up").accepted());  // hold-to-move does not need feedback
  r.send("desk.release");
  r.hw.height = 900;
  CHECK(r.send("desk.sitting").accepted());
}

TEST("presets: STOP after a tapped preset runs the stop method once") {
  Rig r;
  r.boot();
  r.send("desk.standing");
  r.advance(500);  // tap over, controller still travelling
  CHECK(r.send("desk.stop", "alexa").accepted());
  CHECK_EQ(r.hw.stops, 1);
  CHECK(r.sawEvent(CoreEventKind::StopMethodSent));
  r.advance(200);
  CHECK(!r.hw.armed);
  CHECK(r.send("desk.stop").accepted());
  CHECK_EQ(r.hw.stops, 1);  // nothing moving: STOP must not tap anything
}

TEST("presets: STOP when idle never taps a key") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.stop").accepted());
  CHECK_EQ(r.hw.stops, 0);
  CHECK_EQ(r.hw.taps, 0);
}

TEST("presets: hold_to_run mode uses a lease") {
  Config c;
  c.preset_mode = PresetMode::HoldToRun;
  Rig r(c);
  r.boot();
  CHECK(r.send("desk.sitting").accepted());
  CHECK(r.hw.asserted == Action::Sitting);
  CHECK_EQ(r.hw.taps, 0);
  r.advance(401);
  CHECK(!r.hw.asserted.has_value());
}

// --- Envelope validation (10.1, 10.6, 10.7) ---------------------------------

TEST("envelope: reset, unknown command, wrong device, unknown source") {
  Rig r;
  r.boot();
  CHECK(r.send("desk.reset").error == ErrorCode::CommandNotEnabled);
  CHECK(r.send("move").error == ErrorCode::BadRequest);
  CHECK(r.send("desk.up", "toaster").error == ErrorCode::BadRequest);
  CommandEnvelope e;
  e.device_id = "airo-desk-999";
  e.command = "desk.up";
  e.source = "test";
  e.request_id = "x";
  CHECK(r.core.submit(e).error == ErrorCode::WrongDevice);
  CHECK_EQ(r.hw.presses, 0);
}

TEST("config: changes only while IDLE and within bounds") {
  Rig r;
  r.boot();
  Config c = r.core.config();
  c.lease_max_ms = 5000;
  CHECK(r.core.setConfig(c) == ErrorCode::BadRequest);
  c = r.core.config();
  c.presets_require_height_feedback = true;
  r.send("desk.up");
  CHECK(r.core.setConfig(c) == ErrorCode::NotIdle);
  r.send("desk.release");
  CHECK(r.core.setConfig(c) == ErrorCode::None);
  CHECK(r.core.config().presets_require_height_feedback);
}

TEST("status: reports owner, lease and armed state") {
  Rig r;
  r.boot();
  r.send("desk.up", "mobile_app", 400);
  r.advance(100);
  Status s = r.core.status();
  CHECK(s.state == DeskState::AppActive);
  CHECK(s.command == std::string("desk.up"));
  CHECK(s.owner == std::string("mobile_app"));
  CHECK(s.lease_remaining_ms <= 300 && s.lease_remaining_ms >= 290);
  CHECK(s.armed);
  CHECK(!s.height_mm.has_value());
}
