// Pin map for the ESP32-S3 bench prototype (retrofit phase 2).
//
// EVERY PIN HERE IS UNVERIFIED. The keypad interface has not been identified
// (spec section 5), so these are breadboard placeholders that avoid the
// ESP32-S3's strapping, USB, flash/PSRAM and on-board LED pins. Override any
// of them at build time with -DAIRO_PIN_<NAME>=<gpio> (-1 = not connected);
// see firmware/esp32/platformio.ini. Nothing in this map is wired to the desk.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "airo/types.h"

namespace airo::board {

/// Every Port C / Interface Board signal the Core drives or reads.
/// Outputs first, then inputs.
enum class Signal : uint8_t {
  // Outputs (Core -> Interface Board). Driven only when BenchIo allows it.
  Heartbeat,  ///< watchdog toggle (spec 6.1 R7)
  Arm,        ///< selector request (K1 in A2, K2 in B)
  InjUp,      ///< A1 injectors, one per button
  InjDown,
  InjSit,
  InjStand,
  InjHs0,  ///< A2 encoded injector lines
  InjHs1,
  InjHs2,
  InjHs3,
  // Inputs (Interface Board -> Core).
  BypassState,  ///< MANUAL ONLY switch / selector position
  SenseUp,      ///< A1 keypad sense taps
  SenseDown,
  SenseSit,
  SenseStand,
  SenseReset,
  SenseHs0,  ///< A2 encoded sense lines
  SenseHs1,
  SenseHs2,
  SenseHs3,
  Count
};

constexpr int kSignalCount = static_cast<int>(Signal::Count);
constexpr int kFirstInput = static_cast<int>(Signal::BypassState);

inline bool isOutput(Signal s) { return static_cast<int>(s) < kFirstInput; }
const char* toString(Signal s);
std::optional<Signal> parseSignal(const std::string& name);

/// Case B UART lines. The Core never has a reset line: desk.reset is not exposed.
enum class UartLine : uint8_t { HtxTx, HtxRx, DtxRx, Count };
const char* toString(UartLine l);

struct PinAssignment {
  int gpio = -1;            ///< -1 = not connected
  bool active_low = false;  ///< electrical level that means "asserted" is LOW
};

struct PinMap {
  PinAssignment signals[kSignalCount];
  int uart_gpio[static_cast<int>(UartLine::Count)] = {-1, -1, -1};
  uint32_t uart_baud = 9600;  ///< [COMMUNITY] Jiecang, spec 13.1
  /// Stays false until the bench checks in docs/BENCH_TEST.md and discovery
  /// (spec 5.4) confirm the wiring. Reported in /api/v1/info.
  bool verified = false;

  PinAssignment& operator[](Signal s) { return signals[static_cast<int>(s)]; }
  const PinAssignment& operator[](Signal s) const { return signals[static_cast<int>(s)]; }
  int uart(UartLine l) const { return uart_gpio[static_cast<int>(l)]; }
  int& uart(UartLine l) { return uart_gpio[static_cast<int>(l)]; }
};

/// The placeholder breadboard map (or the AIRO_PIN_* overrides).
PinMap defaultPinMap();

/// The signals one interface case uses.
std::vector<Signal> signalsFor(InterfaceCase c);
std::vector<UartLine> uartLinesFor(InterfaceCase c);

/// Problems that make a map unusable for `c`: pins missing, used twice,
/// or on pins the ESP32-S3 must not use. Empty = usable.
std::vector<std::string> validate(const PinMap& m, InterfaceCase c);
/// Pins that are legal but worth a second look (octal PSRAM, JTAG).
std::vector<std::string> warnings(const PinMap& m, InterfaceCase c);

/// The A1 sense / injector signal for a button; nullopt for Reset injector.
std::optional<Signal> injectorFor(Action a);
Signal senseFor(Action a);

}  // namespace airo::board
