// SPDX-License-Identifier: GPL-2.0-or-later
#include "frame-protocol.hpp"
#include <cstring>
namespace vban {
void FrameAssembler::discard() {
    if (active_) ++counters_.incomplete;
    active_ = false;
    bytes_.clear();
}
void FrameAssembler::expire(uint64_t now) {
    if (active_ && now - started_ > frame_timeout_ns) discard();
}
std::optional<std::vector<uint8_t>> FrameAssembler::push(const uint8_t *data, size_t size, uint64_t now) {
    expire(now);
    ++counters_.packets;
    if (!data || size <= header_size || size > max_datagram || std::memcmp(data, "VBAN", 4) ||
        (data[4] & 0xe0) != 0x80 || (data[4] & 0x1f) >= 25 || (data[7] & 0xf8)) {
        ++counters_.invalid; discard(); return {};
    }
    const unsigned flags = data[7];
    // First+last is a single-packet image. Some senders mark next+last together.
    if (flags != 1 && flags != 2 && flags != 4 && flags != 5 && flags != 6) {
        ++counters_.invalid; discard(); return {};
    }
    const uint16_t index = uint16_t(data[5]) | (uint16_t(data[6]) << 8);
    const uint32_t frame = uint32_t(data[24]) | (uint32_t(data[25]) << 8) |
        (uint32_t(data[26]) << 16) | (uint32_t(data[27]) << 24);
    if (have_completed_ && now - completed_at_ < frame_timeout_ns &&
        (frame == completed_frame_ || (frame - completed_frame_) >= 0x80000000U)) {
        ++counters_.duplicates; return {};
    }
    // A delayed start is just as old as any other delayed fragment.
    if (active_ && frame != frame_ && (frame - frame_) >= 0x80000000U) {
        ++counters_.duplicates; return {};
    }
    if (flags & 1) {
        if (index != 0) { ++counters_.invalid; discard(); return {}; }
        if (active_ && frame == frame_) { ++counters_.duplicates; return {}; }
        discard();
        active_ = true; frame_ = frame; next_ = 0; started_ = now;
    }
    if (!active_) return {};
    if (frame != frame_) {
        // An old fragment cannot cancel a newer image in progress.
        if ((frame - frame_) >= 0x80000000U) { ++counters_.duplicates; return {}; }
        discard(); return {};
    }
    if (index != next_) {
        if (index < next_) { ++counters_.duplicates; return {}; }
        discard(); return {};
    }
    if (size - header_size > max_image_bytes - bytes_.size()) {
        ++counters_.invalid; discard(); return {};
    }
    bytes_.insert(bytes_.end(), data + header_size, data + size);
    ++next_;
    if (!(flags & 4)) return {};
    active_ = false;
    have_completed_ = true; completed_frame_ = frame; completed_at_ = now;
    ++counters_.completed;
    return std::move(bytes_);
}
}
