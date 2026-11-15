#include "console.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "driver/uart.h"
#include "esp_system.h"
#include "firmware.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace airo::esp {

void restartWifi();  // main.cpp

namespace {

void say(const std::string& s) {
  std::printf("%s\n", s.c_str());
  std::fflush(stdout);
}

void help() {
  say("Airo console. Commands:\n"
      "  info                      device, bench and pin state (JSON)\n"
      "  pins                      the pin map and its warnings\n"
      "  drive on BENCH-ONLY       drive output pins on the breadboard (bench mode only, resets at boot)\n"
      "  drive off                 release every output to high impedance\n"
      "  inputs link|physical      where inputs come from (HIL needs link)\n"
      "  case A1|A2|B              interface case for the bench\n"
      "  wifi <ssid> <password>    store Wi-Fi credentials and reconnect\n"
      "  token [new-token]         show or set the API token (set needs reboot)\n"
      "  reboot");
}

void pins() {
  auto& fw = *g_fw;
  const auto& m = fw.io.pins();
  const InterfaceCase c = fw.hw.interfaceCase();
  say(std::string("pin map (") + (m.verified ? "verified" : "UNVERIFIED") + "), case " + toString(c) + ":");
  for (board::Signal s : board::signalsFor(c)) {
    const auto& p = m[s];
    say("  " + std::string(board::toString(s)) + " = " + (p.gpio < 0 ? std::string("-") : "GPIO" + std::to_string(p.gpio)) +
        (p.active_low ? " (active low)" : "") + (board::isOutput(s) ? "  out" : "  in"));
  }
  for (board::UartLine l : board::uartLinesFor(c))
    say("  " + std::string(board::toString(l)) + " = GPIO" + std::to_string(m.uart(l)));
  for (const auto& e : board::validate(m, c)) say("  ERROR: " + e);
  for (const auto& w : board::warnings(m, c)) say("  warning: " + w);
}

void run(const std::string& line) {
  std::istringstream in(line);
  std::vector<std::string> a;
  for (std::string w; in >> w;) a.push_back(w);
  if (a.empty()) return;
  auto& fw = *g_fw;
  const std::string& cmd = a[0];
  if (cmd == "help" || cmd == "?") return help();
  if (cmd == "info") {
    std::lock_guard<std::mutex> l(fw.lock);
    Json j = fw.app.info();
    j.set("token_set", !fw.settings.token.empty());
    j.set("wifi_ssid", fw.settings.wifi_ssid);
    return say(j.dump());
  }
  if (cmd == "pins") {
    std::lock_guard<std::mutex> l(fw.lock);
    return pins();
  }
  if (cmd == "drive") {
    std::lock_guard<std::mutex> l(fw.lock);
    std::string why;
    if (a.size() >= 2 && a[1] == "off") {
      fw.io.setBenchDrive(false, &why);
      return say("bench drive OFF: every output is high impedance");
    }
    if (a.size() >= 3 && a[1] == "on" && a[2] == "BENCH-ONLY") {
      if (!fw.io.setBenchDrive(true, &why)) return say("refused: " + why);
      say("bench drive ON: output pins now drive the breadboard. Never connect this board to the desk.");
      fw.bench.sendState();
      return;
    }
    return say("usage: drive off | drive on BENCH-ONLY");
  }
  if (cmd == "inputs" && a.size() >= 2) {
    std::lock_guard<std::mutex> l(fw.lock);
    std::string why;
    const bool phys = a[1] == "physical";
    if (!phys && a[1] != "link") return say("usage: inputs link|physical");
    if (!fw.io.setInputs(phys ? board::InputSource::Physical : board::InputSource::Link, &why)) return say("refused: " + why);
    fw.hw.setInputsReady(phys || fw.bench.helloReceived());
    fw.core.begin();
    fw.bench.sendState();
    return say(std::string("inputs: ") + board::toString(fw.io.inputs()) + ", core rebooted");
  }
  if (cmd == "case" && a.size() >= 2) {
    auto c = parseInterfaceCase(a[1]);
    if (!c) return say("usage: case A1|A2|B");
    std::lock_guard<std::mutex> l(fw.lock);
    fw.hw.setInterfaceCase(*c);
    fw.core.begin();
    fw.bench.sendState();
    return say(std::string("interface case ") + toString(*c) + ", core rebooted");
  }
  if (cmd == "wifi" && a.size() >= 2) {
    {
      std::lock_guard<std::mutex> l(fw.lock);
      fw.settings.wifi_ssid = a[1];
      fw.settings.wifi_pass = a.size() >= 3 ? a[2] : "";
      if (!saveSettings(fw.settings)) return say("could not save to NVS");
    }
    say("saved; connecting to " + a[1]);
    restartWifi();
    return;
  }
  if (cmd == "token") {
    std::lock_guard<std::mutex> l(fw.lock);
    if (a.size() < 2) return say("token: " + fw.settings.token);
    if (a[1].size() < 12) return say("token must be at least 12 characters");
    fw.settings.token = a[1];
    if (!saveSettings(fw.settings)) return say("could not save to NVS");
    return say("token saved; reboot to apply");
  }
  if (cmd == "reboot") {
    say("rebooting");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
  }
  say("unknown command; type help");
}

void consoleTask(void*) {
  std::string line;
  uint8_t ch;
  for (;;) {
    const int n = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
    if (n <= 0) continue;
    if (ch == '\r' || ch == '\n') {
      if (!line.empty()) {
        std::printf("\n");
        run(line);
        line.clear();
      }
      continue;
    }
    if ((ch == 8 || ch == 127) && !line.empty()) {
      line.pop_back();
      continue;
    }
    if (line.size() < 256) line.push_back(static_cast<char>(ch));
  }
}

}  // namespace

void startConsole() {
  uart_driver_install(UART_NUM_0, 512, 0, 0, nullptr, 0);
  xTaskCreate(consoleTask, "airo-console", 8192, nullptr, 3, nullptr);
  help();
}

}  // namespace airo::esp
