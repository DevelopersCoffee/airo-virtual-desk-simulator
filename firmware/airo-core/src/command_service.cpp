#include "airo/command_service.h"

#include <algorithm>

namespace airo {

namespace {

std::optional<Action> parseMovement(const std::string& c) {
  if (c == "desk.up") return Action::Up;
  if (c == "desk.down") return Action::Down;
  if (c == "desk.sitting") return Action::Sitting;
  if (c == "desk.standing") return Action::Standing;
  return std::nullopt;
}

bool isPreset(Action a) { return a == Action::Sitting || a == Action::Standing; }

}  // namespace

bool isKnownSource(const std::string& s) {
  static const char* const kSources[] = {"mobile_app", "web_dashboard", "home_assistant", "alexa",
                                         "mqtt",       "simulator",     "test"};
  for (const char* k : kSources)
    if (s == k) return true;
  return false;
}

CommandService::CommandService(HardwareAdapter& hw, Clock& clock, Config cfg)
    : hw_(hw), clock_(clock), cfg_(std::move(cfg)) {}

void CommandService::begin() {
  const uint64_t now = clock_.nowMs();
  hw_.releaseAll();
  hw_.arm(false);
  armed_ = false;
  active_.reset();
  preset_maybe_moving_ = false;
  blocked_dir_.reset();
  disarm_at_ = 0;
  last_manual_at_.reset();
  manual_now_ = false;
  bypass_now_ = false;
  last_height_.reset();
  height_trend_.reset();
  seen_.clear();
  rate_.clear();
  boot_at_ = now;
  fault_.reset();
  state_ = DeskState::SelfTest;
  if (!hw_.selfTest()) {
    state_ = DeskState::Fault;
    fault_ = "self_test_failed";
  }
  bumpIfChanged();
}

bool CommandService::runSelfTest() {
  const uint64_t now = clock_.nowMs();
  abort();
  if (hw_.selfTest()) {
    if (state_ == DeskState::Fault) {
      state_ = DeskState::SelfTest;
      boot_at_ = now;
      fault_.reset();
    }
    bumpIfChanged();
    return true;
  }
  state_ = DeskState::Fault;
  fault_ = "self_test_failed";
  bumpIfChanged();
  return false;
}

CommandResult CommandService::submit(const CommandEnvelope& env) {
  const uint64_t now = clock_.nowMs();
  if (env.command.empty() || env.request_id.empty() || !isKnownSource(env.source))
    return {ErrorCode::BadRequest};
  if (env.device_id != cfg_.device_id) return {ErrorCode::WrongDevice};

  while (!seen_.empty() && now - seen_.front().at > cfg_.request_id_ttl_ms) seen_.pop_front();
  for (const Seen& s : seen_) {
    if (s.id == env.request_id) {
      CommandResult r = s.result;
      r.duplicate = true;
      return r;  // acknowledged, never re-executed
    }
  }

  const std::string client = env.client_id.empty() ? env.source : env.client_id;
  CommandResult r = dispatch(env, client, now);
  seen_.push_back({env.request_id, now, r});
  while (seen_.size() > cfg_.request_id_window) seen_.pop_front();
  bumpIfChanged();
  return r;
}

CommandResult CommandService::dispatch(const CommandEnvelope& env, const std::string& client, uint64_t now) {
  const std::string& c = env.command;
  if (c == "desk.get_status") return {};
  if (c == "desk.stop") {  // always accepted, from any authenticated source
    stop(now);
    return {};
  }
  if (c == "desk.reset") return {ErrorCode::CommandNotEnabled};

  const bool known = c == "desk.release" || c == "desk.renew" || parseMovement(c).has_value();
  if (!known) return {ErrorCode::BadRequest};
  if (rateLimited(env.source, now)) return {ErrorCode::RateLimited};

  if (c == "desk.release") {
    if (!active_) return {};
    if (active_->owner_client != client) return {ErrorCode::NotOwner};
    const auto d = directionOf(active_->action);
    abort();
    emit(CoreEventKind::Stopped, d, "released");
    refreshState(now);
    return {};
  }
  if (c == "desk.renew") {
    if (!active_ || !active_->held || active_->owner_client != client) return {ErrorCode::NotOwner};
    active_->lease_deadline = std::max(active_->lease_deadline, now + clampLease(env.lease_ms));
    return {};
  }
  return startMovement(env, *parseMovement(c), client, now);
}

CommandResult CommandService::startMovement(const CommandEnvelope& env, Action a, const std::string& client,
                                            uint64_t now) {
  switch (state_) {
    case DeskState::Fault:
      return {ErrorCode::Fault};
    case DeskState::SelfTest:
      return {ErrorCode::NotReady};
    case DeskState::Bypass:
      return {ErrorCode::BypassActive};
    default:
      break;
  }
  if (manual_now_ || !manualQuiet(now)) return {ErrorCode::ManualActive};

  const bool preset = isPreset(a);
  if (preset && cfg_.presets_require_height_feedback && !hw_.heightMm())
    return {ErrorCode::HeightFeedbackRequired};

  const auto dir = directionOf(a);
  if (dir && blocked_dir_ && *blocked_dir_ == *dir && now < blocked_until_) return {ErrorCode::DirectionBlocked};

  const bool held = !preset || cfg_.preset_mode == PresetMode::HoldToRun;
  if (active_) {
    if (active_->owner_client != client) return {ErrorCode::Busy};
    if (active_->action == a) {
      // A repeated command renews; it never stacks a second movement.
      if (active_->held) active_->lease_deadline = std::max(active_->lease_deadline, now + clampLease(env.lease_ms));
      return {};
    }
  }

  if (dir && blocked_dir_ && *blocked_dir_ != *dir) blocked_dir_.reset();  // reversal clears the block

  Active next;
  next.action = a;
  next.command = env.command;
  next.owner_client = client;
  next.owner_source = env.source;
  next.held = held;
  next.started_at = now;
  if (active_) {
    // Only one direction at a time: release the old one, wait, then apply.
    hw_.releaseAll();
    next.pending = true;
    next.apply_at = now + cfg_.switch_settle_ms;
  }
  if (held) next.lease_deadline = (next.pending ? next.apply_at : now) + clampLease(env.lease_ms);
  // A new press stops a tapped preset on the controller by itself.
  preset_maybe_moving_ = false;
  active_ = next;
  if (!active_->pending) apply(*active_, now);
  refreshState(now);
  return {};
}

void CommandService::apply(Active& a, uint64_t now) {
  a.pending = false;
  a.started_at = now;
  disarm_at_ = 0;
  if (!armed_) {
    hw_.arm(true);
    armed_ = true;
  }
  if (a.held) {
    hw_.press(a.action);
  } else {
    hw_.tap(a.action, static_cast<uint16_t>(cfg_.tap_ms));
    a.tap_ends_at = now + cfg_.tap_ms;
  }
  last_height_change_at_ = now;  // baseline for stall detection
}

void CommandService::abort() {
  hw_.releaseAll();
  if (armed_) {
    hw_.arm(false);
    armed_ = false;
  }
  active_.reset();
  disarm_at_ = 0;
}

void CommandService::stop(uint64_t now) {
  const bool had_active = active_.has_value();
  const auto travel = had_active ? directionOf(active_->action) : std::nullopt;
  if (had_active && !active_->held && isPreset(active_->action)) {
    preset_maybe_moving_ = true;  // the controller already saw the tap
    preset_started_at_ = now;
  }
  abort();
  if (preset_maybe_moving_ && cfg_.stop_preset_with_stop_method && !manual_now_ &&
      (state_ == DeskState::Idle || state_ == DeskState::AppActive)) {
    hw_.arm(true);
    armed_ = true;
    hw_.stopMotion(height_trend_);
    disarm_at_ = now + cfg_.tap_ms + 20;
    emit(CoreEventKind::StopMethodSent, height_trend_);
  }
  preset_maybe_moving_ = false;
  emit(CoreEventKind::Stopped, travel, had_active ? "app_command_released" : "nothing_active");
  refreshState(now);
}

void CommandService::tick() {
  const uint64_t now = clock_.nowMs();

  // 1. Hardware bypass (MANUAL ONLY) beats everything.
  const bool bypass = hw_.bypassActive();
  bypass_now_ = bypass;
  if (bypass && state_ != DeskState::Bypass && state_ != DeskState::Fault) {
    if (active_) emit(CoreEventKind::Stopped, directionOf(active_->action), "bypass");
    abort();
    preset_maybe_moving_ = false;
    state_ = DeskState::Bypass;
  } else if (!bypass && state_ == DeskState::Bypass) {
    state_ = DeskState::Idle;
  }

  // 2. Physical keypad, then adapter events.
  bool manual = hw_.manualActive();
  while (auto e = hw_.pollEvent()) {
    switch (e->kind) {
      case DeskEventKind::ManualPress:
        manual = true;
        break;
      case DeskEventKind::LimitReached:
        if (e->direction) handleLimit(*e->direction, now);
        break;
      case DeskEventKind::ControllerError:
        if (active_) abort();
        preset_maybe_moving_ = false;
        emit(CoreEventKind::DeskError, e->direction, e->detail);
        break;
    }
  }
  manual_now_ = manual;
  if (manual) {
    last_manual_at_ = now;
    preset_maybe_moving_ = false;
    const bool had = active_.has_value();
    if (had || armed_) abort();
    if (had) emit(CoreEventKind::ManualOverride);
    if (state_ == DeskState::Idle || state_ == DeskState::AppActive) state_ = DeskState::ManualActive;
    if (state_ == DeskState::SelfTest) boot_at_ = now;  // boot needs an idle keypad
  }

  // 3. Height feedback: trend, and "no progress" = limit.
  const auto h = hw_.heightMm();
  if (h != last_height_) {
    if (h && last_height_ && *h != *last_height_)
      height_trend_ = *h > *last_height_ ? Direction::Up : Direction::Down;
    last_height_ = h;
    last_height_change_at_ = now;
  }
  if (active_ && !active_->pending && active_->held && h) {
    const auto d = directionOf(active_->action);
    if (d && now - last_height_change_at_ >= cfg_.stall_ms) handleLimit(*d, now);
  }
  if (preset_maybe_moving_ && h && now - preset_started_at_ >= cfg_.stall_ms &&
      now - last_height_change_at_ >= cfg_.stall_ms)
    preset_maybe_moving_ = false;  // feedback says the preset finished

  // 4. Timers on the active command.
  if (active_) {
    if (active_->pending) {
      if (now >= active_->apply_at) apply(*active_, now);
    } else if (active_->held && now >= active_->lease_deadline) {
      const auto d = directionOf(active_->action);
      abort();
      emit(CoreEventKind::LeaseExpired, d);
    } else if (now - active_->started_at >= cfg_.max_hold_ms) {
      const auto d = directionOf(active_->action);
      abort();
      emit(CoreEventKind::MaxDuration, d);
    } else if (!active_->held && now >= active_->tap_ends_at) {
      const bool preset = isPreset(active_->action);
      abort();
      if (preset) {
        preset_maybe_moving_ = true;
        preset_started_at_ = now;
      }
    }
  }
  if (!active_ && armed_ && disarm_at_ != 0 && now >= disarm_at_) {
    hw_.releaseAll();
    hw_.arm(false);
    armed_ = false;
    disarm_at_ = 0;
  }
  if (preset_maybe_moving_ && now - preset_started_at_ >= cfg_.preset_motion_window_ms) preset_maybe_moving_ = false;

  refreshState(now);
  bumpIfChanged();
}

void CommandService::handleLimit(Direction d, uint64_t now) {
  if (active_ && directionOf(active_->action) == d) abort();
  blocked_dir_ = d;
  blocked_until_ = now + cfg_.limit_block_ms;
  emit(CoreEventKind::LimitReached, d);
}

void CommandService::clientGone(const std::string& client_id) {
  if (active_ && active_->owner_client == client_id) {
    const auto d = directionOf(active_->action);
    abort();
    emit(CoreEventKind::OwnerGone, d);
    refreshState(clock_.nowMs());
    bumpIfChanged();
  }
}

void CommandService::refreshState(uint64_t now) {
  if (state_ == DeskState::Fault || state_ == DeskState::Bypass) return;
  if (state_ == DeskState::SelfTest) {
    if (manual_now_ || now - boot_at_ < cfg_.boot_quiet_ms) return;
    state_ = DeskState::Idle;
  }
  if (state_ == DeskState::ManualActive) {
    if (manual_now_ || !manualQuiet(now)) return;
    state_ = DeskState::Idle;
  }
  state_ = active_ ? DeskState::AppActive : DeskState::Idle;
}

bool CommandService::manualQuiet(uint64_t now) const {
  if (!last_manual_at_) return true;
  return now - *last_manual_at_ >= std::max(cfg_.manual_quiet_ms, cfg_.pre_command_quiet_ms);
}

bool CommandService::rateLimited(const std::string& source, uint64_t now) {
  auto& q = rate_[source];
  while (!q.empty() && now - q.front() >= 1000) q.pop_front();
  if (q.size() >= cfg_.rate_limit_per_s) return true;
  q.push_back(now);
  return false;
}

uint32_t CommandService::clampLease(std::optional<uint32_t> ms) const {
  if (!ms || *ms == 0) return cfg_.lease_default_ms;
  return std::min(*ms, cfg_.lease_max_ms);
}

void CommandService::emit(CoreEventKind k, std::optional<Direction> d, std::string detail) {
  events_.push_back({k, d, std::move(detail)});
  ++change_counter_;
}

std::vector<CoreEvent> CommandService::takeEvents() {
  std::vector<CoreEvent> out;
  out.swap(events_);
  return out;
}

Status CommandService::status() {
  const uint64_t now = clock_.nowMs();
  Status s;
  s.state = state_;
  if (active_) {
    s.command = active_->command;
    s.owner = active_->owner_source;
    if (active_->held && !active_->pending && active_->lease_deadline > now)
      s.lease_remaining_ms = static_cast<uint32_t>(active_->lease_deadline - now);
    else if (active_->held && active_->pending)
      s.lease_remaining_ms = static_cast<uint32_t>(active_->lease_deadline - now);
  }
  s.height_mm = hw_.heightMm();
  s.height_age_ms = s.height_mm ? hw_.heightAgeMs() : std::nullopt;
  s.bypass = bypass_now_;
  if (last_manual_at_) s.last_manual_ms = static_cast<uint32_t>(now - *last_manual_at_);
  s.fault = fault_;
  if (blocked_dir_ && now < blocked_until_) s.blocked_direction = blocked_dir_;
  s.armed = armed_;
  return s;
}

void CommandService::bumpIfChanged() {
  const Status s = status();
  std::string key;
  key += toString(s.state);
  key += '|';
  key += s.command.value_or("-");
  key += '|';
  key += s.owner.value_or("-");
  key += s.bypass ? "|B" : "|-";
  key += s.armed ? "|A" : "|-";
  key += '|' + (s.height_mm ? std::to_string(*s.height_mm) : std::string("-"));
  key += '|' + std::string(s.blocked_direction ? toString(*s.blocked_direction) : "-");
  key += '|' + s.fault.value_or("-");
  if (key != last_key_) {
    last_key_ = key;
    ++change_counter_;
  }
}

ErrorCode CommandService::setConfig(const Config& cfg, std::string* why) {
  if (state_ != DeskState::Idle) {
    if (why) *why = "config can only change while IDLE";
    return ErrorCode::NotIdle;
  }
  if (auto err = validate(cfg)) {
    if (why) *why = *err;
    return ErrorCode::BadRequest;
  }
  const std::string id = cfg_.device_id;
  cfg_ = cfg;
  cfg_.device_id = id;  // identity is not runtime-configurable
  bumpIfChanged();
  return ErrorCode::None;
}

}  // namespace airo
