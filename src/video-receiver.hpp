// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "frame-protocol.hpp"
#include "video-config.hpp"
#include <QImage>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace vban {
constexpr uint64_t video_stale_ns = 3000000000ULL;
struct VideoStatus {
    bool enabled = false, receiving = false;
    int width = 0, height = 0;
    uint64_t decoded = 0, decode_errors = 0, queue_drops = 0;
    FrameCounters packets;
    std::string format, error;
};
struct VideoSnapshot { QImage image; uint64_t serial = 0, timestamp = 0; };
class VideoReceiver final : public VideoSink {
public:
    explicit VideoReceiver(std::function<uint64_t()> clock);
    ~VideoReceiver() override;
    void shutdown();
    void configure(const VideoConfigs &, uint16_t port) override;
    void enqueue(size_t slot, const uint8_t *, size_t, uint64_t now) override;
    VideoStatus status(size_t slot) const;
    VideoSnapshot snapshot(int slot) const;
private:
    struct Datagram {
        size_t slot = 0, size = 0;
        uint64_t generation = 0, timestamp = 0;
        std::array<uint8_t, max_datagram> bytes{};
    };
    struct Slot {
        VideoConfig config;
        uint64_t generation = 0;
        VideoSnapshot latest;
        VideoStatus status;
    };
    void run();
    std::function<uint64_t()> clock_;
    mutable std::mutex state_mutex_;
    std::array<Slot, video_slot_count> slots_;
    std::array<std::atomic<uint64_t>, video_slot_count> generations_{}, drops_{};
    uint16_t port_ = 0;
    uint64_t serial_ = 0;
    std::mutex queue_mutex_;
    std::condition_variable wake_;
    std::deque<Datagram> queue_;
    std::atomic<bool> stop_{false};
    std::thread worker_;
};
}
