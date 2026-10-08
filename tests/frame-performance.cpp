// SPDX-License-Identifier: GPL-2.0-or-later
#include "socket-platform.hpp"
#include "frame-sender.hpp"
#include "frame-protocol.hpp"
#include <QCoreApplication>
#include <QImage>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <stdexcept>
using namespace vban;
using Clock=std::chrono::steady_clock;
static uint64_t ns(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());}
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Result {double fps=0,mbps=0,age_ms=0;uint64_t frames=0,packets=0;};
static Result measure(int width,int height,int quality,int limit,bool still,bool enforce){
    FrameSender sender;
    auto socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    check(socket!=net::invalid,"receiver socket");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(!bind(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address)),"receiver bind");
    net::Length length=sizeof(address);getsockname(socket,reinterpret_cast<sockaddr*>(&address),&length);net::receive_buffer(socket);
    FrameOutputConfig config;config.enabled=true;config.destination_ip="127.0.0.1";config.port=ntohs(address.sin_port);
    config.max_width=width;config.max_height=height;config.fps=30;config.quality=quality;config.mbps=limit;
    std::string error;auto route=sender.prepare(config,uint32_t(width),uint32_t(height),error);check(bool(route),error.c_str());sender.activate(route);route.reset();
    QImage picture(width,height,QImage::Format_RGBA8888);uint32_t random=391;
    // Repeatable detailed scene, with spatial correlation like a textured picture.
    for(int y=0;y<height;++y){auto *row=picture.scanLine(y);for(int x=0;x<width;++x){
        for(int c=0;c<3;++c){random=random*1664525U+1013904223U;row[x*4+c]=uint8_t(((x/8+y/8+c*33)&127)+(random>>25));}row[x*4+3]=255;}}
    std::array<std::atomic<uint64_t>,128> timestamps{};
    std::atomic<bool> stop=false;std::atomic<uint64_t> received=0,bytes=0,packets=0,age_total=0;std::string reader_error;
    std::thread reader([&]{try{FrameAssembler assembler;while(!stop){if(net::readable(socket,20)!=1)continue;
        std::array<uint8_t,1465> packet{};const int count=recv(socket,reinterpret_cast<char*>(packet.data()),int(packet.size()),0);
        if(count<=0)continue;bytes+=uint64_t(count)+66;++packets;
        auto complete=assembler.push(packet.data(),size_t(count),ns());if(!complete)continue;
        auto image=QImage::fromData(complete->data(),int(complete->size()));check(!image.isNull(),"decode benchmark JPEG");
        unsigned id=0;for(unsigned bit=0;bit<7;++bit)if(image.pixelColor(int(bit)*16+8,8).red()>128)id|=1U<<bit;
        const auto sent=timestamps[id].load();if(sent&& !still){age_total+=ns()-sent;}++received;
    }}catch(const std::exception &e){reader_error=e.what();}});
    const auto start=Clock::now();
    for(unsigned i=0;i<90;++i){
        if(!still||i==0)for(int y=0;y<16;++y){auto *row=picture.scanLine(y);for(unsigned bit=0;bit<7;++bit)for(int x=0;x<16;++x){auto *pixel=row+(bit*16+unsigned(x))*4;pixel[0]=pixel[1]=pixel[2]=(i&(1U<<bit))?255:0;}}
        timestamps[i]=ns();sender.capture(picture.constBits(),uint32_t(picture.bytesPerLine()),ns());
        std::this_thread::sleep_until(start+std::chrono::nanoseconds((uint64_t(i)+1)*1'000'000'000ULL/30));
    }
    const auto status=sender.status();sender.shutdown();stop=true;reader.join();net::close(socket);check(reader_error.empty(),reader_error.c_str());
    Result result;result.frames=received;result.packets=packets;result.fps=double(received)/3.0;result.mbps=double(bytes)*8/3'000'000.0;result.age_ms=received?double(age_total)/double(received)/1'000'000:0;
    std::cout<<width<<"x"<<height<<" q="<<quality<<" cap="<<limit<<" still="<<still<<": fps="<<result.fps<<" wire_Mbps="<<result.mbps<<" age_ms="<<result.age_ms<<" frames="<<result.frames<<" skipped="<<status.dropped<<" errors="<<status.errors<<" encode_ms="<<status.encode_ms<<" send_ms="<<status.send_ms<<" effective_q="<<status.jpeg_quality<<"\n";
    if(enforce){check(result.mbps<double(limit)*1.12,"network cap");if(still)check(result.frames<=5&&result.frames>=2,"unchanged frames suppressed with heartbeat");else check(result.fps>=20,"moving detailed picture keeps useful frame rate");}
    return result;
}
int main(int argc,char **argv){try{QCoreApplication app(argc,argv);const bool enforce=argc<2||std::strcmp(argv[1],"--report-only");
    if(argc>1 && !std::strcmp(argv[1],"--check-static")){measure(1280,720,80,48,true,true);return 0;}
    measure(1280,720,80,48,false,enforce);measure(1920,1080,80,84,false,enforce);measure(1920,1080,25,84,false,enforce);measure(1280,720,80,48,true,enforce);
    return 0;}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
