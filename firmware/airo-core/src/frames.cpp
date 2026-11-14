#include "airo/frames.h"

namespace airo::frames {

std::vector<uint8_t> encode(uint8_t addr, uint8_t cmd, const std::vector<uint8_t>& params) {
  std::vector<uint8_t> out{addr, addr, cmd, static_cast<uint8_t>(params.size())};
  uint32_t sum = cmd + static_cast<uint32_t>(params.size());
  for (uint8_t p : params) {
    out.push_back(p);
    sum += p;
  }
  out.push_back(static_cast<uint8_t>(sum & 0xFF));
  out.push_back(kEnd);
  return out;
}

std::vector<uint8_t> handset(uint8_t cmd) { return encode(kHandsetAddr, cmd); }

std::vector<uint8_t> heightReport(uint16_t mm) {
  return encode(kControllerAddr, kReportHeight,
                {static_cast<uint8_t>(mm >> 8), static_cast<uint8_t>(mm & 0xFF), 0x00});
}

std::vector<Frame> Decoder::feed(const std::vector<uint8_t>& bytes) {
  std::vector<Frame> out;
  buf_.insert(buf_.end(), bytes.begin(), bytes.end());
  size_t i = 0;
  while (true) {
    // Find a start: two equal address bytes.
    while (i + 1 < buf_.size() &&
           !((buf_[i] == kHandsetAddr || buf_[i] == kControllerAddr) && buf_[i + 1] == buf_[i]))
      ++i;
    if (i + 4 > buf_.size()) break;
    const uint8_t len = buf_[i + 3];
    const size_t total = 4u + len + 2u;
    if (len > 16) {  // nonsense length: skip this start
      ++bad_;
      ++i;
      continue;
    }
    if (i + total > buf_.size()) break;
    uint32_t sum = buf_[i + 2] + len;
    for (size_t k = 0; k < len; ++k) sum += buf_[i + 4 + k];
    if (buf_[i + 4 + len] != static_cast<uint8_t>(sum & 0xFF) || buf_[i + 5 + len] != kEnd) {
      ++bad_;
      ++i;
      continue;
    }
    Frame f{buf_[i], buf_[i + 2], {}};
    f.params.assign(buf_.begin() + static_cast<long>(i + 4), buf_.begin() + static_cast<long>(i + 4 + len));
    out.push_back(std::move(f));
    i += total;
  }
  buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(i));
  if (buf_.size() > 256) buf_.clear();
  return out;
}

std::optional<uint16_t> heightOf(const Frame& f) {
  if (f.addr != kControllerAddr || f.cmd != kReportHeight || f.params.size() < 2) return std::nullopt;
  return static_cast<uint16_t>((f.params[0] << 8) | f.params[1]);
}

}  // namespace airo::frames
