#include "airo/api.h"

#include <cmath>

namespace airo::api {

std::optional<CommandEnvelope> parseEnvelope(const Json& j, std::string* why) {
  auto fail = [&](const char* m) -> std::optional<CommandEnvelope> {
    if (why) *why = m;
    return std::nullopt;
  };
  if (!j.isObject()) return fail("envelope must be a JSON object");
  for (const char* k : {"device_id", "command", "source", "request_id"})
    if (!j[k].isString() || j[k].asString().empty()) return fail("device_id, command, source and request_id are required strings");
  CommandEnvelope e;
  e.device_id = j["device_id"].asString();
  e.command = j["command"].asString();
  e.source = j["source"].asString();
  e.request_id = j["request_id"].asString();
  if (e.request_id.size() > 128) return fail("request_id too long");
  if (!isKnownSource(e.source)) return fail("unknown source");
  if (j.has("lease_ms") && !j["lease_ms"].isNull()) {
    const double v = j["lease_ms"].asNumber(-1);
    if (!j["lease_ms"].isNumber() || v < 0 || v > 60000 || std::floor(v) != v) return fail("lease_ms must be a non-negative integer");
    e.lease_ms = static_cast<uint32_t>(v);
  }
  return e;
}

Json statusJson(const Status& s) {
  Json j = Json::object();
  j.set("type", "status");
  j.set("state", toString(s.state));
  j.set("command", s.command);
  j.set("owner", s.owner);
  j.set("lease_remaining_ms", s.lease_remaining_ms);
  j.set("height_mm", s.height_mm ? Json(static_cast<unsigned>(*s.height_mm)) : Json());
  j.set("height_age_ms", s.height_age_ms);
  j.set("bypass", s.bypass);
  j.set("last_manual_ms", s.last_manual_ms);
  j.set("fault", s.fault);
  j.set("blocked_direction", s.blocked_direction ? Json(toString(*s.blocked_direction)) : Json());
  j.set("armed", s.armed);
  return j;
}

Json eventJson(const CoreEvent& e) {
  Json j = Json::object();
  j.set("type", "event");
  j.set("event", toString(e.kind));
  if (e.direction) j.set("direction", toString(*e.direction));
  if (!e.detail.empty()) j.set("detail", e.detail);
  return j;
}

Json ackJson(const std::string& request_id, const CommandResult& r) {
  if (!r.accepted()) return errorJson(request_id, r.error);
  Json j = Json::object();
  j.set("type", "ack");
  j.set("request_id", request_id);
  j.set("result", r.duplicate ? "duplicate" : "accepted");
  return j;
}

Json errorJson(const std::string& request_id, ErrorCode code, const std::string& detail) {
  Json j = Json::object();
  j.set("type", "error");
  j.set("request_id", request_id.empty() ? Json() : Json(request_id));
  j.set("code", toString(code));
  if (!detail.empty()) j.set("detail", detail);
  return j;
}

Json configJson(const Config& c) {
  Json j = Json::object();
  j.set("device_id", c.device_id);
  j.set("manual_release_ms", c.manual_release_ms);
  j.set("manual_quiet_ms", c.manual_quiet_ms);
  j.set("pre_command_quiet_ms", c.pre_command_quiet_ms);
  j.set("lease_default_ms", c.lease_default_ms);
  j.set("lease_max_ms", c.lease_max_ms);
  j.set("tap_ms", c.tap_ms);
  j.set("max_hold_ms", c.max_hold_ms);
  j.set("watchdog_period_ms", c.watchdog_period_ms);
  j.set("request_id_window", c.request_id_window);
  j.set("switch_settle_ms", c.switch_settle_ms);
  j.set("boot_quiet_ms", c.boot_quiet_ms);
  j.set("limit_block_ms", c.limit_block_ms);
  j.set("rate_limit_per_s", c.rate_limit_per_s);
  j.set("stall_ms", c.stall_ms);
  j.set("preset_mode", toString(c.preset_mode));
  j.set("presets_require_height_feedback", c.presets_require_height_feedback);
  j.set("stop_preset_with_stop_method", c.stop_preset_with_stop_method);
  return j;
}

std::optional<Config> patchConfig(const Config& base, const Json& patch, std::string* why) {
  auto fail = [&](const std::string& m) -> std::optional<Config> {
    if (why) *why = m;
    return std::nullopt;
  };
  if (!patch.isObject()) return fail("config patch must be an object");
  Config c = base;
  struct U32 {
    const char* name;
    uint32_t Config::*field;
  };
  static const U32 kFields[] = {
      {"manual_release_ms", &Config::manual_release_ms}, {"manual_quiet_ms", &Config::manual_quiet_ms},
      {"pre_command_quiet_ms", &Config::pre_command_quiet_ms}, {"lease_default_ms", &Config::lease_default_ms},
      {"lease_max_ms", &Config::lease_max_ms}, {"tap_ms", &Config::tap_ms},
      {"max_hold_ms", &Config::max_hold_ms}, {"switch_settle_ms", &Config::switch_settle_ms},
      {"boot_quiet_ms", &Config::boot_quiet_ms}, {"limit_block_ms", &Config::limit_block_ms},
      {"rate_limit_per_s", &Config::rate_limit_per_s}, {"stall_ms", &Config::stall_ms},
  };
  for (const auto& [key, value] : patch.asObject()) {
    bool done = false;
    for (const U32& f : kFields) {
      if (key != f.name) continue;
      const double v = value.asNumber(-1);
      if (!value.isNumber() || v < 0 || std::floor(v) != v || v > 4e9) return fail(key + " must be a non-negative integer");
      c.*(f.field) = static_cast<uint32_t>(v);
      done = true;
    }
    if (done) continue;
    if (key == "preset_mode") {
      if (value.asString() == "tap") c.preset_mode = PresetMode::Tap;
      else if (value.asString() == "hold_to_run") c.preset_mode = PresetMode::HoldToRun;
      else return fail("preset_mode must be tap or hold_to_run");
    } else if (key == "presets_require_height_feedback") {
      if (!value.isBool()) return fail(key + " must be a boolean");
      c.presets_require_height_feedback = value.asBool();
    } else if (key == "stop_preset_with_stop_method") {
      if (!value.isBool()) return fail(key + " must be a boolean");
      c.stop_preset_with_stop_method = value.asBool();
    } else {
      return fail("unknown or read-only config field: " + key);
    }
  }
  if (auto err = validate(c)) return fail(*err);
  return c;
}

Json infoJson(const Config& c, const InfoContext& ctx) {
  Json caps = Json::object();
  Json cmds = Json::array();
  for (const char* k : {"desk.up", "desk.down", "desk.stop", "desk.sitting", "desk.standing", "desk.get_status"})
    cmds.push(k);
  caps.set("commands", cmds);
  caps.set("height_feedback", ctx.height_feedback);
  caps.set("stop_method_verified", ctx.stop_method_verified);
  caps.set("preset_mode", toString(c.preset_mode));
  caps.set("presets_require_height_feedback", c.presets_require_height_feedback);
  caps.set("reset_exposed", false);

  Json j = Json::object();
  j.set("device_id", c.device_id);
  j.set("fw", kFirmwareVersion);
  j.set("api", kApiVersion);
  j.set("hardware", ctx.hardware);
  j.set("adapter", ctx.adapter);
  j.set("interface_case", ctx.interface_case ? Json(toString(*ctx.interface_case)) : Json());
  j.set("board_connected", ctx.board_connected);
  j.set("capabilities", caps);
  return j;
}

}  // namespace airo::api
