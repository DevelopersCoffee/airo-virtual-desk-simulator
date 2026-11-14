// Timing constants and policy flags (spec section 8.3).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace airo {

/// How the controller runs its sit/stand presets (open question Q5).
enum class PresetMode : uint8_t {
  Tap,        ///< one-touch: Airo sends a tap of tap_ms (or one frame)
  HoldToRun,  ///< presets need a lease like UP/DOWN
};

struct Config {
  std::string device_id = "airo-desk-001";

  uint32_t manual_release_ms = 20;      ///< max time to release after a manual press
  uint32_t manual_quiet_ms = 1000;      ///< keypad idle before commands are accepted again
  uint32_t pre_command_quiet_ms = 300;  ///< keypad idle required before starting
  uint32_t lease_default_ms = 400;
  uint32_t lease_max_ms = 1000;
  uint32_t tap_ms = 150;
  uint32_t max_hold_ms = 30000;
  uint32_t watchdog_period_ms = 50;
  uint32_t request_id_window = 64;
  uint32_t request_id_ttl_ms = 60000;
  uint32_t switch_settle_ms = 50;        ///< release, wait, then apply a new movement
  uint32_t boot_quiet_ms = 2000;         ///< no driving until keypad idle this long after boot
  uint32_t limit_block_ms = 2000;        ///< block a direction after its limit
  uint32_t rate_limit_per_s = 20;        ///< movement commands per source per second
  uint32_t stall_ms = 400;               ///< height unchanged this long while moving = limit
  uint32_t preset_motion_window_ms = 30000;  ///< how long a tapped preset may still be moving

  PresetMode preset_mode = PresetMode::Tap;

  /// Open question Q14 / conflict C4. false (default, the spec's
  /// recommendation): controller-native presets (desk.sitting / desk.standing)
  /// are allowed without height feedback, because the controller already runs
  /// them from the keypad. true: presets are refused with
  /// `height_feedback_required` unless the interface reports height.
  bool presets_require_height_feedback = false;

  /// After STOP, if a tapped preset may still be moving, run the adapter's
  /// stop method. false = STOP only releases Airo's own lines.
  bool stop_preset_with_stop_method = true;
};

/// Returns an error message if any value is outside its safe bound.
std::optional<std::string> validate(const Config& c);

const char* toString(PresetMode m);

}  // namespace airo
