#include "airo/app_server.h"

#include <utility>

namespace airo {

namespace {
ApiResponse json(int status, const Json& body) { return ApiResponse{status, body.dump()}; }

ApiResponse error(ErrorCode code, const std::string& detail = {}, const std::string& request_id = {}) {
  return json(httpStatus(code), api::errorJson(request_id, code, detail));
}

std::string suffix(const CommandResult& r) { return r.duplicate ? " (duplicate)" : ""; }
}  // namespace

AppServer::AppServer(CommandService& core, Clock& clock, std::string token, Hooks hooks)
    : core_(core), clock_(clock), token_(std::move(token)), hooks_(std::move(hooks)) {}

void AppServer::log(const std::string& line) {
  if (hooks_.log) hooks_.log(line);
}

Json AppServer::info() {
  Json info = api::infoJson(core_.config(), hooks_.info ? hooks_.info() : api::InfoContext{});
  info.set("powered", hooks_.online ? hooks_.online() : true);
  if (hooks_.extend_info) hooks_.extend_info(info);
  return info;
}

ApiResponse AppServer::http(const ApiRequest& r) {
  if (r.path == "/api/v1/info" && r.method == "GET") return json(200, info());
  if (r.path.rfind("/api/v1/", 0) != 0) return json(404, api::errorJson("", ErrorCode::BadRequest, "not found"));
  if (r.authorization != "Bearer " + token_) return error(ErrorCode::Unauthorized);
  if (hooks_.online && !hooks_.online()) return json(503, api::errorJson("", ErrorCode::NotReady, "powered_off"));
  if (r.path == "/api/v1/status" && r.method == "GET") return json(200, api::statusJson(core_.status()));
  if (r.path == "/api/v1/selftest" && r.method == "POST") {
    const bool ok = core_.runSelfTest();
    Json j = api::statusJson(core_.status());
    j.set("self_test", ok);
    return json(ok ? 200 : 423, j);
  }
  if (r.path == "/api/v1/config") {
    if (r.method == "GET") return json(200, api::configJson(core_.config()));
    if (r.method == "PATCH") {
      auto body = Json::parse(r.body);
      if (!body) return error(ErrorCode::BadRequest, "body is not JSON");
      std::string why;
      auto c = api::patchConfig(core_.config(), *body, &why);
      if (!c) return error(ErrorCode::BadRequest, why);
      const ErrorCode e = core_.setConfig(*c, &why);
      if (e != ErrorCode::None) return error(e, why);
      return json(200, api::configJson(core_.config()));
    }
  }
  if (r.path == "/api/v1/commands" && r.method == "POST") {
    auto body = Json::parse(r.body);
    if (!body) return error(ErrorCode::BadRequest, "body is not JSON");
    std::string why;
    auto env = api::parseEnvelope(*body, &why);
    if (!env) return error(ErrorCode::BadRequest, why, (*body)["request_id"].asString());
    env->client_id = "rest:" + env->source;
    const CommandResult result = core_.submit(*env);
    Json out = api::ackJson(env->request_id, result);
    out.set("status", api::statusJson(core_.status()));
    log("REST " + env->command + " from " + env->source + " -> " + toString(result.error) + suffix(result));
    return json(httpStatus(result.error), out);
  }
  return json(405, api::errorJson("", ErrorCode::BadRequest, "method not allowed"));
}

bool AppServer::wsOpen(int conn) {
  if (hooks_.online && !hooks_.online()) return false;
  pending_.insert(conn);
  return true;
}

void AppServer::wsText(int conn, const std::string& text) {
  auto msg = Json::parse(text);
  if (pending_.count(conn)) {
    pending_.erase(conn);
    if (!msg || (*msg)["type"].asString() != "auth" || (*msg)["token"].asString() != token_) {
      hooks_.send(conn, api::errorJson("", ErrorCode::Unauthorized).dump());
      hooks_.close(conn);
      return;
    }
    clients_.insert(conn);
    Json ok = Json::object();
    ok.set("type", "auth_ok");
    ok.set("info", api::infoJson(core_.config(), hooks_.info ? hooks_.info() : api::InfoContext{}));
    hooks_.send(conn, ok.dump());
    hooks_.send(conn, api::statusJson(core_.status()).dump());
    return;
  }
  if (!clients_.count(conn)) return;
  if (!msg) {
    hooks_.send(conn, api::errorJson("", ErrorCode::BadRequest, "not JSON").dump());
    return;
  }
  std::string why;
  auto env = api::parseEnvelope(*msg, &why);
  if (!env) {
    hooks_.send(conn, api::errorJson((*msg)["request_id"].asString(), ErrorCode::BadRequest, why).dump());
    return;
  }
  env->client_id = "ws:" + std::to_string(conn);
  const CommandResult res = core_.submit(*env);
  hooks_.send(conn, api::ackJson(env->request_id, res).dump());
  if (env->command == "desk.get_status") hooks_.send(conn, api::statusJson(core_.status()).dump());
  if (env->command != "desk.renew" || !res.accepted())
    log("WS " + env->command + " from " + env->source + " -> " + toString(res.error) + suffix(res));
}

void AppServer::wsClose(int conn) {
  pending_.erase(conn);
  if (clients_.erase(conn)) core_.clientGone("ws:" + std::to_string(conn));
}

void AppServer::dropAll() {
  std::set<int> all = clients_;
  all.insert(pending_.begin(), pending_.end());
  for (int c : all) hooks_.close(c);
}

void AppServer::broadcast(const Json& j) {
  const std::string text = j.dump();
  for (int c : clients_) hooks_.send(c, text);
}

void AppServer::loop() {
  for (const CoreEvent& e : core_.takeEvents()) {
    log(std::string("event ") + toString(e.kind) + (e.direction ? std::string(" ") + toString(*e.direction) : ""));
    broadcast(api::eventJson(e));
  }
  const uint64_t now = clock_.nowMs();
  const bool active = core_.status().state == DeskState::AppActive;
  if (core_.changeCounter() != last_counter_ || (active && now - last_periodic_ >= 500)) {
    last_counter_ = core_.changeCounter();
    last_periodic_ = now;
    broadcast(api::statusJson(core_.status()));
  }
}

}  // namespace airo
