// Airo Core firmware, ESP32-S3, bench prototype (retrofit phase 2).
//
// Runs the same airo-core CommandService and AppServer as the laptop host,
// with Esp32HardwareAdapter on real GPIO through BenchIo. BENCH MODE ONLY:
// outputs are high impedance unless bench drive is switched on at the serial
// console, and desk mode is not compiled in. Never connect this to the desk.
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "firmware.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "console.h"
#include "web.h"
#if __has_include("mdns.h")
#include "mdns.h"
#define AIRO_HAVE_MDNS 1
#endif

#ifndef AIRO_INTERFACE_CASE_DEFAULT
#define AIRO_INTERFACE_CASE_DEFAULT "A2"
#endif
#ifdef AIRO_ALLOW_DESK_MODE
#error "Desk mode is not allowed in this firmware yet: the Interface Board and discovery (spec 5) are not done."
#endif

namespace airo::esp {

Firmware* g_fw = nullptr;

namespace {
constexpr const char* TAG = "airo";

Config bootConfig(const Settings& s) {
  Config c;
  c.device_id = s.device_id;
  return c;
}

AppServer::Hooks hooks(Firmware& fw) {
  AppServer::Hooks h;
  h.send = [](int fd, const std::string& t) { wsSend(fd, t); };
  h.close = [](int fd) { wsClose(fd); };
  h.online = [&fw]() { return fw.hw.powered(); };
  h.info = [&fw]() { return fw.infoContext(); };
  h.extend_info = [&fw](Json& info) {
    Json b = fw.bench.state();
    Json pins = Json::object();
    const auto& m = fw.io.pins();
    for (board::Signal s : board::signalsFor(fw.hw.interfaceCase())) pins.set(board::toString(s), m[s].gpio);
    for (board::UartLine l : board::uartLinesFor(fw.hw.interfaceCase())) pins.set(board::toString(l), m.uart(l));
    b.set("pins", pins);
    Json warn = Json::array();
    for (const auto& w : board::warnings(m, fw.hw.interfaceCase())) warn.push(w);
    for (const auto& e : board::validate(m, fw.hw.interfaceCase())) warn.push("ERROR: " + e);
    b.set("pin_warnings", warn);
    info.set("bench", b);
  };
  h.log = [&fw](const std::string& line) { fw.log(line); };
  return h;
}

board::PinMap pinMap() { return board::defaultPinMap(); }

InterfaceCase initialCase() {
  auto c = parseInterfaceCase(AIRO_INTERFACE_CASE_DEFAULT);
  return c ? *c : InterfaceCase::A2;
}

// --- Wi-Fi -----------------------------------------------------------------
bool g_wifi_started = false;

void onWifiEvent(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto* e = static_cast<ip_event_got_ip_t*>(data);
    std::printf("AIRO_IP " IPSTR "  (http://" IPSTR "/api/v1/info, http://%s.local)\n", IP2STR(&e->ip_info.ip),
                IP2STR(&e->ip_info.ip), g_fw->settings.device_id.c_str());
    std::fflush(stdout);
  }
}

void startMdns(const std::string& host) {
#ifdef AIRO_HAVE_MDNS
  if (mdns_init() != ESP_OK) return;
  mdns_hostname_set(host.c_str());
  mdns_instance_name_set("Airo Desk (bench)");
  mdns_txt_item_t txt[] = {{"api", "1"}, {"mode", "bench"}};
  mdns_service_add(nullptr, "_airo", "_tcp", 80, txt, 2);
  mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
#else
  (void)host;
  ESP_LOGW(TAG, "built without the mdns component: use the IP address");
#endif
}
}  // namespace

void restartWifi() {
  wifi_config_t wc = {};
  std::strncpy(reinterpret_cast<char*>(wc.sta.ssid), g_fw->settings.wifi_ssid.c_str(), sizeof(wc.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(wc.sta.password), g_fw->settings.wifi_pass.c_str(), sizeof(wc.sta.password) - 1);
  if (g_wifi_started) {
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
    return;
  }
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  esp_wifi_start();
  esp_wifi_set_ps(WIFI_PS_NONE);  // modem sleep adds 100+ ms to every bench message
  g_wifi_started = true;
}

Firmware::Firmware(Settings s, InterfaceCase initial, bool desk_mode_compiled)
    : settings(std::move(s)),
      io(gpio, &htx, &dtx, pinMap(), desk_mode_compiled),
      hw(io, clock, initial, bootConfig(settings).watchdog_period_ms),
      core(hw, clock, bootConfig(settings)),
      bench(io, hw),
      app(core, clock, settings.token, hooks(*this)) {}

void Firmware::log(const std::string& line) {
  std::printf("%s\n", line.c_str());
  std::fflush(stdout);
}

api::InfoContext Firmware::infoContext() {
  api::InfoContext ctx;
  ctx.hardware = "esp32-s3";
  ctx.adapter = "esp32";
  ctx.interface_case = (bench.helloReceived() || io.inputs() == board::InputSource::Physical)
                           ? std::optional<InterfaceCase>(hw.interfaceCase())
                           : std::nullopt;
  ctx.height_feedback = hw.interfaceCase() == InterfaceCase::B;
  ctx.stop_method_verified = false;
  ctx.board_connected = bench.helloReceived();
  return ctx;
}

void Firmware::softReboot() {
  halted = false;
  hw.setPowered(true);
  app.dropAll();
  core.begin();
}

void Firmware::onBenchResult(board::BenchLink::Result r) {
  using R = board::BenchLink::Result;
  switch (r) {
    case R::Hello:
      halted = false;
      core.begin();
      log(std::string("bench: hello, interface case ") + toString(hw.interfaceCase()) + ", core rebooting");
      break;
    case R::PowerChanged:
      if (hw.powered()) {
        core.begin();
        log("bench: Airo power restored, core booting");
      } else {
        app.dropAll();
        log("bench: Airo power cut");
      }
      break;
    case R::Hang: hang_ms = bench.hangMs(); break;
    case R::Halt:
      halted = true;
      log("bench: halted");
      break;
    case R::Reboot:
      softReboot();
      log("bench: reboot");
      break;
    case R::Invalid: log("bench: invalid message"); break;
    case R::None: break;
  }
}

}  // namespace airo::esp

using namespace airo;
using namespace airo::esp;

extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  static Firmware fw(loadSettings(), initialCase(), false);
  g_fw = &fw;
  {
    std::lock_guard<std::mutex> l(fw.lock);
    fw.core.begin();  // no bench link yet: self-test fails, FAULT until hello
  }
  std::printf("\nAIRO_BOOT fw=%s mode=%s drive=%s pins=%s case=%s\n", api::kFirmwareVersion,
              board::toString(fw.io.mode()), fw.io.driveEnabled() ? "on" : "off",
              fw.io.pins().verified ? "verified" : "UNVERIFIED", toString(fw.hw.interfaceCase()));

  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&wcfg);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent, nullptr);
  if (fw.settings.wifi_ssid.empty()) std::printf("No Wi-Fi configured. Type: wifi <ssid> <password>\n");
  else restartWifi();
  startMdns(fw.settings.device_id);
  startWeb(80);
  startConsole();

  for (;;) {
    {
      std::lock_guard<std::mutex> l(fw.lock);
      const uint32_t hang = fw.hang_ms.exchange(0);
      if (hang) vTaskDelay(pdMS_TO_TICKS(hang));  // bench "hang": everything waits, heartbeat stops
      if (!fw.halted) {
        fw.hw.service();
        if (fw.hw.powered()) {
          fw.core.tick();
          fw.app.loop();
        }
      }
    }
    vTaskDelay(1);  // 1 ms at CONFIG_FREERTOS_HZ=1000
  }
}
