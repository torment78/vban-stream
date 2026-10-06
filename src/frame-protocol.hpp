// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "vban-protocol.hpp"
#include <optional>
namespace vban {
constexpr size_t max_image_bytes = 48 * 1024 * 1024;
constexpr uint64_t frame_timeout_ns = 2000000000ULL;
struct FrameCounters {
    uint64_t packets = 0, completed = 0, incomplete = 0, invalid = 0, duplicates = 0;
};
// VB-Audio VBAN specification revision 13, pp. 23-25. A counter identifies an
// image; the little-endian 16-bit index orders its fragments. No partial display.
class FrameAssembler {
public:
    std::optional<std::vector<uint8_t>> push(const uint8_t *, size_t, uint64_t now);
    void expire(uint64_t now);
    const FrameCounters &counters() const { return counters_; }
private:
    void discard();
    bool active_ = false, have_completed_ = false;
    uint32_t frame_ = 0, completed_frame_ = 0;
    uint16_t next_ = 0;
    uint64_t started_ = 0, completed_at_ = 0;
    std::vector<uint8_t> bytes_;
    FrameCounters counters_;
};
}
