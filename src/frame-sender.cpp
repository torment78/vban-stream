// SPDX-License-Identifier: GPL-2.0-or-later
#include "socket-platform.hpp"
#include "frame-sender.hpp"
#include "frame-protocol.hpp"
#include "vban-transmitter.hpp"
#include <QImage>
#include <QBuffer>
#include <QImageWriter>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <thread>
#include <stdexcept>
namespace vban {
namespace {
using Clock = std::chrono::steady_clock;
uint64_t clock_ns() { return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()); }
}
struct FrameSender::Session {
    FrameOutputConfig config;
    net::Socket socket = net::invalid;
    int width = 0, height = 0;
    std::string local_ip, error;
    std::array<std::vector<uint8_t>,3> pixels;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> frames{0}, packets{0}, bytes{0}, dropped{0}, errors{0}, sent_at{0};
    std::shared_ptr<std::atomic<uint32_t>> sequence;
    int pending = -1, busy = -1;
    uint64_t next_timestamp = 0, previous_timestamp = 0; // Producer only.
    ~Session() {
        stop = true; wake.notify_all();
        if (worker.joinable()) worker.join();
        if (socket != net::invalid) net::close(socket);
    }
    void fail(const std::string &message) { ++errors; std::lock_guard lock(mutex); error = message; }
    void capture(const uint8_t *rgba, uint32_t stride, uint64_t timestamp) noexcept {
        if (!config.enabled || stop || !rgba || stride < uint32_t(width)*4) return;
        const auto period = 1'000'000'000ULL / uint64_t(config.fps);
        if (timestamp < previous_timestamp) next_timestamp = 0;
        previous_timestamp = timestamp;
        if (next_timestamp && timestamp < next_timestamp) return;
        if (!next_timestamp || timestamp-next_timestamp > period) next_timestamp = timestamp+period;
        else next_timestamp += period;
        std::unique_lock lock(mutex,std::try_to_lock);
        if (!lock.owns_lock()) { ++dropped; return; }
        int index = pending;
        if (index >= 0) ++dropped; // Replace stale queued work with the latest Program picture.
        else for (int i=0;i<3;++i) if (i!=busy) { index=i; break; }
        if (index < 0) { ++dropped; return; }
        auto *destination=pixels[size_t(index)].data();
        for (int y=0;y<height;++y) std::memcpy(destination+size_t(y)*size_t(width)*4,rgba+size_t(y)*stride,size_t(width)*4);
        pending=index; lock.unlock(); wake.notify_one();
    }
    bool send_image(const QByteArray &encoded) {
        const auto bit_rate=uint64_t(config.mbps)*1'000'000ULL;
        const auto count=(size_t(encoded.size())+1435)/1436;
        // Leave ample margin under the receiver's two-second assembly timeout.
        const auto wire_bytes=uint64_t(encoded.size())+uint64_t(count)*(header_size+66);
        if (encoded.isEmpty() || size_t(encoded.size())>max_image_bytes || wire_bytes>bit_rate/8) {
            ++dropped; fail("Image exceeds the one-second network budget. Use JPEG, a smaller resolution or a higher network limit."); return false;
        }
        std::array<uint8_t,max_datagram> packet{};
        std::memcpy(packet.data(),"VBAN",4);
        const uint8_t rate_index=config.mbps==12?9:config.mbps==24?11:config.mbps==48?13:15;
        packet[4]=uint8_t(0x80|rate_index);
        std::memcpy(packet.data()+8,config.stream_name.data(),config.stream_name.size());
        const auto frame=sequence->fetch_add(1);
        for (unsigned i=0;i<4;++i) packet[24+i]=uint8_t(frame>>(8*i));
        const auto began=Clock::now(); uint64_t sent=0;
        for (size_t index=0;index<count;++index) {
            if (stop) return false;
            // Small packet batches avoid an image-sized UDP burst competing with audio.
            if (index%16==0 && index) {
                const auto deadline=began+std::chrono::nanoseconds(sent*8'000'000'000ULL/bit_rate);
                std::unique_lock lock(mutex);
                if (wake.wait_until(lock,deadline,[this]{return stop.load();})) return false;
            }
            if (Clock::now()-began>std::chrono::milliseconds(1500)) { ++dropped; fail("Video sender missed its network deadline."); return false; }
            const size_t offset=index*1436, length=std::min(size_t(1436),size_t(encoded.size())-offset);
            packet[5]=uint8_t(index);packet[6]=uint8_t(index>>8);
            packet[7]=index==0?1:2;
            if (index+1==count) packet[7]=index==0?5:4;
            std::memcpy(packet.data()+header_size,encoded.constData()+offset,length);
            const auto size=int(header_size+length);
            if (::send(socket,reinterpret_cast<const char*>(packet.data()),size,0)!=size) {
                ++dropped;fail("Video UDP send failed (socket "+std::to_string(net::error())+").");return false;
            }
            ++packets;bytes+=uint64_t(size);sent+=uint64_t(size)+66;
        }
        ++frames;sent_at=clock_ns();
        { std::lock_guard lock(mutex);error.clear(); }
        // Include the last batch in pacing, even when an entire picture is tiny.
        std::unique_lock lock(mutex);
        wake.wait_until(lock,began+std::chrono::nanoseconds(sent*8'000'000'000ULL/bit_rate),[this]{return stop.load();});
        return true;
    }
    void run() {
        while (!stop) {
            int index;
            {
                std::unique_lock lock(mutex);wake.wait(lock,[this]{return stop.load()||pending>=0;});
                if (stop) break;
                index=pending;pending=-1;busy=index;
            }
            try {
                // OBS provides RGBA bytes. Program is an opaque composited view.
                QImage image(static_cast<const uint8_t*>(pixels[size_t(index)].data()),width,height,width*4,QImage::Format_RGBX8888);
                QByteArray encoded;QBuffer buffer(&encoded);buffer.open(QIODevice::WriteOnly);
                QImageWriter writer(&buffer,config.format.c_str());
                if (config.format=="JPEG") writer.setQuality(config.quality);
                if (!writer.write(image)) { ++dropped;fail("Image encoding failed: "+writer.errorString().toStdString()); }
                else if (!stop) send_image(encoded);
            } catch (const std::exception &e) { ++dropped;fail(std::string("Video sender: ")+e.what()); }
            { std::lock_guard lock(mutex);busy=-1; }
        }
    }
};
FrameSender::FrameSender():sequence_(std::make_shared<std::atomic<uint32_t>>(0)) {
    if (net::startup()) throw std::runtime_error("Video output socket initialization failed.");
}
FrameSender::~FrameSender() { shutdown(); net::cleanup(); }
FrameSender::Prepared FrameSender::prepare(const FrameOutputConfig &cfg,uint32_t source_width,uint32_t source_height,std::string &error) {
    error.clear();auto next=std::make_shared<Session>();next->config=cfg;next->sequence=sequence_;
    if (!cfg.enabled) return next;
    if (!source_width||!source_height||!valid_stream_name(cfg.stream_name)||!cfg.port||
        (cfg.format!="JPEG"&&cfg.format!="PNG")||cfg.fps<1||cfg.fps>30||cfg.quality<1||cfg.quality>100||
        (cfg.mbps!=12&&cfg.mbps!=24&&cfg.mbps!=48&&cfg.mbps!=84)||
        !((cfg.max_width==640&&cfg.max_height==360)||(cfg.max_width==1280&&cfg.max_height==720)||(cfg.max_width==1920&&cfg.max_height==1080))) {
        error="OBS to VBAN Frame: invalid stream, image format, resolution, rate or network limit.";return {};
    }
    const double scale=std::min({1.0,double(cfg.max_width)/source_width,double(cfg.max_height)/source_height});
    next->width=std::max(1,int(source_width*scale));next->height=std::max(1,int(source_height*scale));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(cfg.port);
    if (net::parse(AF_INET,cfg.destination_ip.c_str(),&address.sin_addr)!=1||address.sin_addr.s_addr==INADDR_ANY||
        address.sin_addr.s_addr==INADDR_BROADCAST||(ntohl(address.sin_addr.s_addr)&0xf0000000U)==0xe0000000U) {
        error="OBS to VBAN Frame: enter the receiving computer's unicast IPv4 address.";return {};
    }
    next->socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (next->socket==net::invalid||!net::nonblocking(next->socket)||!net::ignore_port_unreachable(next->socket)) {
        error="OBS to VBAN Frame: cannot create UDP socket.";return {};
    }
    if (!cfg.local_ip.empty()) {
        const auto adapters=local_ipv4_addresses(error);if(!error.empty())return {};
        const auto found=std::find_if(adapters.begin(),adapters.end(),[&](const auto&a){return a.address==cfg.local_ip;});
        sockaddr_in local{};local.sin_family=AF_INET;
        if(found==adapters.end()||net::parse(AF_INET,cfg.local_ip.c_str(),&local.sin_addr)!=1||
            !net::select_interface(next->socket,found->interface_index)||bind(next->socket,reinterpret_cast<sockaddr*>(&local),sizeof(local))) {
            error="OBS to VBAN Frame: selected local adapter is unavailable.";return {};
        }
    }
    for (int size : {1024*1024,512*1024,256*1024,128*1024})
        if (!setsockopt(next->socket,SOL_SOCKET,SO_SNDBUF,reinterpret_cast<const char*>(&size),sizeof(size))) break;
    if(connect(next->socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))) {error="OBS to VBAN Frame: cannot select destination route.";return {};}
    sockaddr_in local{};net::Length length=sizeof(local);
    if(!getsockname(next->socket,reinterpret_cast<sockaddr*>(&local),&length)) {
        char ip[INET_ADDRSTRLEN]{};if(net::format(AF_INET,&local.sin_addr,ip,sizeof(ip)))next->local_ip=ip;
    }
    for(auto &buffer:next->pixels) buffer.resize(size_t(next->width)*size_t(next->height)*4);
    next->worker=std::thread([session=next.get()]{session->run();});
    return next;
}
void FrameSender::activate(Prepared next) { shutdown();active_=std::move(next); }
void FrameSender::shutdown() { if(active_) {active_->stop=true;active_->wake.notify_all();if(active_->worker.joinable())active_->worker.join();}active_.reset(); }
void FrameSender::capture(const uint8_t *rgba,uint32_t stride,uint64_t timestamp) noexcept { if(active_)active_->capture(rgba,stride,timestamp); }
std::pair<int,int> FrameSender::dimensions(const Prepared &s) { return s?std::pair{s->width,s->height}:std::pair{0,0}; }
FrameOutputStatus FrameSender::status() const {
    if(!active_)return {};const auto &s=*active_;
    FrameOutputStatus result;result.enabled=s.config.enabled;result.width=s.width;result.height=s.height;
    result.frames=s.frames;result.packets=s.packets;result.bytes=s.bytes;result.dropped=s.dropped;result.errors=s.errors;
    const auto sent=s.sent_at.load();result.sending=sent&&clock_ns()-sent<3'000'000'000ULL;result.local_ip=s.local_ip;
    { std::lock_guard lock(s.mutex);result.error=s.error; }return result;
}
}
