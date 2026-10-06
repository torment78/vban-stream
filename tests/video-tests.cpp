// SPDX-License-Identifier: GPL-2.0-or-later
#include "socket-platform.hpp"
#include "receiver.hpp"
#include "video-receiver.hpp"
#include <QCoreApplication>
#include <QBuffer>
#include <QImageReader>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace vban;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static uint64_t now() { return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
static std::vector<uint8_t> fragment(uint32_t frame, uint16_t index, uint8_t flags, const QByteArray &payload, const char *name = "VIDEO1") {
    std::vector<uint8_t> p(28 + static_cast<size_t>(payload.size()));
    std::memcpy(p.data(), "VBAN", 4); p[4] = 0x89; p[5] = uint8_t(index); p[6] = uint8_t(index >> 8); p[7] = flags;
    std::memcpy(p.data()+8, name, std::strlen(name));
    for (unsigned i=0;i<4;++i) p[24+i] = uint8_t(frame >> (8*i));
    std::memcpy(p.data()+28, payload.constData(), static_cast<size_t>(payload.size())); return p;
}
static std::vector<std::vector<uint8_t>> frame_packets(uint32_t frame, const QByteArray &bytes, const char *name = "VIDEO1") {
    std::vector<std::vector<uint8_t>> result;
    for (qsizetype offset = 0; offset < bytes.size(); offset += 1436) {
        const auto part = bytes.mid(offset, 1436);
        uint8_t flags = offset == 0 ? 1 : 2;
        if (offset + part.size() == bytes.size()) flags = offset == 0 ? 5 : 4;
        result.push_back(fragment(frame, static_cast<uint16_t>(result.size()), flags, part, name));
    }
    return result;
}
static QByteArray image_bytes(const char *format, QRgb color, bool noisy = false) {
    QImage image(320, 180, QImage::Format_RGB32); image.fill(color);
    if (noisy) for (int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
        image.setPixel(x,y,qRgb((x*13+y*19)%256,(x*29+y*7)%256,(x*31+y*11)%256));
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    check(image.save(&buffer,format,95),"JPEG and PNG image codecs must be available"); return bytes;
}
static void parser_tests() {
    FrameAssembler a;
    auto first=fragment(10,0,1,"abc"), last=fragment(10,1,4,"def");
    check(!a.push(first.data(),first.size(),1),"First fragment must not display partial frame");
    auto complete=a.push(last.data(),last.size(),2);
    check(complete && std::string(complete->begin(),complete->end())=="abcdef","Official header reassembles ordered fragments");
    check(!a.push(last.data(),last.size(),3),"Duplicate completed frame ignored");
    auto single=fragment(11,0,5,"one");
    check(a.push(single.data(),single.size(),4).has_value(),"Combined start/end single packet");
    first=fragment(12,0,1,"first"); last=fragment(12,2,4,"last");
    a.push(first.data(),first.size(),5);
    check(!a.push(last.data(),last.size(),6),"Packet loss must discard entire frame");
    single=fragment(13,0,5,"new");
    check(a.push(single.data(),single.size(),7).has_value(),"Next frame recovers after loss");
    first=fragment(14,0,1,"first"); last=fragment(14,1,6,"last");
    a.push(first.data(),first.size(),8); a.push(first.data(),first.size(),9);
    check(a.push(last.data(),last.size(),10).has_value(),"Duplicate start and continue/end compatibility");
    first=fragment(15,0,1,"first"); a.push(first.data(),first.size(),11);
    a.expire(frame_timeout_ns+12);
    check(a.counters().incomplete>=2,"Partial frame times out");
    single=fragment(1,0,5,"restart");
    check(a.push(single.data(),single.size(),frame_timeout_ns+13).has_value(),"Sender restart recovers after quiet period");
    FrameAssembler reordered;
    first=fragment(20,0,1,"new");last=fragment(20,1,4,"image");
    reordered.push(first.data(),first.size(),20);
    auto old=fragment(19,0,1,"old");reordered.push(old.data(),old.size(),21);
    complete=reordered.push(last.data(),last.size(),22);
    check(complete && std::string(complete->begin(),complete->end())=="newimage","Delayed old start cannot cancel a newer image");
    FrameAssembler wrap;
    for (auto sequence : {0xfffffffeU,0xffffffffU,0U,1U}) {
        single=fragment(sequence,0,5,"wrap"); check(wrap.push(single.data(),single.size(),100).has_value(),"Frame counter wrap");
    }
    FrameAssembler limits;
    auto invalid=fragment(2,0,0x15,"bad");
    check(!limits.push(invalid.data(),invalid.size(),1),"Reject reserved image subtype");
    invalid=fragment(2,1,5,"bad");check(!limits.push(invalid.data(),invalid.size(),2),"Reject nonzero starting packet index");
    invalid=fragment(2,0,5,QByteArray(1437,'x'));check(!limits.push(invalid.data(),invalid.size(),3),"Reject oversized datagram");
    invalid=fragment(2,0,5,"bad");invalid[4]=3;check(!limits.push(invalid.data(),invalid.size(),4),"Audio cannot become a video frame");
    auto block=QByteArray(1436,'x');
    for(size_t i=0;i<=max_image_bytes/1436+1;++i) {
        const auto p=fragment(3,static_cast<uint16_t>(i),i==0?1:2,block);
        check(!limits.push(p.data(),p.size(),5),"Oversized frame never completes");
    }
    check(limits.counters().invalid>=5,"Reassembly memory limit is enforced");
}
int main(int argc,char **argv) {
 try {
    QCoreApplication app(argc,argv); parser_tests();
    check(net::startup()==0,"Network startup");
    auto socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    check(socket!=net::invalid,"UDP sender");
    sockaddr_in destination{};destination.sin_family=AF_INET;destination.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    auto reserve=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    check(bind(reserve,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))==0,"Reserve test port");
    net::Length len=sizeof(destination);getsockname(reserve,reinterpret_cast<sockaddr*>(&destination),&len);net::close(reserve);
    auto video=std::make_shared<VideoReceiver>(now);
    Receiver receiver(now,{},video);Config cfg;cfg.port=ntohs(destination.sin_port);
    cfg.videos[0]={true,"Mixer","127.0.0.1","VIDEO1"};cfg.videos[1]={true,"Screen","127.0.0.1","VIDEO2"};
    cfg.sender_ip="127.0.0.1";cfg.slots[0]={true,"Same-name audio","","VIDEO1"};
    std::string error;check(receiver.configure(cfg,error),error.c_str());
    auto send=[&](const std::vector<uint8_t> &p){check(sendto(socket,reinterpret_cast<const char*>(p.data()),static_cast<int>(p.size()),0,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))==int(p.size()),"Send complete datagram");};
    auto send_image=[&](uint32_t seq,const QByteArray &bytes,const char *name){
        const auto packets=frame_packets(seq,bytes,name);
        for(size_t i=0;i<packets.size();++i){send(packets[i]);if(i%32==0)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    };
    auto wait=[&](auto predicate,const char *message){const auto end=now()+3000000000ULL;while(!predicate()&&now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));check(predicate(),message);};
    const auto png=image_bytes("PNG",qRgb(255,0,0)); const auto jpeg=image_bytes("JPEG",qRgb(0,0,255));
    send_image(1,png,"VIDEO1");send_image(1,jpeg,"VIDEO2");
    wait([&]{return video->status(0).decoded==1 && video->status(1).decoded==1;},"Two independent PNG/JPEG streams decode through real UDP");
    check(video->snapshot(0).image.pixelColor(10,10).red()>245,"PNG pixels reach video slot one");
    check(video->snapshot(1).image.pixelColor(10,10).blue()>245,"JPEG pixels reach video slot two");
    check(video->status(0).format=="PNG" && video->status(1).format=="JPEG","Formats detected automatically");
    check(receiver.status(0).counters.unsupported==0,"Video does not poison same-name audio receiver");
    const auto before=video->status(0).decoded;
    auto parts=frame_packets(2,image_bytes("PNG",qRgb(0,0,0),true));check(parts.size()>2,"Multi-packet loss fixture");
    for(size_t i=0;i<parts.size();++i)if(i!=1)send(parts[i]);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(video->status(0).decoded==before,"Dropped fragment never displays corrupted image");
    send_image(3,jpeg,"VIDEO1");wait([&]{return video->status(0).decoded==before+1;},"Recovery and JPEG/PNG live switch");
    send_image(4,QByteArray("not an image"),"VIDEO1");wait([&]{return video->status(0).decode_errors==1;},"Corrupt complete image rejected safely");
    const auto old_serial=video->snapshot(0).serial;send_image(5,png,"WRONG");std::this_thread::sleep_for(std::chrono::milliseconds(40));
    check(video->snapshot(0).serial==old_serial,"Unconfigured stream name ignored");
    auto badcfg=cfg;badcfg.videos[1]=cfg.videos[0];check(!receiver.configure(badcfg,error),"Duplicate stream rejected atomically");
    check(receiver.config().videos[1].stream_name=="VIDEO2","Rejected settings preserve routing");
    cfg.videos[0].enabled=false;check(receiver.configure(cfg,error),"Disable video");
    check(video->snapshot(0).image.isNull(),"Disable clears last image immediately");
    cfg.videos[0]={true,"New sender","127.0.0.2","VIDEO1"};check(receiver.configure(cfg,error),"Retarget sender");
    send_image(6,png,"VIDEO1");std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(video->status(0).decoded==0,"Sender IPv4 filter enforced");
    cfg.slots[0].enabled=false;cfg.videos[0].sender_ip="127.0.0.1";check(receiver.configure(cfg,error),"Video-only reception");
    send_image(7,png,"VIDEO1");wait([&]{return video->status(0).decoded==1;},"Listener stays open without audio slots");
    std::this_thread::sleep_for(std::chrono::milliseconds(3100));
    check(video->snapshot(0).image.isNull()&&!video->status(0).receiving,"Stale receiver releases picture and reports waiting");
    receiver.shutdown();video->shutdown();net::close(socket);net::cleanup();
    std::cout<<"PASS: VBAN-Frame fragmentation/loss/duplicates/wrap/limits, real UDP two-slot PNG/JPEG reception, sender/name filtering, live changes and shutdown.\n";return 0;
 }catch(const std::exception &e){std::cerr<<"Video tests failed: "<<e.what()<<"\n";return 1;}
}
