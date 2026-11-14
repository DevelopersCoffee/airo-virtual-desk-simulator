// AppServer: the local API (spec section 10.3) independent of the socket
// library. The laptop host feeds it from its POSIX server and the ESP32-S3
// from esp_http_server, so both answer every request the same way.
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>

#include "airo/api.h"
#include "airo/command_service.h"
#include "airo/json.h"

namespace airo {

struct ApiRequest {
  std::string method;
  std::string path;           // without query
  std::string authorization;  // the Authorization header, or empty
  std::string body;
};

struct ApiResponse {
  int status = 200;
  std::string body;  // always JSON
};

class AppServer {
 public:
  struct Hooks {
    /// Send one WebSocket text frame to an app connection.
    std::function<void(int conn, const std::string& text)> send;
    /// Close an app connection.
    std::function<void(int conn)> close;
    /// What GET /api/v1/info reports about the hardware.
    std::function<api::InfoContext()> info;
    /// false = the core is unpowered: the API answers 503 and drops sockets.
    std::function<bool()> online;
    /// Extra fields for /api/v1/info (bench state on the board).
    std::function<void(Json& info)> extend_info;
    std::function<void(const std::string& line)> log;
  };

  AppServer(CommandService& core, Clock& clock, std::string token, Hooks hooks);

  ApiResponse http(const ApiRequest& r);
  /// A WebSocket opened on /api/v1/ws. false = refuse it.
  bool wsOpen(int conn);
  void wsText(int conn, const std::string& text);
  void wsClose(int conn);
  /// Close every app connection (power cut).
  void dropAll();
  /// Call after core.tick(): broadcasts events and status.
  void loop();

  Json info();
  const std::string& token() const { return token_; }

 private:
  void broadcast(const Json& j);
  void log(const std::string& line);

  CommandService& core_;
  Clock& clock_;
  std::string token_;
  Hooks hooks_;
  std::set<int> pending_;  // WS clients that have not authenticated yet
  std::set<int> clients_;  // authenticated WS clients
  uint64_t last_counter_ = ~0ull;
  uint64_t last_periodic_ = 0;
};

}  // namespace airo
