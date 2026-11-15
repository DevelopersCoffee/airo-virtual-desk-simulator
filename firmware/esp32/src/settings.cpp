#include "settings.h"

#include <cstdio>

#include "esp_random.h"
#include "nvs.h"

namespace airo::esp {

namespace {
constexpr const char* kNs = "airo";

std::string get(nvs_handle_t h, const char* key) {
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) return {};
  std::string v(len, '\0');
  if (nvs_get_str(h, key, v.data(), &len) != ESP_OK) return {};
  v.resize(len - 1);  // drop the terminator
  return v;
}

std::string randomToken() {
  char buf[33];
  for (int i = 0; i < 4; ++i) std::snprintf(buf + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
  return std::string(buf, 32);
}
}  // namespace

Settings loadSettings() {
  Settings s;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) == ESP_OK) {
    s.wifi_ssid = get(h, "ssid");
    s.wifi_pass = get(h, "pass");
    s.token = get(h, "token");
    const std::string id = get(h, "device_id");
    if (!id.empty()) s.device_id = id;
    nvs_close(h);
  }
  if (s.token.empty()) {
    s.token = randomToken();
    saveSettings(s);
  }
  return s;
}

bool saveSettings(const Settings& s) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_str(h, "ssid", s.wifi_ssid.c_str()) == ESP_OK;
  ok = ok && nvs_set_str(h, "pass", s.wifi_pass.c_str()) == ESP_OK;
  ok = ok && nvs_set_str(h, "token", s.token.c_str()) == ESP_OK;
  ok = ok && nvs_set_str(h, "device_id", s.device_id.c_str()) == ESP_OK;
  ok = ok && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

}  // namespace airo::esp
