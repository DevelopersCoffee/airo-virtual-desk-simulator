// JSON mapping of the local API (spec section 10). Shared by the laptop host
// and the ESP32-S3 transport layer so both speak exactly the same schema.
#pragma once

#include <optional>
#include <string>

#include "airo/command_service.h"
#include "airo/json.h"

namespace airo::api {

constexpr const char* kFirmwareVersion = "0.3.0-sim";
constexpr int kApiVersion = 1;

/// Parses a command envelope. Returns an error message on a malformed one.
std::optional<CommandEnvelope> parseEnvelope(const Json& j, std::string* why = nullptr);

Json statusJson(const Status& s);
Json eventJson(const CoreEvent& e);
Json ackJson(const std::string& request_id, const CommandResult& r);
Json errorJson(const std::string& request_id, ErrorCode code, const std::string& detail = {});
Json configJson(const Config& c);
/// Applies the fields present in `patch` on top of `base`.
std::optional<Config> patchConfig(const Config& base, const Json& patch, std::string* why);

struct InfoContext {
  std::string hardware;  // "esp32-s3" | "laptop-sim"
  std::string adapter;   // "esp32" | "virtual"
  std::optional<InterfaceCase> interface_case;
  bool height_feedback = false;
  bool stop_method_verified = false;
  bool board_connected = false;
};
Json infoJson(const Config& c, const InfoContext& ctx);

}  // namespace airo::api
