// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "frame-sender.hpp"
#include <obs.h>
namespace vban {
class ProgramOutput {
public:
    using Prepared = FrameSender::Prepared;
    ~ProgramOutput();
    Prepared prepare(const FrameOutputConfig &,std::string &error);
    void activate(Prepared);
    void shutdown();
    FrameOutputStatus status() const { return sender_.status(); }
private:
    static void capture(void *,video_data *);
    FrameSender sender_;
    bool connected_ = false;
};
}
