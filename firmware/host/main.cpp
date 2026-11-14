// airo-host: the laptop build of the Airo Core.
//
// Runs the same airo-core CommandService that ships on the ESP32-S3, with a
// VirtualHardwareAdapter instead of GPIO, and serves:
//   GET  /api/v1/info                 (no auth)
//   POST /api/v1/commands             (Bearer token)
//   GET  /api/v1/status, POST /api/v1/selftest, GET/PATCH /api/v1/config
//   WS   /api/v1/ws                   (token in first message)
//   WS   /bridge                      (the simulated Interface Board, Port C)
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "airo/api.h"
#include "airo/command_service.h"
#include "airo/json.h"
#include "net_server.h"
#include "virtual_hardware_adapter.h"

using namespace airo;
using airo::host::HttpRequest;
using airo::host::HttpResponse;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

struct SteadyClock : Clock {
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  uint64_t nowMs() override {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
  }
};

HttpResponse json(int status, const Json& body) { return HttpResponse{status, "application/json", body.dump(), {}}; }

HttpResponse error(ErrorCode code, const std::string& detail = {}, const std::string& request_id = {}) {
  return json(httpStatus(code), api::errorJson(request_id, code, detail));
}

void usage() {
  std::fprintf(stderr,
               "usage: airo-host [--port N] [--token T] [--device-id ID] [--set key=value ...] [--quiet]\n"
               "  --set accepts any PATCH /api/v1/config field, e.g. --set presets_require_height_feedback=true\n");
}

}  // namespace

int main(int argc, char** argv) {
  uint16_t port = 8080;
  std::string token = "airo-sim-token";
  Config cfg;
  bool quiet = false;
  Json patch = Json::object();

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--port") port = static_cast<uint16_t>(std::atoi(next().c_str()));
    else if (a == "--token") token = next();
    else if (a == "--device-id") cfg.device_id = next();
    else if (a == "--quiet") quiet = true;
    else if (a == "--set") {
      const std::string kv = next();
      const size_t eq = kv.find('=');
      if (eq == std::string::npos) {
        usage();
        return 2;
      }
      const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
      auto parsed = Json::parse(v);
      patch.set(k, parsed ? *parsed : Json(v));
    } else {
      usage();
      return 2;
    }
  }
  if (!patch.asObject().empty()) {
    std::string why;
    auto c = api::patchConfig(cfg, patch, &why);
    if (!c) {
      std::fprintf(stderr, "invalid --set: %s\n", why.c_str());
      return 2;
    }
    cfg = *c;
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGPIPE, SIG_IGN);

  SteadyClock clock;
  host::VirtualHardwareAdapter hw(clock, cfg.watchdog_period_ms);
  CommandService core(hw, clock, cfg);
  core.begin();  // no board yet: self-test fails, FAULT until the board says hello

  host::NetServer server;
  const int bound = server.listen(port);
  if (bound < 0) {
    std::fprintf(stderr, "airo-host: cannot listen on 127.0.0.1:%u: %s\n", port, std::strerror(errno));
    return 1;
  }
  std::printf("airo-host listening on http://127.0.0.1:%d (device %s)\n", bound, cfg.device_id.c_str());
  std::printf("AIRO_HOST_READY port=%d\n", bound);
  if (token == "airo-sim-token")
    std::printf("Using the default simulator token 'airo-sim-token'. The ESP32-S3 generates a random one at BLE setup.\n");
  std::fflush(stdout);

  int bridge_conn = -1;
  std::set<int> app_pending;          // WS clients that have not authenticated yet
  std::set<int> app_clients;          // authenticated WS clients
  uint64_t last_counter = ~0ull;
  uint64_t last_periodic = 0;

  auto log = [&](const char* fmt, auto... args) {
    if (quiet) return;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    std::printf(fmt, args...);
#pragma GCC diagnostic pop
    std::printf("\n");
    std::fflush(stdout);
  };

  auto online = [&]() { return hw.powered(); };

  auto infoCtx = [&]() {
    api::InfoContext ctx;
    ctx.hardware = "laptop-sim";
    ctx.adapter = "virtual";
    ctx.interface_case = hw.interfaceCase();
    ctx.height_feedback = hw.interfaceCase() == InterfaceCase::B;
    ctx.stop_method_verified = false;
    ctx.board_connected = hw.connected() && hw.helloReceived();
    return ctx;
  };

  auto broadcast = [&](const Json& j) {
    const std::string text = j.dump();
    for (int c : app_clients) server.sendText(c, text);
  };

  auto authorized = [&](const HttpRequest& r) {
    auto it = r.headers.find("authorization");
    return it != r.headers.end() && it->second == "Bearer " + token;
  };

  auto dropApps = [&]() {
    std::set<int> all = app_clients;
    all.insert(app_pending.begin(), app_pending.end());
    for (int c : all) server.close(c);
  };

  server.onHttp = [&](const HttpRequest& r) -> HttpResponse {
    HttpResponse cors;
    if (r.method == "OPTIONS") {
      cors.status = 204;
      cors.body.clear();
      cors.headers["Access-Control-Allow-Origin"] = "*";
      cors.headers["Access-Control-Allow-Headers"] = "Authorization, Content-Type";
      cors.headers["Access-Control-Allow-Methods"] = "GET, POST, PATCH, OPTIONS";
      return cors;
    }
    HttpResponse res = [&]() -> HttpResponse {
      if (r.path == "/api/v1/info" && r.method == "GET") {
        Json info = api::infoJson(core.config(), infoCtx());
        info.set("powered", online());
        return json(200, info);
      }
      if (r.path.rfind("/api/v1/", 0) != 0) return json(404, api::errorJson("", ErrorCode::BadRequest, "not found"));
      if (!authorized(r)) return error(ErrorCode::Unauthorized);
      if (!online()) {
        Json j = api::errorJson("", ErrorCode::NotReady, "powered_off");
        return json(503, j);
      }
      if (r.path == "/api/v1/status" && r.method == "GET") return json(200, api::statusJson(core.status()));
      if (r.path == "/api/v1/selftest" && r.method == "POST") {
        const bool ok = core.runSelfTest();
        Json j = api::statusJson(core.status());
        j.set("self_test", ok);
        return json(ok ? 200 : 423, j);
      }
      if (r.path == "/api/v1/config") {
        if (r.method == "GET") return json(200, api::configJson(core.config()));
        if (r.method == "PATCH") {
          auto body = Json::parse(r.body);
          if (!body) return error(ErrorCode::BadRequest, "body is not JSON");
          std::string why;
          auto c = api::patchConfig(core.config(), *body, &why);
          if (!c) return error(ErrorCode::BadRequest, why);
          const ErrorCode e = core.setConfig(*c, &why);
          if (e != ErrorCode::None) return error(e, why);
          return json(200, api::configJson(core.config()));
        }
      }
      if (r.path == "/api/v1/commands" && r.method == "POST") {
        auto body = Json::parse(r.body);
        if (!body) return error(ErrorCode::BadRequest, "body is not JSON");
        std::string why;
        auto env = api::parseEnvelope(*body, &why);
        if (!env) return error(ErrorCode::BadRequest, why, (*body)["request_id"].asString());
        env->client_id = "rest:" + env->source;
        const CommandResult result = core.submit(*env);
        Json out = api::ackJson(env->request_id, result);
        out.set("status", api::statusJson(core.status()));
        log("REST %s from %s -> %s%s", env->command.c_str(), env->source.c_str(), toString(result.error),
            result.duplicate ? " (duplicate)" : "");
        return json(httpStatus(result.error), out);
      }
      return json(405, api::errorJson("", ErrorCode::BadRequest, "method not allowed"));
    }();
    res.headers["Access-Control-Allow-Origin"] = "*";
    return res;
  };

  server.onWsOpen = [&](int conn, const HttpRequest& r) -> bool {
    if (r.path == "/bridge") {
      if (bridge_conn != -1) return false;  // one Interface Board
      bridge_conn = conn;
      hw.attach([&server, conn](const std::string& text) { server.sendText(conn, text); });
      log("bridge: interface board connected");
      return true;
    }
    if (r.path == "/api/v1/ws") {
      if (!online()) return false;
      app_pending.insert(conn);
      return true;
    }
    return false;
  };

  server.onWsClose = [&](int conn) {
    if (conn == bridge_conn) {
      bridge_conn = -1;
      hw.detach();
      core.begin();  // board gone: self-test fails -> FAULT
      log("bridge: interface board disconnected");
      return;
    }
    app_pending.erase(conn);
    if (app_clients.erase(conn)) core.clientGone("ws:" + std::to_string(conn));
  };

  server.onWsText = [&](int conn, const std::string& text) {
    auto msg = Json::parse(text);
    if (conn == bridge_conn) {
      if (!msg) return;
      const auto r = hw.onBridgeMessage(*msg);
      if (r == host::VirtualHardwareAdapter::BridgeResult::Hello ||
          r == host::VirtualHardwareAdapter::BridgeResult::CaseChanged) {
        core.begin();
        log("bridge: board hello, interface case %s, core rebooting", toString(*hw.interfaceCase()));
      } else if (r == host::VirtualHardwareAdapter::BridgeResult::PowerChanged) {
        if (hw.powered()) {
          core.begin();
          log("bridge: Airo power restored, core booting");
        } else {
          dropApps();
          log("bridge: Airo power cut");
        }
      }
      return;
    }
    if (app_pending.count(conn)) {
      if (!msg || (*msg)["type"].asString() != "auth" || (*msg)["token"].asString() != token) {
        server.sendText(conn, api::errorJson("", ErrorCode::Unauthorized).dump());
        server.close(conn);
        app_pending.erase(conn);
        return;
      }
      app_pending.erase(conn);
      app_clients.insert(conn);
      Json ok = Json::object();
      ok.set("type", "auth_ok");
      ok.set("info", api::infoJson(core.config(), infoCtx()));
      server.sendText(conn, ok.dump());
      server.sendText(conn, api::statusJson(core.status()).dump());
      return;
    }
    if (!app_clients.count(conn)) return;
    if (!msg) {
      server.sendText(conn, api::errorJson("", ErrorCode::BadRequest, "not JSON").dump());
      return;
    }
    std::string why;
    auto env = api::parseEnvelope(*msg, &why);
    if (!env) {
      server.sendText(conn, api::errorJson((*msg)["request_id"].asString(), ErrorCode::BadRequest, why).dump());
      return;
    }
    env->client_id = "ws:" + std::to_string(conn);
    const CommandResult res = core.submit(*env);
    server.sendText(conn, api::ackJson(env->request_id, res).dump());
    if (env->command == "desk.get_status") server.sendText(conn, api::statusJson(core.status()).dump());
    if (env->command != "desk.renew" || !res.accepted())
      log("WS %s from %s -> %s%s", env->command.c_str(), env->source.c_str(), toString(res.error),
          res.duplicate ? " (duplicate)" : "");
  };

  while (!g_stop) {
    server.poll(2);
    hw.service();
    if (!online()) continue;
    core.tick();
    for (const CoreEvent& e : core.takeEvents()) {
      log("event %s%s%s", toString(e.kind), e.direction ? " " : "", e.direction ? toString(*e.direction) : "");
      broadcast(api::eventJson(e));
    }
    const uint64_t now = clock.nowMs();
    const bool active = core.status().state == DeskState::AppActive;
    if (core.changeCounter() != last_counter || (active && now - last_periodic >= 500)) {
      last_counter = core.changeCounter();
      last_periodic = now;
      broadcast(api::statusJson(core.status()));
    }
  }
  std::printf("airo-host: stopping\n");
  return 0;
}
