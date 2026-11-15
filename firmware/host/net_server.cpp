#include "net_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <vector>

namespace airo::host {

namespace {

constexpr size_t kMaxRequest = 64 * 1024;
constexpr size_t kMaxWsMessage = 64 * 1024;

const char* reason(int s) {
  switch (s) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 423: return "Locked";
    case 429: return "Too Many Requests";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string wsFrame(uint8_t opcode, const std::string& payload) {
  std::string f;
  f += static_cast<char>(0x80 | opcode);
  const size_t n = payload.size();
  if (n < 126) {
    f += static_cast<char>(n);
  } else if (n <= 0xFFFF) {
    f += static_cast<char>(126);
    f += static_cast<char>((n >> 8) & 0xFF);
    f += static_cast<char>(n & 0xFF);
  } else {
    f += static_cast<char>(127);
    for (int i = 7; i >= 0; --i) f += static_cast<char>((static_cast<uint64_t>(n) >> (8 * i)) & 0xFF);
  }
  return f + payload;
}

}  // namespace

std::string sha1(const std::string& data) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::string m = data;
  const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
  m += static_cast<char>(0x80);
  while (m.size() % 64 != 56) m += '\0';
  for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (8 * i)) & 0xFF);
  auto rol = [](uint32_t v, int s) { return (v << s) | (v >> (32 - s)); };
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(static_cast<uint8_t>(m[off + 4 * i])) << 24) |
             (static_cast<uint32_t>(static_cast<uint8_t>(m[off + 4 * i + 1])) << 16) |
             (static_cast<uint32_t>(static_cast<uint8_t>(m[off + 4 * i + 2])) << 8) |
             static_cast<uint32_t>(static_cast<uint8_t>(m[off + 4 * i + 3]));
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  std::string out;
  for (uint32_t v : h)
    for (int i = 3; i >= 0; --i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
  return out;
}

std::string base64(const std::string& data) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    const uint32_t v = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8) |
                       static_cast<uint8_t>(data[i + 2]);
    out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
  }
  if (i + 1 == data.size()) {
    const uint32_t v = static_cast<uint8_t>(data[i]) << 16;
    out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += "==";
  } else if (i + 2 == data.size()) {
    const uint32_t v = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8);
    out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '=';
  }
  return out;
}

std::string websocketAccept(const std::string& key) {
  return base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

NetServer::~NetServer() {
  for (auto& [id, c] : conns_) ::close(c.fd);
  if (listen_fd_ >= 0) ::close(listen_fd_);
}

int NetServer::listen(uint16_t port, const std::string& host) {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) return -1;
  int one = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) return -1;
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) return -1;
  if (::listen(listen_fd_, 16) < 0) return -1;
  ::fcntl(listen_fd_, F_SETFL, O_NONBLOCK);
  socklen_t len = sizeof addr;
  ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
  return ntohs(addr.sin_port);
}

void NetServer::poll(int timeout_ms) {
  std::vector<pollfd> fds;
  std::vector<int> ids;
  fds.push_back({listen_fd_, POLLIN, 0});
  ids.push_back(0);
  for (auto& [id, c] : conns_) {
    short ev = POLLIN;
    if (!c.out.empty()) ev |= POLLOUT;
    fds.push_back({c.fd, ev, 0});
    ids.push_back(id);
  }
  const int n = ::poll(fds.data(), fds.size(), timeout_ms);
  if (n <= 0) return;
  if (fds[0].revents & POLLIN) accept();
  for (size_t i = 1; i < fds.size(); ++i) {
    const int id = ids[i];
    if (!conns_.count(id)) continue;
    if (fds[i].revents & (POLLERR | POLLNVAL)) {
      drop(id);
      continue;
    }
    if (fds[i].revents & (POLLIN | POLLHUP)) readFrom(id);
    if (conns_.count(id) && (fds[i].revents & POLLOUT)) writeTo(id);
  }
}

void NetServer::accept() {
  while (true) {
    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) return;
    ::fcntl(fd, F_SETFL, O_NONBLOCK);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    conns_[next_id_++] = Conn{fd, {}, {}, false, false, false, {}};
  }
}

void NetServer::readFrom(int id) {
  Conn& c = conns_.at(id);
  char buf[8192];
  while (true) {
    const ssize_t r = ::recv(c.fd, buf, sizeof buf, 0);
    if (r > 0) {
      c.in.append(buf, static_cast<size_t>(r));
      if (c.in.size() > kMaxRequest * 2) {
        drop(id);
        return;
      }
      continue;
    }
    if (r == 0) {
      drop(id);
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    if (errno == EINTR) continue;
    drop(id);
    return;
  }
  if (c.ws) handleWs(id);
  else handleHttp(id);
}

void NetServer::writeTo(int id) {
  Conn& c = conns_.at(id);
  while (!c.out.empty()) {
    const ssize_t w = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
    if (w > 0) {
      c.out.erase(0, static_cast<size_t>(w));
      continue;
    }
    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (w < 0 && errno == EINTR) continue;
    drop(id);
    return;
  }
  if (c.close_after_write) drop(id);
}

void NetServer::queue(int id, const std::string& data) {
  auto it = conns_.find(id);
  if (it == conns_.end()) return;
  it->second.out += data;
  writeTo(id);
}

void NetServer::handleHttp(int id) {
  while (conns_.count(id) && !conns_.at(id).ws) {
    Conn& c = conns_.at(id);
    const size_t hdr_end = c.in.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
      if (c.in.size() > kMaxRequest) drop(id);
      return;
    }
    HttpRequest req;
    const std::string head = c.in.substr(0, hdr_end);
    size_t line_end = head.find("\r\n");
    const std::string first = head.substr(0, line_end);
    const size_t sp1 = first.find(' ');
    const size_t sp2 = first.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
      drop(id);
      return;
    }
    req.method = first.substr(0, sp1);
    std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = target.find('?');
    req.path = target.substr(0, q);
    if (q != std::string::npos) req.query = target.substr(q + 1);
    size_t pos = line_end == std::string::npos ? head.size() : line_end + 2;
    while (pos < head.size()) {
      size_t e = head.find("\r\n", pos);
      if (e == std::string::npos) e = head.size();
      const std::string line = head.substr(pos, e - pos);
      const size_t colon = line.find(':');
      if (colon != std::string::npos) req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
      pos = e + 2;
    }
    size_t body_len = 0;
    if (req.headers.count("content-length")) body_len = std::strtoul(req.headers["content-length"].c_str(), nullptr, 10);
    if (body_len > kMaxRequest) {
      queue(id, "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      if (conns_.count(id)) conns_.at(id).close_after_write = true;
      return;
    }
    if (c.in.size() < hdr_end + 4 + body_len) return;
    req.body = c.in.substr(hdr_end + 4, body_len);
    c.in.erase(0, hdr_end + 4 + body_len);

    const bool upgrade = lower(req.headers["upgrade"]) == "websocket";
    if (upgrade) {
      const std::string key = req.headers["sec-websocket-key"];
      if (key.empty() || !onWsOpen) {
        queue(id, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        if (conns_.count(id)) conns_.at(id).close_after_write = true;
        return;
      }
      c.ws = true;
      queue(id, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                    websocketAccept(key) + "\r\n\r\n");
      if (!conns_.count(id)) return;
      if (!onWsOpen(id, req)) {
        close(id);
        return;
      }
      if (conns_.count(id)) handleWs(id);
      return;
    }

    HttpResponse res = onHttp ? onHttp(req) : HttpResponse{404, "application/json", "{}", {}};
    std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + reason(res.status) + "\r\n";
    out += "Content-Type: " + res.content_type + "\r\n";
    out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\n";
    for (const auto& [k, v] : res.headers) out += k + ": " + v + "\r\n";
    const bool close_conn = lower(req.headers["connection"]) == "close";
    if (close_conn) out += "Connection: close\r\n";
    out += "\r\n" + res.body;
    queue(id, out);
    if (close_conn && conns_.count(id)) {
      conns_.at(id).close_after_write = true;
      if (conns_.at(id).out.empty()) drop(id);
      return;
    }
  }
}

void NetServer::handleWs(int id) {
  while (conns_.count(id)) {
    Conn& c = conns_.at(id);
    if (c.in.size() < 2) return;
    const uint8_t b0 = static_cast<uint8_t>(c.in[0]);
    const uint8_t b1 = static_cast<uint8_t>(c.in[1]);
    const bool fin = b0 & 0x80;
    const uint8_t op = b0 & 0x0F;
    const bool masked = b1 & 0x80;
    uint64_t len = b1 & 0x7F;
    size_t off = 2;
    if (len == 126) {
      if (c.in.size() < 4) return;
      len = (static_cast<uint8_t>(c.in[2]) << 8) | static_cast<uint8_t>(c.in[3]);
      off = 4;
    } else if (len == 127) {
      if (c.in.size() < 10) return;
      len = 0;
      for (int i = 0; i < 8; ++i) len = (len << 8) | static_cast<uint8_t>(c.in[2 + i]);
      off = 10;
    }
    if (!masked || len > kMaxWsMessage) {  // clients must mask (RFC 6455 5.1)
      drop(id);
      return;
    }
    if (c.in.size() < off + 4 + len) return;
    const std::string mask = c.in.substr(off, 4);
    std::string payload = c.in.substr(off + 4, len);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
    c.in.erase(0, off + 4 + len);

    switch (op) {
      case 0x0:  // continuation
      case 0x1:  // text
        c.fragments += payload;
        if (c.fragments.size() > kMaxWsMessage) {
          drop(id);
          return;
        }
        if (fin) {
          std::string msg;
          msg.swap(c.fragments);
          if (onWsText) onWsText(id, msg);
        }
        break;
      case 0x8:  // close
        queue(id, wsFrame(0x8, payload.substr(0, 2)));
        if (conns_.count(id)) {
          // The reply usually goes out at once; then nothing would ever call
          // writeTo() again and the close went unnoticed until the client's
          // FIN (the app's lease expired instead of being released).
          conns_.at(id).close_after_write = true;
          if (conns_.at(id).out.empty()) drop(id);
        }
        return;
      case 0x9:  // ping
        queue(id, wsFrame(0xA, payload));
        break;
      default:  // pong, binary: ignored
        break;
    }
  }
}

void NetServer::sendText(int conn, const std::string& text) {
  auto it = conns_.find(conn);
  if (it == conns_.end() || !it->second.ws || it->second.close_after_write) return;
  queue(conn, wsFrame(0x1, text));
}

void NetServer::close(int conn) {
  auto it = conns_.find(conn);
  if (it == conns_.end()) return;
  if (it->second.ws) {
    queue(conn, wsFrame(0x8, std::string("\x03\xe8", 2)));
    if (conns_.count(conn)) {
      conns_.at(conn).close_after_write = true;
      if (conns_.at(conn).out.empty()) drop(conn);
    }
  } else {
    drop(conn);
  }
}

void NetServer::drop(int id) {
  auto it = conns_.find(id);
  if (it == conns_.end()) return;
  const bool ws = it->second.ws;
  ::close(it->second.fd);
  conns_.erase(it);
  if (ws && onWsClose) onWsClose(id);
}

}  // namespace airo::host
