// Settings kept in NVS: Wi-Fi credentials, API token, device id.
// Set them at the serial console (see console.cpp). Config (timings) is not
// stored: every boot starts from the defaults in airo/config.h.
#pragma once

#include <string>

namespace airo::esp {

struct Settings {
  std::string wifi_ssid;
  std::string wifi_pass;
  std::string token;
  std::string device_id = "airo-desk-001";
};

Settings loadSettings();  // generates and stores a random token on first boot
bool saveSettings(const Settings& s);

}  // namespace airo::esp
