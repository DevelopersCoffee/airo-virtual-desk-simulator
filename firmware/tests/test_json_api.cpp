#include "airo/api.h"
#include "airo/frames.h"
#include "airo/json.h"
#include "net_server.h"
#include "test.h"

using namespace airo;

TEST("json: parse and dump round trip") {
  auto j = Json::parse(R"({"a":1,"b":[true,false,null],"c":"x\"yé","d":-2.5})");
  CHECK(j.has_value());
  CHECK_EQ((*j)["a"].asNumber(), 1.0);
  CHECK((*j)["b"].asArray().size() == 3);
  CHECK((*j)["c"].asString() == "x\"y\xc3\xa9");
  CHECK_EQ((*j)["d"].asNumber(), -2.5);
  auto again = Json::parse(j->dump());
  CHECK(again.has_value() && again->dump() == j->dump());
}

TEST("json: rejects malformed input") {
  for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "tru", "{\"a\":1}x", "\"\x01\"", "01a"})
    CHECK(!Json::parse(bad).has_value());
}

TEST("api: envelope requires the four fields and a known source") {
  std::string why;
  auto ok = api::parseEnvelope(*Json::parse(
      R"({"device_id":"airo-desk-001","command":"desk.up","source":"mobile_app","request_id":"r1","lease_ms":400})"));
  CHECK(ok.has_value() && ok->lease_ms == 400u);
  CHECK(!api::parseEnvelope(*Json::parse(R"({"command":"desk.up","source":"test","request_id":"r"})"), &why));
  CHECK(!api::parseEnvelope(*Json::parse(R"({"device_id":"d","command":"desk.up","source":"x","request_id":"r"})")));
  CHECK(!api::parseEnvelope(
      *Json::parse(R"({"device_id":"d","command":"desk.up","source":"test","request_id":"r","lease_ms":-5})")));
  CHECK(!api::parseEnvelope(*Json::parse(R"({"command":"move","direction":"up"})")));
}

TEST("api: config patch validates bounds and rejects unknown fields") {
  Config c;
  std::string why;
  auto p = api::patchConfig(c, *Json::parse(R"({"presets_require_height_feedback":true,"preset_mode":"hold_to_run"})"), &why);
  CHECK(p.has_value() && p->presets_require_height_feedback && p->preset_mode == PresetMode::HoldToRun);
  CHECK(!api::patchConfig(c, *Json::parse(R"({"lease_max_ms":9000})"), &why));
  CHECK(!api::patchConfig(c, *Json::parse(R"({"device_id":"other"})"), &why));
  CHECK(!api::patchConfig(c, *Json::parse(R"({"presets_require_height_feedback":"yes"})"), &why));
}

TEST("api: status json uses null height without feedback") {
  Status s;
  s.state = DeskState::Idle;
  Json j = api::statusJson(s);
  CHECK(j["state"].asString() == "IDLE");
  CHECK(j["height_mm"].isNull());
  CHECK(j["type"].asString() == "status");
}

TEST("frames: encode matches the reported handset frames") {
  const std::vector<uint8_t> up{0xF1, 0xF1, 0x01, 0x00, 0x01, 0x7E};
  CHECK(frames::handset(frames::kCmdUp) == up);
  const std::vector<uint8_t> stop{0xF1, 0xF1, 0x2B, 0x00, 0x2B, 0x7E};
  CHECK(frames::handset(frames::kCmdStop) == stop);
}

TEST("frames: decoder parses a height report across chunks and noise") {
  // Spec 13.4 prints CK=0x14 for this frame, but its own formula
  // (cmd + len + params) gives 0x16. The formula is used here.
  frames::Decoder d;
  CHECK(d.feed({0x00, 0xF2, 0xF2, 0x01, 0x03}).empty());
  auto fs = d.feed({0x05, 0x06, 0x07, 0x16, 0x7E});
  CHECK(fs.size() == 1);
  CHECK(frames::heightOf(fs.at(0)) == std::optional<uint16_t>(1286));
  auto bad = d.feed({0xF2, 0xF2, 0x01, 0x03, 0x05, 0x06, 0x07, 0x14, 0x7E});  // bad checksum
  CHECK(bad.empty());
  CHECK(d.badFrames() >= 1);
  auto hr = d.feed(frames::heightReport(740));
  CHECK(hr.size() == 1 && frames::heightOf(hr[0]) == std::optional<uint16_t>(740));
}

TEST("websocket: accept key matches RFC 6455 example") {
  CHECK(host::websocketAccept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  CHECK(host::base64("") == "");
  CHECK(host::base64("ab") == "YWI=");
}
