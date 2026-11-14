#include "airo/command_service.h"
#include "airo/config.h"
#include "airo/types.h"

namespace airo {

const char* toString(Action a) {
  switch (a) {
    case Action::Up: return "up";
    case Action::Down: return "down";
    case Action::Sitting: return "sitting";
    case Action::Standing: return "standing";
    case Action::Reset: return "reset";
  }
  return "?";
}

const char* toString(Direction d) { return d == Direction::Up ? "up" : "down"; }

const char* toString(DeskState s) {
  switch (s) {
    case DeskState::SelfTest: return "SELF_TEST";
    case DeskState::Idle: return "IDLE";
    case DeskState::AppActive: return "APP_ACTIVE";
    case DeskState::ManualActive: return "MANUAL_ACTIVE";
    case DeskState::Bypass: return "BYPASS";
    case DeskState::Fault: return "FAULT";
  }
  return "?";
}

const char* toString(InterfaceCase c) {
  switch (c) {
    case InterfaceCase::A1: return "A1";
    case InterfaceCase::A2: return "A2";
    case InterfaceCase::B: return "B";
  }
  return "?";
}

std::optional<InterfaceCase> parseInterfaceCase(const std::string& s) {
  if (s == "A1") return InterfaceCase::A1;
  if (s == "A2") return InterfaceCase::A2;
  if (s == "B") return InterfaceCase::B;
  return std::nullopt;
}

const char* toString(PresetMode m) { return m == PresetMode::Tap ? "tap" : "hold_to_run"; }

const char* toString(ErrorCode e) {
  switch (e) {
    case ErrorCode::None: return "ok";
    case ErrorCode::BadRequest: return "bad_request";
    case ErrorCode::Unauthorized: return "unauthorized";
    case ErrorCode::CommandNotEnabled: return "command_not_enabled";
    case ErrorCode::WrongDevice: return "wrong_device";
    case ErrorCode::ManualActive: return "manual_active";
    case ErrorCode::BypassActive: return "bypass_active";
    case ErrorCode::Busy: return "busy";
    case ErrorCode::DirectionBlocked: return "direction_blocked";
    case ErrorCode::HeightFeedbackRequired: return "height_feedback_required";
    case ErrorCode::NotOwner: return "not_owner";
    case ErrorCode::NotIdle: return "not_idle";
    case ErrorCode::NotReady: return "not_ready";
    case ErrorCode::Fault: return "fault";
    case ErrorCode::RateLimited: return "rate_limited";
  }
  return "?";
}

int httpStatus(ErrorCode e) {
  switch (e) {
    case ErrorCode::None: return 202;
    case ErrorCode::BadRequest: return 400;
    case ErrorCode::Unauthorized: return 401;
    case ErrorCode::CommandNotEnabled: return 403;
    case ErrorCode::WrongDevice: return 404;
    case ErrorCode::ManualActive:
    case ErrorCode::BypassActive:
    case ErrorCode::Busy:
    case ErrorCode::DirectionBlocked:
    case ErrorCode::HeightFeedbackRequired:
    case ErrorCode::NotOwner:
    case ErrorCode::NotIdle: return 409;
    case ErrorCode::Fault: return 423;
    case ErrorCode::RateLimited: return 429;
    case ErrorCode::NotReady: return 503;
  }
  return 500;
}

const char* toString(CoreEventKind k) {
  switch (k) {
    case CoreEventKind::ManualOverride: return "manual_override";
    case CoreEventKind::LeaseExpired: return "lease_expired";
    case CoreEventKind::LimitReached: return "limit_reached";
    case CoreEventKind::MaxDuration: return "max_duration";
    case CoreEventKind::DeskError: return "desk_error";
    case CoreEventKind::OwnerGone: return "owner_disconnected";
    case CoreEventKind::Stopped: return "stopped";
    case CoreEventKind::StopMethodSent: return "stop_method_sent";
  }
  return "?";
}

namespace {
bool in(uint32_t v, uint32_t lo, uint32_t hi) { return v >= lo && v <= hi; }
}  // namespace

std::optional<std::string> validate(const Config& c) {
  if (c.device_id.empty()) return "device_id must not be empty";
  if (!in(c.manual_release_ms, 1, 20)) return "manual_release_ms must be 1..20";
  if (!in(c.manual_quiet_ms, 300, 10000)) return "manual_quiet_ms must be 300..10000";
  if (!in(c.pre_command_quiet_ms, 100, 10000)) return "pre_command_quiet_ms must be 100..10000";
  if (!in(c.lease_max_ms, 100, 1000)) return "lease_max_ms must be 100..1000";
  if (!in(c.lease_default_ms, 100, c.lease_max_ms)) return "lease_default_ms must be 100..lease_max_ms";
  if (!in(c.tap_ms, 50, 500)) return "tap_ms must be 50..500";
  if (!in(c.max_hold_ms, 1000, 60000)) return "max_hold_ms must be 1000..60000";
  if (!in(c.watchdog_period_ms, 10, 100)) return "watchdog_period_ms must be 10..100";
  if (!in(c.request_id_window, 1, 1024)) return "request_id_window must be 1..1024";
  if (!in(c.request_id_ttl_ms, 1000, 600000)) return "request_id_ttl_ms must be 1000..600000";
  if (!in(c.switch_settle_ms, 20, 500)) return "switch_settle_ms must be 20..500";
  if (!in(c.boot_quiet_ms, 500, 10000)) return "boot_quiet_ms must be 500..10000";
  if (!in(c.limit_block_ms, 0, 10000)) return "limit_block_ms must be 0..10000";
  if (!in(c.rate_limit_per_s, 1, 50)) return "rate_limit_per_s must be 1..50";
  if (!in(c.stall_ms, 100, 5000)) return "stall_ms must be 100..5000";
  if (!in(c.preset_motion_window_ms, 1000, 120000)) return "preset_motion_window_ms must be 1000..120000";
  return std::nullopt;
}

}  // namespace airo
