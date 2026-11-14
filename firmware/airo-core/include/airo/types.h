// Portable types shared by the Airo firmware core and its hardware adapters.
// No ESP-IDF, POSIX or Arduino includes are allowed under airo-core/.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace airo {

/// The five functions of the original keypad (spec section 4).
enum class Action : uint8_t { Up, Down, Sitting, Standing, Reset };

enum class Direction : uint8_t { Up, Down };

/// Firmware state machine (spec section 8.2).
enum class DeskState : uint8_t { SelfTest, Idle, AppActive, ManualActive, Bypass, Fault };

/// Something the hardware observed (limit hit, controller error, manual press).
enum class DeskEventKind : uint8_t { LimitReached, ControllerError, ManualPress };

struct DeskEvent {
  DeskEventKind kind;
  std::optional<Direction> direction;
  std::string detail;
};

/// Interface case from the discovery decision tree (spec section 5.3).
/// Only the cases the simulator models are listed.
enum class InterfaceCase : uint8_t { A1, A2, B };

const char* toString(Action a);
const char* toString(Direction d);
const char* toString(DeskState s);
const char* toString(InterfaceCase c);
std::optional<InterfaceCase> parseInterfaceCase(const std::string& s);

inline std::optional<Direction> directionOf(Action a) {
  if (a == Action::Up) return Direction::Up;
  if (a == Action::Down) return Direction::Down;
  return std::nullopt;
}

inline Direction opposite(Direction d) { return d == Direction::Up ? Direction::Down : Direction::Up; }

}  // namespace airo
