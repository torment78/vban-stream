// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "frame-output-config.hpp"
#include <atomic>
#include <memory>
#include <utility>
namespace vban {
struct FrameOutputStatus {
    bool enabled = false, sending = false;
    int width = 0, height = 0;
    uint64_t frames = 0, packets = 0, bytes = 0, dropped = 0, errors = 0, unchanged = 0;
    double fps = 0, mbps = 0, encode_ms = 0, send_ms = 0, age_ms = 0;
    int jpeg_quality = 0;
    std::string local_ip, error;
};
class FrameSender {
public:
    struct Session;
    using Prepared = std::shared_ptr<Session>;
    FrameSender();
    ~FrameSender();
    Prepared prepare(const FrameOutputConfig &, uint32_t source_width, uint32_t source_height, std::string &error);
    // Caller must disconnect/quiesce its producer before activate or shutdown.
    void activate(Prepared);
    void shutdown();
    void capture(const uint8_t *rgba, uint32_t stride, uint64_t timestamp) noexcept;
    FrameOutputStatus status() const;
    static int frame_rate(const Prepared &);
    static std::pair<int,int> dimensions(const Prepared &);
private:
    Prepared active_;
    std::shared_ptr<std::atomic<uint32_t>> sequence_;
};
}
