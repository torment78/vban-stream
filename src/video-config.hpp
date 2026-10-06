// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace vban {
constexpr size_t video_slot_count = 2;
struct VideoConfig {
    bool enabled = false;
    std::string label, sender_ip, stream_name;
    bool operator==(const VideoConfig &other) const {
        return enabled == other.enabled && sender_ip == other.sender_ip && stream_name == other.stream_name;
    }
};
using VideoConfigs = std::array<VideoConfig, video_slot_count>;
// The audio socket only routes and queues packets. Image decoding must not run here.
class VideoSink {
public:
    virtual ~VideoSink() = default;
    virtual void configure(const VideoConfigs &, uint16_t port) = 0;
    virtual void enqueue(size_t slot, const uint8_t *bytes, size_t size, uint64_t now) = 0;
};
}
