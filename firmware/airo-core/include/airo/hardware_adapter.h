// HardwareAdapter: the only way airo-core touches the desk (spec section 8.1).
//
// Esp32HardwareAdapter (retrofit phase 2) drives the Interface Board's GPIO /
// I2C expander / UART. VirtualHardwareAdapter (laptop host) speaks the JSON
// bridge protocol to the simulated Interface Board in the 3D simulator.
#pragma once

#include <cstdint>
#include <optional>

#include "airo/types.h"

namespace airo {

struct HardwareAdapter {
  virtual ~HardwareAdapter() = default;

  /// false = refuse to arm, enter FAULT.
  virtual bool selfTest() = 0;
  /// Request the selector to the Airo path (no-op in case A1).
  virtual void arm(bool on) = 0;
  /// Start asserting one function.
  virtual void press(Action a) = 0;
  /// Assert one function for `ms`, then release (the adapter times it).
  virtual void tap(Action a, uint16_t ms) = 0;
  /// Idempotent; must complete within manual_release_ms (20 ms).
  virtual void releaseAll() = 0;
  /// Controller-specific stop method for a one-touch preset in progress
  /// (spec 7.3): stop frame on B/B0, short tap on switch lines (A/C).
  /// [ASSUMPTION] until bench stage T6 verifies it.
  virtual void stopMotion(std::optional<Direction> travel) = 0;
  /// Keypad activity right now.
  virtual bool manualActive() = 0;
  /// MANUAL ONLY switch / selector forced to keypad path.
  virtual bool bypassActive() = 0;
  /// Height reported by the controller, if this interface case has any.
  virtual std::optional<uint16_t> heightMm() = 0;
  /// Age of the last height report.
  virtual std::optional<uint32_t> heightAgeMs() { return std::nullopt; }
  /// Limit hit, controller error, manual press.
  virtual std::optional<DeskEvent> pollEvent() = 0;
};

struct Clock {
  virtual ~Clock() = default;
  /// Monotonic milliseconds.
  virtual uint64_t nowMs() = 0;
};

}  // namespace airo
