// SPDX-License-Identifier: GPL-2.0-or-later
#include "program-output.hpp"
#include <algorithm>
namespace vban {
ProgramOutput::~ProgramOutput() { shutdown(); }
ProgramOutput::Prepared ProgramOutput::prepare(const FrameOutputConfig &cfg,std::string &error) {
    obs_video_info info{};
    if(cfg.enabled) {
        if(!obs_get_video_info(&info)||!obs_get_video()) {error="OBS to VBAN Frame: OBS video is not ready.";return {};}
        if(info.colorspace==VIDEO_CS_2100_PQ||info.colorspace==VIDEO_CS_2100_HLG) {
            error="OBS to VBAN Frame currently requires an SDR OBS video colour space.";return {};
        }
    }
    return sender_.prepare(cfg,info.output_width,info.output_height,error);
}
void ProgramOutput::activate(Prepared next) {
    shutdown();const auto [width,height]=FrameSender::dimensions(next);const auto fps=FrameSender::frame_rate(next);sender_.activate(std::move(next));
    if(width&&height) {
        video_scale_info conversion{};conversion.format=VIDEO_FORMAT_RGBA;conversion.width=uint32_t(width);conversion.height=uint32_t(height);
        conversion.range=VIDEO_RANGE_FULL;conversion.colorspace=VIDEO_CS_SRGB;
        obs_video_info info{};uint32_t divisor=1;
        if(obs_get_video_info(&info) && info.fps_den)
            divisor=uint32_t(std::max(uint64_t(1),uint64_t(info.fps_num)/(uint64_t(info.fps_den)*uint64_t(fps))));
        // OBS skips before scaling/conversion; filtering only inside capture would
        // still convert every 60/120-Hz Program frame on the CPU.
        obs_add_raw_video_callback2(&conversion,divisor,capture,this);connected_=true;
    }
}
void ProgramOutput::shutdown() {
    if(connected_) {obs_remove_raw_video_callback(capture,this);connected_=false;}
    sender_.shutdown();
}
void ProgramOutput::capture(void *opaque,video_data *frame) {
    if(frame) static_cast<ProgramOutput*>(opaque)->sender_.capture(frame->data[0],frame->linesize[0],frame->timestamp);
}
}
