// SPDX-License-Identifier: GPL-2.0-or-later
#include "video-receiver.hpp"
#include <QBuffer>
#include <QImageReader>
#include <chrono>
#include <cstring>
namespace vban {
VideoReceiver::VideoReceiver(std::function<uint64_t()> clock) : clock_(std::move(clock)), worker_([this] { run(); }) {}
VideoReceiver::~VideoReceiver() { shutdown(); }
void VideoReceiver::shutdown() {
    stop_ = true; wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}
void VideoReceiver::configure(const VideoConfigs &config, uint16_t port) {
    std::lock_guard lock(state_mutex_);
    for (size_t i = 0; i < video_slot_count; ++i) {
        auto &s = slots_[i];
        if (!(s.config == config[i]) || port_ != port) {
            s.latest = {}; s.status = {}; s.status.enabled = config[i].enabled;
            s.generation = ++generations_[i]; drops_[i] = 0;
        }
        s.config = config[i];
    }
    port_ = port;
}
void VideoReceiver::enqueue(size_t slot, const uint8_t *data, size_t size, uint64_t now) {
    if (slot >= video_slot_count || stop_) return;
    if (size > max_datagram || size < header_size) { ++drops_[slot]; return; }
    std::lock_guard lock(queue_mutex_);
    // At most ~6 MiB of queued packets, independent of incoming rate and image size.
    if (queue_.size() >= 4096) { ++drops_[slot]; return; }
    queue_.emplace_back(); auto &p = queue_.back();
    p.slot = slot; p.size = size; p.timestamp = now; p.generation = generations_[slot].load();
    std::memcpy(p.bytes.data(), data, size);
    wake_.notify_one();
}
VideoStatus VideoReceiver::status(size_t slot) const {
    if (slot >= video_slot_count) return {};
    std::lock_guard lock(state_mutex_);
    auto result = slots_[slot].status;
    result.queue_drops = drops_[slot].load();
    result.receiving = result.enabled && !slots_[slot].latest.image.isNull() &&
        clock_() - slots_[slot].latest.timestamp < video_stale_ns;
    return result;
}
VideoSnapshot VideoReceiver::snapshot(int slot) const {
    if (slot < 0 || slot >= int(video_slot_count)) return {};
    std::lock_guard lock(state_mutex_);
    const auto &s = slots_[slot];
    if (!s.config.enabled || s.latest.image.isNull() || clock_() - s.latest.timestamp >= video_stale_ns) return {};
    return s.latest;
}
void VideoReceiver::run() {
    std::array<FrameAssembler, video_slot_count> assemblers;
    std::array<uint64_t, video_slot_count> generations{};
    while (!stop_) {
        Datagram p;
        {
            std::unique_lock lock(queue_mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(20), [this] { return stop_ || !queue_.empty(); });
            if (stop_) break;
            if (queue_.empty()) {
                for (auto &assembler : assemblers) assembler.expire(clock_());
                continue;
            }
            p = std::move(queue_.front()); queue_.pop_front();
        }
        if (p.generation != generations_[p.slot].load() || clock_() - p.timestamp >= video_stale_ns) continue;
        auto &assembler = assemblers[p.slot];
        if (generations[p.slot] != p.generation) {
            assembler = {}; generations[p.slot] = p.generation;
        }
        try {
            auto completed = assembler.push(p.bytes.data(), p.size, p.timestamp);
            QImage image;
            std::string format, error;
            if (completed) {
                QByteArray bytes(reinterpret_cast<const char *>(completed->data()), static_cast<qsizetype>(completed->size()));
                QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                const auto type = reader.format().toLower();
                const auto dimensions = reader.size();
                // Fixed limits before decompression; do not let a remote image allocate arbitrary RAM.
                if (type != "png" && type != "jpeg" && type != "jpg") error = "Only VBAN-Frame JPEG and PNG images are supported.";
                else if (!dimensions.isValid() || dimensions.width() > 4096 || dimensions.height() > 4096 ||
                    int64_t(dimensions.width()) * dimensions.height() > 4096LL * 2160)
                    error = "Video image exceeds the 4K limit or has an invalid header.";
                else {
                    image = reader.read();
                    if (image.isNull()) error = "Cannot decode image: " + reader.errorString().toStdString();
                    else { image = image.convertToFormat(QImage::Format_RGBA8888); format = type == "png" ? "PNG" : "JPEG"; }
                }
            }
            std::lock_guard lock(state_mutex_);
            auto &s = slots_[p.slot];
            if (s.generation != p.generation) continue;
            s.status.packets = assembler.counters();
            if (completed) {
                if (!error.empty() || image.isNull()) {
                    ++s.status.decode_errors;
                    s.status.error = error.empty() ? "Image conversion failed." : error;
                } else {
                    s.latest = {std::move(image), ++serial_, p.timestamp};
                    ++s.status.decoded; s.status.error.clear(); s.status.format = format;
                    s.status.width = s.latest.image.width(); s.status.height = s.latest.image.height();
                }
            }
        } catch (const std::exception &e) {
            assembler = {};
            std::lock_guard lock(state_mutex_);
            if (slots_[p.slot].generation == p.generation) {
                ++slots_[p.slot].status.decode_errors;
                slots_[p.slot].status.error = std::string("Video frame rejected: ") + e.what();
            }
        }
    }
}
}
