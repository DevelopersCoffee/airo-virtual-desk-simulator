#include "web.h"

#include <memory>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "firmware.h"

namespace airo::esp {

namespace {
constexpr const char* TAG = "airo-web";
httpd_handle_t g_server = nullptr;

const char* statusLine(int code) {
  switch (code) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 204: return "204 No Content";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 423: return "423 Locked";
    case 429: return "429 Too Many Requests";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
  }
}

const char* methodName(int m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PATCH: return "PATCH";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "?";
  }
}

void cors(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PATCH, OPTIONS");
}

esp_err_t apiHandler(httpd_req_t* req) {
  cors(req);
  if (req->method == HTTP_OPTIONS) {
    httpd_resp_set_status(req, statusLine(204));
    return httpd_resp_send(req, nullptr, 0);
  }
  ApiRequest r;
  r.method = methodName(req->method);
  r.path = req->uri;
  const size_t q = r.path.find('?');
  if (q != std::string::npos) r.path.resize(q);
  const size_t alen = httpd_req_get_hdr_value_len(req, "Authorization");
  if (alen > 0) {
    std::string a(alen + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, "Authorization", a.data(), a.size()) == ESP_OK) {
      a.resize(alen);
      r.authorization = a;
    }
  }
  if (req->content_len > 8192) {
    httpd_resp_set_status(req, statusLine(400));
    return httpd_resp_sendstr(req, "{\"type\":\"error\",\"code\":\"bad_request\",\"detail\":\"body too large\"}");
  }
  r.body.resize(req->content_len);
  size_t got = 0;
  while (got < req->content_len) {
    const int n = httpd_req_recv(req, r.body.data() + got, req->content_len - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return ESP_FAIL;
    got += static_cast<size_t>(n);
  }
  ApiResponse res;
  {
    std::lock_guard<std::mutex> l(g_fw->lock);
    res = g_fw->app.http(r);
  }
  httpd_resp_set_status(req, statusLine(res.status));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, res.body.data(), static_cast<ssize_t>(res.body.size()));
}

bool recvText(httpd_req_t* req, std::string& out, httpd_ws_type_t& type) {
  httpd_ws_frame_t pkt = {};
  if (httpd_ws_recv_frame(req, &pkt, 0) != ESP_OK) return false;
  type = pkt.type;
  if (pkt.len == 0 || pkt.len > 16384) {
    out.clear();
    return pkt.len == 0;
  }
  out.assign(pkt.len, '\0');
  pkt.payload = reinterpret_cast<uint8_t*>(out.data());
  return httpd_ws_recv_frame(req, &pkt, pkt.len) == ESP_OK;
}

esp_err_t appWsHandler(httpd_req_t* req) {
  const int fd = httpd_req_to_sockfd(req);
  if (req->method == HTTP_GET) {  // handshake
    std::lock_guard<std::mutex> l(g_fw->lock);
    return g_fw->app.wsOpen(fd) ? ESP_OK : ESP_FAIL;
  }
  std::string text;
  httpd_ws_type_t type;
  if (!recvText(req, text, type)) return ESP_FAIL;
  if (type != HTTPD_WS_TYPE_TEXT) return ESP_OK;
  std::lock_guard<std::mutex> l(g_fw->lock);
  g_fw->app.wsText(fd, text);
  return ESP_OK;
}

esp_err_t benchWsHandler(httpd_req_t* req) {
  const int fd = httpd_req_to_sockfd(req);
  if (req->method == HTTP_GET) {
    std::lock_guard<std::mutex> l(g_fw->lock);
    std::string why;
    if (g_fw->bench_fd != -1 || !g_fw->bench.open([fd](const std::string& t) { wsSend(fd, t); }, &why)) {
      g_fw->log("bench: refused: " + (why.empty() ? std::string("a bench link is already open") : why));
      return ESP_FAIL;
    }
    g_fw->bench_fd = fd;
    g_fw->log("bench: link connected");
    return ESP_OK;
  }
  std::string text;
  httpd_ws_type_t type;
  if (!recvText(req, text, type)) return ESP_FAIL;
  if (type != HTTPD_WS_TYPE_TEXT) return ESP_OK;
  auto msg = Json::parse(text);
  if (!msg) return ESP_OK;
  std::lock_guard<std::mutex> l(g_fw->lock);
  if (fd != g_fw->bench_fd) return ESP_FAIL;
  g_fw->onBenchResult(g_fw->bench.onMessage(*msg));
  return ESP_OK;
}

void onClose(httpd_handle_t, int fd) {
  {
    std::lock_guard<std::mutex> l(g_fw->lock);
    if (fd == g_fw->bench_fd) {
      g_fw->bench_fd = -1;
      g_fw->bench.close();
      g_fw->core.begin();  // board gone: self-test fails -> FAULT
      g_fw->log("bench: link disconnected");
    } else {
      g_fw->app.wsClose(fd);
    }
  }
  close(fd);
}

struct Outgoing {
  int fd;
  std::string text;
};

void sendWork(void* arg) {
  std::unique_ptr<Outgoing> o(static_cast<Outgoing*>(arg));
  httpd_ws_frame_t pkt = {};
  pkt.type = HTTPD_WS_TYPE_TEXT;
  pkt.payload = reinterpret_cast<uint8_t*>(o->text.data());
  pkt.len = o->text.size();
  pkt.final = true;
  httpd_ws_send_frame_async(g_server, o->fd, &pkt);
}

void closeWork(void* arg) {
  const int fd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
  httpd_sess_trigger_close(g_server, fd);
}
}  // namespace

void wsSend(int fd, const std::string& text) {
  if (!g_server) return;
  auto* o = new Outgoing{fd, text};
  if (httpd_queue_work(g_server, sendWork, o) != ESP_OK) delete o;
}

void wsClose(int fd) {
  if (g_server) httpd_queue_work(g_server, closeWork, reinterpret_cast<void*>(static_cast<intptr_t>(fd)));
}

bool startWeb(uint16_t port) {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = port;
  cfg.stack_size = 10240;
  cfg.max_open_sockets = 10;
  cfg.lru_purge_enable = true;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  cfg.close_fn = onClose;
  if (httpd_start(&g_server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    return false;
  }
  // WebSocket routes first: the wildcard below would match them too.
  httpd_uri_t app_ws = {};
  app_ws.uri = "/api/v1/ws";
  app_ws.method = HTTP_GET;
  app_ws.handler = appWsHandler;
  app_ws.is_websocket = true;
  httpd_register_uri_handler(g_server, &app_ws);

  httpd_uri_t bench_ws = {};
  bench_ws.uri = "/bench";
  bench_ws.method = HTTP_GET;
  bench_ws.handler = benchWsHandler;
  bench_ws.is_websocket = true;
  httpd_register_uri_handler(g_server, &bench_ws);

  for (httpd_method_t m : {HTTP_GET, HTTP_POST, HTTP_PATCH, HTTP_OPTIONS}) {
    httpd_uri_t api = {};
    api.uri = "/api/v1/*";
    api.method = m;
    api.handler = apiHandler;
    httpd_register_uri_handler(g_server, &api);
  }
  return true;
}

}  // namespace airo::esp
