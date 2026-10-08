// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
namespace vban {
struct FrameOutputConfig {
    bool enabled = false, adaptive_jpeg = true;
    std::string destination_ip, local_ip, stream_name = "OBS-PROGRAM", format = "JPEG";
    uint16_t port = 6980;
    int max_width = 1280, max_height = 720, fps = 15, quality = 80, mbps = 24;
};
}
