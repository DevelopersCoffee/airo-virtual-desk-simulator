// Small single-threaded HTTP/1.1 + WebSocket (RFC 6455) server on POSIX
// sockets, for the laptop host only. The ESP32-S3 build uses esp_http_server.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace airo::host {

struct HttpRequest {
  std::string method;
  std::string path;   // without query
  std::string query;
  std::map<std::string, std::string> headers;  // lower-case keys
  std::string body;
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json";
  std::string body;
  std::map<std::string, std::string> headers;
};

class NetServer {
 public:
  using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;
  using WsOpen = std::function<bool(int conn, const HttpRequest&)>;  // false = reject
  using WsText = std::function<void(int conn, const std::string&)>;
  using WsClose = std::function<void(int conn)>;

  NetServer() = default;
  ~NetServer();
  NetServer(const NetServer&) = delete;
  NetServer& operator=(const NetServer&) = delete;

  /// Binds to 127.0.0.1:port (0 = ephemeral). Returns the bound port, or -1.
  int listen(uint16_t port, const std::string& host = "127.0.0.1");
  /// Wait up to timeout_ms for I/O and dispatch callbacks.
  void poll(int timeout_ms);

  void sendText(int conn, const std::string& text);
  void close(int conn);
  bool isOpen(int conn) const { return conns_.count(conn) > 0; }

  HttpHandler onHttp;
  WsOpen onWsOpen;
  WsText onWsText;
  WsClose onWsClose;

 private:
  struct Conn {
    int fd;
    std::string in;
    std::string out;
    bool ws = false;
    bool close_after_write = false;
    bool closing = false;
    std::string fragments;
  };

  void accept();
  void readFrom(int id);
  void writeTo(int id);
  void handleHttp(int id);
  void handleWs(int id);
  void drop(int id);
  void queue(int id, const std::string& data);

  int listen_fd_ = -1;
  int next_id_ = 1;
  std::map<int, Conn> conns_;
};

std::string sha1(const std::string& data);
std::string base64(const std::string& data);
std::string websocketAccept(const std::string& key);

}  // namespace airo::host
