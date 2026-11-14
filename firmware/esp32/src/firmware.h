// Everything the ESP32-S3 firmware runs, in one place, guarded by one lock.
// The HTTP server task, the console task and the main loop all take `lock`
// before touching the core, the adapter or the bench link.
#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "airo/app_server.h"
#include "airo/board/bench_link.h"
#include "airo/board/esp32_hardware_adapter.h"
#include "airo/command_service.h"
#include "esp_io.h"
#include "settings.h"

namespace airo::esp {

struct Firmware {
  Firmware(Settings s, InterfaceCase initial, bool desk_mode_compiled);

  std::mutex lock;
  Settings settings;
  EspClock clock;
  EspGpio gpio;
  EspUart htx{UART_NUM_1};
  EspUart dtx{UART_NUM_2};
  board::BenchIo io;
  board::Esp32HardwareAdapter hw;
  CommandService core;
  board::BenchLink bench;
  AppServer app;

  int bench_fd = -1;
  bool halted = false;               // bench "halt": core stopped until reboot
  std::atomic<uint32_t> hang_ms{0};  // bench "hang": main loop freezes this long

  /// Bench link result handling shared by the WS handler (lock held).
  void onBenchResult(board::BenchLink::Result r);
  /// Soft reboot of the core (bench "reboot"): lock held.
  void softReboot();
  void log(const std::string& line);
  api::InfoContext infoContext();
};

/// Set once in app_main.
extern Firmware* g_fw;

}  // namespace airo::esp
