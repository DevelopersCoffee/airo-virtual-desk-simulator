// esp_http_server glue: REST /api/v1/*, WS /api/v1/ws (app), WS /bench (HIL).
#pragma once

#include <string>

namespace airo::esp {

bool startWeb(uint16_t port = 80);
/// Thread-safe: queued onto the server task.
void wsSend(int fd, const std::string& text);
void wsClose(int fd);

}  // namespace airo::esp
