// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video-config.hpp"
#include <memory>
#include <mutex>
#include <vector>
namespace vban {
struct MouseConfig {
    bool enabled = false;
    std::string destination_ip;
    uint16_t port = 6980;
    std::string stream_name = "Command1";
};
using MouseConfigs = std::array<MouseConfig, video_slot_count>;
enum class MouseAction { move, left_down, left_up, right_down, right_up };
std::vector<uint8_t> mouse_packet(const std::string &name, uint32_t sequence, MouseAction action, int x, int y);
struct MouseStatus {
    bool enabled = false, dragging = false;
    uint64_t packets = 0, errors = 0;
    std::string error, local_ip;
};
// Nonblocking, small UDP writes on the UI thread; never touches the audio callback.
class MouseReturn {
public:
    struct Routing;
    using Prepared = std::shared_ptr<Routing>;
    MouseReturn();
    ~MouseReturn();
    Prepared prepare(const MouseConfigs &, const std::string &local_ip, std::string &error);
    void activate(Prepared);
    bool press(size_t slot, bool right, int x, int y);
    bool move(size_t slot, int x, int y);
    void release();
    bool dragging() const;
    MouseStatus status(size_t slot) const;
private:
    bool send(size_t slot, MouseAction action, int x, int y);
    void release_locked();
    mutable std::mutex mutex_;
    Prepared routing_;
    std::array<MouseStatus, video_slot_count> status_{};
    std::array<uint32_t, video_slot_count> sequences_{};
    int held_slot_ = -1, x_ = 0, y_ = 0;
    bool right_ = false;
};
}
