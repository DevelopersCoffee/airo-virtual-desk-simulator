// airo-host: the laptop build of the Airo Core.
//
// Runs the same airo-core CommandService that ships on the ESP32-S3, with a
// VirtualHardwareAdapter instead of GPIO, and serves:
//   GET  /api/v1/info                 (no auth)
//   POST /api/v1/commands             (Bearer token)
//   GET  /api/v1/status, POST /api/v1/selftest, GET/PATCH /api/v1/config
//   WS   /api/v1/ws                   (token in first message)
//   WS   /bridge                      (the simulated Interface Board, Port C)
//
// With --adapter esp32 it runs Esp32HardwareAdapter (the board's adapter,
// pin-level, bench mode, in-memory GPIO) and serves WS /bench instead of
// /bridge: exactly what the ESP32-S3 serves, so the hardware-in-the-loop
// suite can be run without the board.
#include <chrono>
#include <thread>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "airo/api.h"
#include "airo/app_server.h"
#include "airo/board/bench_link.h"
#include "airo/board/esp32_hardware_adapter.h"
#include "airo/command_service.h"
#include "airo/json.h"
#include "host_io.h"
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

void usage() {
  std::fprintf(stderr,
               "usage: airo-host [--port N] [--token T] [--device-id ID] [--adapter virtual|esp32] [--set key=value ...] [--quiet]\n"
               "  --set accepts any PATCH /api/v1/config field, e.g. --set presets_require_height_feedback=true\n");
}

}  // namespace

int main(int argc, char** argv) {
  uint16_t port = 8080;
  std::string token = "airo-sim-token";
  Config cfg;
  bool quiet = false;
  std::string adapter = "virtual";
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
    else if (a == "--adapter") {
      adapter = next();
      if (adapter != "virtual" && adapter != "esp32") {
        usage();
        return 2;
      }
    }
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
  const bool esp32 = adapter == "esp32";
  host::VirtualHardwareAdapter vhw(clock, cfg.watchdog_period_ms);
  // Board adapter on in-memory pins: bench mode, desk mode not compiled in.
  host::HostGpio gpio;
  host::HostUart htx, dtx;
  board::BenchIo io(gpio, &htx, &dtx, board::defaultPinMap(), false);
  board::Esp32HardwareAdapter bhw(io, clock, InterfaceCase::A2, cfg.watchdog_period_ms);
  board::BenchLink bench(io, bhw);
  HardwareAdapter& hw = esp32 ? static_cast<HardwareAdapter&>(bhw) : static_cast<HardwareAdapter&>(vhw);
  CommandService core(hw, clock, cfg);
  core.begin();  // no board yet: self-test fails, FAULT until the board says hello

  host::NetServer server;
  const int bound = server.listen(port);
  if (bound < 0) {
    std::fprintf(stderr, "airo-host: cannot listen on 127.0.0.1:%u: %s\n", port, std::strerror(errno));
    return 1;
  }
  std::printf("airo-host listening on http://127.0.0.1:%d (device %s, %s adapter)\n", bound, cfg.device_id.c_str(),
              adapter.c_str());
  std::printf("AIRO_HOST_READY port=%d\n", bound);
  if (token == "airo-sim-token")
    std::printf("Using the default simulator token 'airo-sim-token'. The ESP32-S3 generates a random one at BLE setup.\n");
  std::fflush(stdout);

  int link_conn = -1;  // the Interface Board (/bridge) or the bench link (/bench)
  bool halted = false;  // bench "halt": the core stops as if it crashed

  auto log = [&](const std::string& line) {
    if (quiet) return;
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
  };

  auto online = [&]() { return esp32 ? bhw.powered() : vhw.powered(); };

  AppServer::Hooks hooks;
  hooks.send = [&server](int c, const std::string& t) { server.sendText(c, t); };
  hooks.close = [&server](int c) { server.close(c); };
  hooks.online = online;
  hooks.log = log;
  hooks.info = [&]() {
    api::InfoContext ctx;
    ctx.hardware = "laptop-sim";
    ctx.stop_method_verified = false;
    if (esp32) {
      ctx.adapter = "esp32";
      ctx.interface_case = bench.helloReceived() ? std::optional(bhw.interfaceCase()) : std::nullopt;
      ctx.height_feedback = bhw.interfaceCase() == InterfaceCase::B;
      ctx.board_connected = bench.helloReceived();
    } else {
      ctx.adapter = "virtual";
      ctx.interface_case = vhw.interfaceCase();
      ctx.height_feedback = vhw.interfaceCase() == InterfaceCase::B;
      ctx.board_connected = vhw.connected() && vhw.helloReceived();
    }
    return ctx;
  };
  hooks.extend_info = [&](Json& info) {
    if (!esp32) return;
    info.set("bench", bench.state());
  };
  AppServer app(core, clock, token, hooks);

  server.onHttp = [&](const HttpRequest& r) -> HttpResponse {
    HttpResponse res;
    if (r.method == "OPTIONS") {
      res.status = 204;
      res.body.clear();
      res.headers["Access-Control-Allow-Origin"] = "*";
      res.headers["Access-Control-Allow-Headers"] = "Authorization, Content-Type";
      res.headers["Access-Control-Allow-Methods"] = "GET, POST, PATCH, OPTIONS";
      return res;
    }
    auto it = r.headers.find("authorization");
    const ApiResponse a = app.http({r.method, r.path, it != r.headers.end() ? it->second : "", r.body});
    res.status = a.status;
    res.body = a.body;
    res.headers["Access-Control-Allow-Origin"] = "*";
    return res;
  };

  server.onWsOpen = [&](int conn, const HttpRequest& r) -> bool {
    if (r.path == (esp32 ? "/bench" : "/bridge")) {
      if (link_conn != -1) return false;  // one Interface Board
      auto send = [&server, conn](const std::string& text) { server.sendText(conn, text); };
      if (esp32) {
        std::string why;
        if (!bench.open(send, &why)) {
          log("bench: refused: " + why);
          return false;
        }
      } else {
        vhw.attach(send);
      }
      link_conn = conn;
      log(esp32 ? "bench: link connected" : "bridge: interface board connected");
      return true;
    }
    if (r.path == "/api/v1/ws") return app.wsOpen(conn);
    return false;
  };

  server.onWsClose = [&](int conn) {
    if (conn == link_conn) {
      link_conn = -1;
      if (esp32) bench.close();
      else vhw.detach();
      core.begin();  // board gone: self-test fails -> FAULT
      log(esp32 ? "bench: link disconnected" : "bridge: interface board disconnected");
      return;
    }
    app.wsClose(conn);
  };

  auto powerChanged = [&]() {
    if (online()) {
      core.begin();
      log("bridge: Airo power restored, core booting");
    } else {
      app.dropAll();
      log("bridge: Airo power cut");
    }
  };

  server.onWsText = [&](int conn, const std::string& text) {
    if (conn == link_conn) {
      auto msg = Json::parse(text);
      if (!msg) return;
      if (esp32) {
        using R = board::BenchLink::Result;
        switch (bench.onMessage(*msg)) {
          case R::Hello:
            halted = false;
            core.begin();
            log(std::string("bench: hello, interface case ") + toString(bhw.interfaceCase()) + ", core rebooting");
            break;
          case R::PowerChanged: powerChanged(); break;
          case R::Hang: std::this_thread::sleep_for(std::chrono::milliseconds(bench.hangMs())); break;
          case R::Halt:
            halted = true;
            log("bench: halted");
            break;
          case R::Reboot:
            halted = false;
            bhw.setPowered(true);
            app.dropAll();
            core.begin();
            log("bench: reboot");
            break;
          case R::Invalid: log("bench: invalid message " + text); break;
          case R::None: break;
        }
        return;
      }
      const auto r = vhw.onBridgeMessage(*msg);
      if (r == host::VirtualHardwareAdapter::BridgeResult::Hello ||
          r == host::VirtualHardwareAdapter::BridgeResult::CaseChanged) {
        core.begin();
        log(std::string("bridge: board hello, interface case ") + toString(*vhw.interfaceCase()) + ", core rebooting");
      } else if (r == host::VirtualHardwareAdapter::BridgeResult::PowerChanged) {
        powerChanged();
      }
      return;
    }
    app.wsText(conn, text);
  };

  while (!g_stop) {
    server.poll(2);
    if (halted) continue;
    if (esp32) bhw.service();
    else vhw.service();
    if (!online()) continue;
    core.tick();
    app.loop();
  }
  std::printf("airo-host: stopping\n");
  return 0;
}
