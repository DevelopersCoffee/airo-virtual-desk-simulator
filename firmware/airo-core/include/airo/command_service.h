// CommandService: the single implementation of Airo's command logic
// (spec sections 7.2, 8.2, 10). Every control path (REST, WebSocket, MQTT,
// Home Assistant) builds a CommandEnvelope and calls submit(). The service
// never touches GPIO; it drives a HardwareAdapter.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "airo/config.h"
#include "airo/hardware_adapter.h"
#include "airo/types.h"

namespace airo {

enum class ErrorCode : uint8_t {
  None,
  BadRequest,
  Unauthorized,
  CommandNotEnabled,
  WrongDevice,
  ManualActive,
  BypassActive,
  Busy,
  DirectionBlocked,
  HeightFeedbackRequired,
  NotOwner,
  NotIdle,
  NotReady,
  Fault,
  RateLimited,
};

const char* toString(ErrorCode e);
int httpStatus(ErrorCode e);

struct CommandEnvelope {
  std::string device_id;
  std::string command;
  std::string source;
  std::string request_id;
  std::optional<uint32_t> lease_ms;
  /// Set by the transport: identifies the connection that owns a lease.
  /// Empty means "use source".
  std::string client_id;
};

struct CommandResult {
  ErrorCode error = ErrorCode::None;
  bool duplicate = false;
  bool accepted() const { return error == ErrorCode::None; }
};

struct Status {
  DeskState state = DeskState::SelfTest;
  std::optional<std::string> command;
  std::optional<std::string> owner;
  uint32_t lease_remaining_ms = 0;
  std::optional<uint16_t> height_mm;
  std::optional<uint32_t> height_age_ms;
  bool bypass = false;
  std::optional<uint32_t> last_manual_ms;
  std::optional<std::string> fault;
  std::optional<Direction> blocked_direction;
  bool armed = false;
};

enum class CoreEventKind : uint8_t {
  ManualOverride,
  LeaseExpired,
  LimitReached,
  MaxDuration,
  DeskError,
  OwnerGone,
  Stopped,
  StopMethodSent,
};

const char* toString(CoreEventKind k);

struct CoreEvent {
  CoreEventKind kind;
  std::optional<Direction> direction;
  std::string detail;
};

/// Sources accepted in the envelope (spec 10.1).
bool isKnownSource(const std::string& source);

class CommandService {
 public:
  CommandService(HardwareAdapter& hw, Clock& clock, Config cfg = {});

  /// Boot or reboot: release everything, run self-test, wait boot_quiet_ms.
  void begin();
  CommandResult submit(const CommandEnvelope& env);
  /// Call at least every 5 ms. Polls the adapter and enforces timers.
  void tick();
  /// A transport connection closed: its lease is released immediately.
  void clientGone(const std::string& client_id);
  /// Re-run self-test. Clears FAULT on success.
  bool runSelfTest();

  Status status();
  std::vector<CoreEvent> takeEvents();
  /// Increments whenever something a client would display changes.
  uint64_t changeCounter() const { return change_counter_; }

  const Config& config() const { return cfg_; }
  /// Only when IDLE and the config validates.
  ErrorCode setConfig(const Config& cfg, std::string* why = nullptr);

 private:
  struct Active {
    Action action;
    std::string command;  // "desk.up" ...
    std::string owner_client;
    std::string owner_source;
    bool held = false;  // lease-based (UP/DOWN, or presets in HoldToRun)
    bool pending = false;
    uint64_t apply_at = 0;
    uint64_t started_at = 0;
    uint64_t lease_deadline = 0;
    uint64_t tap_ends_at = 0;
  };

  struct Seen {
    std::string id;
    uint64_t at;
    CommandResult result;
  };

  CommandResult dispatch(const CommandEnvelope& env, const std::string& client, uint64_t now);
  CommandResult startMovement(const CommandEnvelope& env, Action a, const std::string& client, uint64_t now);
  void apply(Active& a, uint64_t now);
  void abort();
  void stop(uint64_t now);
  void handleLimit(Direction d, uint64_t now);
  void emit(CoreEventKind k, std::optional<Direction> d = std::nullopt, std::string detail = {});
  bool manualQuiet(uint64_t now) const;
  bool rateLimited(const std::string& source, uint64_t now);
  uint32_t clampLease(std::optional<uint32_t> ms) const;
  void refreshState(uint64_t now);
  void bumpIfChanged();

  HardwareAdapter& hw_;
  Clock& clock_;
  Config cfg_;

  DeskState state_ = DeskState::SelfTest;
  std::optional<std::string> fault_;
  std::optional<Active> active_;
  bool armed_ = false;
  uint64_t boot_at_ = 0;
  std::optional<uint64_t> last_manual_at_;
  bool manual_now_ = false;
  bool bypass_now_ = false;

  std::optional<Direction> blocked_dir_;
  uint64_t blocked_until_ = 0;

  bool preset_maybe_moving_ = false;
  uint64_t preset_started_at_ = 0;
  uint64_t disarm_at_ = 0;

  std::optional<uint16_t> last_height_;
  std::optional<Direction> height_trend_;
  uint64_t last_height_change_at_ = 0;

  std::deque<Seen> seen_;
  std::map<std::string, std::deque<uint64_t>> rate_;
  std::vector<CoreEvent> events_;

  uint64_t change_counter_ = 0;
  std::string last_key_;
};

}  // namespace airo
