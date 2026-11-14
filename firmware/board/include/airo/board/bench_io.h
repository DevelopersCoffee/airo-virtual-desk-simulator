// BenchIo: the single gate between the board code and the chip's pins.
//
// BENCH MODE (the default, and the only mode this firmware can be built in
// unless AIRO_ALLOW_DESK_MODE is defined) means:
//   * every output pin is left high-impedance: nothing is driven;
//   * the logical value of each output is still tracked and reported to the
//     bench link (the laptop desk model) and over /api/v1/info;
//   * inputs come from the bench link by default, not from wires.
// On the bench an operator may enable physical drive from the USB serial
// console (never over the network) to light breadboard LEDs. That switch is
// not stored and resets to OFF at every boot.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "airo/board/io.h"
#include "airo/board/pin_map.h"

namespace airo::board {

enum class Mode : uint8_t { Bench, Desk };
enum class InputSource : uint8_t { Link, Physical };

const char* toString(Mode m);
const char* toString(InputSource s);

class BenchIo {
 public:
  /// `desk_mode_compiled` = the firmware was built with AIRO_ALLOW_DESK_MODE.
  /// No build in this repository sets it.
  BenchIo(Gpio& gpio, Uart* htx, Uart* dtx, PinMap map, bool desk_mode_compiled = false);

  /// Configure the pins one interface case uses. All outputs end up
  /// logically released; physically Hi-Z unless drive is enabled.
  void configure(InterfaceCase c);

  Mode mode() const { return mode_; }
  bool benchMode() const { return mode_ == Mode::Bench; }
  bool deskModeCompiled() const { return desk_mode_compiled_; }
  /// Leaving bench mode is refused unless desk mode was compiled in.
  bool setMode(Mode m, std::string* why = nullptr);

  /// Physical output drive. Always true in desk mode; in bench mode false
  /// until someone turns it on at the serial console.
  bool driveEnabled() const { return mode_ == Mode::Desk || bench_drive_; }
  bool setBenchDrive(bool on, std::string* why = nullptr);

  InputSource inputs() const { return mode_ == Mode::Desk ? InputSource::Physical : inputs_; }
  bool setInputs(InputSource s, std::string* why = nullptr);

  // --- Core side (Esp32HardwareAdapter) ---
  void write(Signal s, bool asserted) { writeGroup({{s, asserted}}); }
  /// Change several outputs as one step (one register write, one link message).
  void writeGroup(const std::vector<std::pair<Signal, bool>>& values);
  bool asserted(Signal s) const { return shadow_[static_cast<size_t>(s)]; }
  bool read(Signal s);
  /// Case B: bytes onto controller HTX (through K2 NO on the real board).
  void uartWrite(const std::vector<uint8_t>& bytes);
  std::vector<uint8_t> uartRead(UartLine rx);
  /// Every output logically released and physically Hi-Z if drive is off.
  void releaseOutputs();

  // --- Bench link side ---
  void linkSet(Signal s, bool asserted);
  void linkRx(UartLine rx, const std::vector<uint8_t>& bytes);
  void linkClear();
  /// Fired once per write that changed outputs' logical values, driven or not.
  std::function<void(const std::vector<std::pair<Signal, bool>>&)> onOutput;
  /// Fired for every byte block written to HTX, driven or not.
  std::function<void(const std::vector<uint8_t>&)> onTx;

  const PinMap& pins() const { return map_; }
  /// Count of physical pin writes; the tests assert it stays 0 in bench mode.
  uint32_t physicalWrites() const { return physical_writes_; }

 private:
  bool used(Signal s) const;
  void apply(Signal s);
  void applyAll();

  Gpio& gpio_;
  Uart* htx_;
  Uart* dtx_;
  PinMap map_;
  bool desk_mode_compiled_;
  Mode mode_ = Mode::Bench;
  bool bench_drive_ = false;
  InputSource inputs_ = InputSource::Link;
  std::optional<InterfaceCase> case_;
  std::array<bool, kSignalCount> shadow_{};
  std::array<bool, kSignalCount> link_in_{};
  std::vector<uint8_t> link_rx_[static_cast<int>(UartLine::Count)];
  uint32_t physical_writes_ = 0;
};

}  // namespace airo::board
