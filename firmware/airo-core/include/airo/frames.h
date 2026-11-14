// Jiecang-style UART frames (spec section 13). [COMMUNITY], unverified on this
// desk: the real board replays frames captured from the real keypad instead.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace airo::frames {

constexpr uint8_t kHandsetAddr = 0xF1;     // handset -> controller
constexpr uint8_t kControllerAddr = 0xF2;  // controller -> handset
constexpr uint8_t kEnd = 0x7E;

constexpr uint8_t kCmdUp = 0x01;
constexpr uint8_t kCmdDown = 0x02;
constexpr uint8_t kCmdMemory1 = 0x05;  // used for desk.sitting [ASSUMPTION]
constexpr uint8_t kCmdMemory2 = 0x06;  // used for desk.standing [ASSUMPTION]
constexpr uint8_t kCmdStop = 0x2B;
constexpr uint8_t kReportHeight = 0x01;

struct Frame {
  uint8_t addr;
  uint8_t cmd;
  std::vector<uint8_t> params;
};

std::vector<uint8_t> encode(uint8_t addr, uint8_t cmd, const std::vector<uint8_t>& params = {});
std::vector<uint8_t> handset(uint8_t cmd);
std::vector<uint8_t> heightReport(uint16_t mm);

/// Incremental decoder: feed bytes, get complete frames with a valid checksum.
class Decoder {
 public:
  std::vector<Frame> feed(const std::vector<uint8_t>& bytes);
  uint32_t badFrames() const { return bad_; }

 private:
  std::vector<uint8_t> buf_;
  uint32_t bad_ = 0;
};

std::optional<uint16_t> heightOf(const Frame& f);

}  // namespace airo::frames
