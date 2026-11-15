// How each keypad function appears on the desk lines, per interface case.
// Shared by VirtualHardwareAdapter (laptop) and Esp32HardwareAdapter (board)
// so both drive exactly the same codes. All values are [COMMUNITY] /
// [ASSUMPTION] until discovery (spec section 5) measures the real keypad.
#pragma once

#include <cstdint>
#include <optional>

#include "airo/frames.h"
#include "airo/types.h"

namespace airo::keypad {

/// A2 code on HS3..HS0 (inverted logic shown as numbers) [COMMUNITY].
inline int encodedCode(Action a) {
  switch (a) {
    case Action::Down: return 1;
    case Action::Up: return 2;
    case Action::Sitting: return 3;   // Preset 1
    case Action::Standing: return 4;  // Preset 2
    case Action::Reset: return 8;     // [UNKNOWN] placeholder, never driven
  }
  return 0;
}

/// A1 line name, also the bridge protocol name.
inline const char* lineName(Action a) {
  switch (a) {
    case Action::Up: return "up";
    case Action::Down: return "down";
    case Action::Sitting: return "sit";
    case Action::Standing: return "stand";
    case Action::Reset: return "reset";
  }
  return "?";
}

/// Case B handset frame command. Reset is never sent.
inline std::optional<uint8_t> frameCommand(Action a) {
  switch (a) {
    case Action::Up: return frames::kCmdUp;
    case Action::Down: return frames::kCmdDown;
    case Action::Sitting: return frames::kCmdMemory1;
    case Action::Standing: return frames::kCmdMemory2;
    case Action::Reset: return std::nullopt;
  }
  return std::nullopt;
}

constexpr uint32_t kFrameRepeatMs = 50;   // keypad repeats a held frame [ASSUMPTION]
constexpr uint32_t kFrameActiveMs = 120;  // keypad counts as active this long after a frame
constexpr uint32_t kHeightValidMs = 1000;
constexpr uint16_t kStopTapMs = 150;

}  // namespace airo::keypad
