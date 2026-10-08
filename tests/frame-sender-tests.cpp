// SPDX-License-Identifier: GPL-2.0-or-later
#include "socket-platform.hpp"
#include "frame-sender.hpp"
#include "frame-protocol.hpp"
#include <QCoreApplication>
#include <QImage>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace vban;
static void check(bool v,const char*m){if(!v)throw std::runtime_error(m);}
static uint64_t now(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Listener {
    net::Socket socket=net::invalid;uint16_t port=0;
    Listener(){socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        check(!bind(socket,reinterpret_cast<sockaddr*>(&a),sizeof(a)),"Bind receiver");net::Length n=sizeof(a);getsockname(socket,reinterpret_cast<sockaddr*>(&a),&n);port=ntohs(a.sin_port);net::receive_buffer(socket);}
    ~Listener(){net::close(socket);}
    QImage receive(uint32_t expected,bool multipart=false){
        FrameAssembler assembler;uint16_t index=0;uint8_t last_flags=0;const auto deadline=now()+3'000'000'000ULL;
        while(now()<deadline){
            if(net::readable(socket,50)!=1)continue;
            std::array<uint8_t,1465> packet{};const int n=recv(socket,reinterpret_cast<char*>(packet.data()),int(packet.size()),0);
            check(n>28&&n<=1464&&!std::memcmp(packet.data(),"VBAN",4)&&packet[4]==0x8b,"Frame header and 24 Mbps hint");
            check(!std::memcmp(packet.data()+8,"OBS-PROGRAM",11),"Outgoing stream name");
            uint32_t frame=0;for(unsigned i=0;i<4;++i)frame|=uint32_t(packet[24+i])<<(8*i);
            check(frame==expected,"Counter continues across route changes");
            check((uint16_t(packet[5])|(uint16_t(packet[6])<<8))==index++,"Ordered 16-bit packet indices");last_flags=packet[7];
            auto complete=assembler.push(packet.data(),size_t(n),now());
            if(complete){check(last_flags==(index==1?5:4),"Correct single/multiple-packet ending");if(multipart)check(index>255,"Large PNG crosses the 8-bit index boundary");
                auto image=QImage::fromData(complete->data(),int(complete->size()));check(!image.isNull(),"Standard image decoder accepts complete output");return image;}
        }throw std::runtime_error("No complete outgoing image");
    }
};
int main(int argc,char**argv){try{
    QCoreApplication app(argc,argv);FrameSender sender;Listener first,second;
    FrameOutputConfig cfg;cfg.enabled=true;cfg.destination_ip="127.0.0.1";cfg.port=first.port;cfg.format="PNG";
    std::string error;auto route=sender.prepare(cfg,320,180,error);check(bool(route),error.c_str());
    check(FrameSender::dimensions(route)==std::pair{320,180},"Smaller images are not enlarged");
    check(net::readable(first.socket,10)==0,"Prepare never transmits");sender.activate(route);route.reset();
    QImage red(320,180,QImage::Format_RGBA8888);red.fill(Qt::red);
    sender.capture(red.constBits(),uint32_t(red.bytesPerLine()),1'000'000'000ULL);
    auto image=first.receive(0);check(image.size()==red.size()&&image.pixelColor(30,40)==QColor(Qt::red),"Lossless PNG retains RGBA byte order");
    sender.capture(red.constBits(),uint32_t(red.bytesPerLine()),1'010'000'000ULL);check(net::readable(first.socket,40)==0,"Frame-rate gate skips early captures");
    auto invalid=cfg;invalid.port=0;check(!sender.prepare(invalid,320,180,error),"Invalid port rejected transactionally");
    invalid=cfg;invalid.local_ip="192.0.2.111";check(!sender.prepare(invalid,320,180,error),"Unavailable adapter rejected");
    invalid=cfg;invalid.format="BMP";check(!sender.prepare(invalid,320,180,error),"Unsupported format rejected");
    invalid=cfg;invalid.fps=0;check(!sender.prepare(invalid,320,180,error),"Invalid fps rejected");
    cfg.port=second.port;cfg.format="JPEG";route=sender.prepare(cfg,320,180,error);check(bool(route),error.c_str());sender.activate(route);route.reset();
    red.fill(Qt::blue);sender.capture(red.constBits(),uint32_t(red.bytesPerLine()),2'000'000'000ULL);image=second.receive(1);
    check(image.pixelColor(30,40).blue()>245&&image.pixelColor(30,40).red()<5,"JPEG output retains blue colour");check(net::readable(first.socket,10)==0,"Old destination stops");
    cfg.format="PNG";cfg.max_width=1280;cfg.max_height=720;route=sender.prepare(cfg,1280,720,error);check(bool(route),error.c_str());sender.activate(route);route.reset();
    QImage noise(1280,720,QImage::Format_RGBA8888);uint32_t random=123;
    for(int y=0;y<noise.height();++y){auto *row=noise.scanLine(y);for(int x=0;x<noise.width();++x){for(int c=0;c<3;++c){random=random*1664525U+1013904223U;row[x*4+c]=uint8_t(random>>24);}row[x*4+3]=255;}}
    sender.capture(noise.constBits(),uint32_t(noise.bytesPerLine()),3'000'000'000ULL);image=second.receive(2,true);
    check(image.pixelColor(700,400)==noise.pixelColor(700,400),"Paced fragmented PNG retains exact pixels");
    check(sender.status().packets>255&&sender.status().errors==0,"Actual packets and send errors reported");
    cfg.mbps=12;route=sender.prepare(cfg,1280,720,error);check(bool(route),error.c_str());sender.activate(route);route.reset();
    for(uint64_t i=0;i<24;++i)sender.capture(noise.constBits(),uint32_t(noise.bytesPerLine()),4'000'000'000ULL+i*100'000'000ULL);
    const auto deadline=now()+3'000'000'000ULL;while(!sender.status().errors&&now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(sender.status().errors>0&&sender.status().dropped>0,"Over-budget PNG and queued replacement are counted");
    check(net::readable(second.socket,20)==0,"Oversized image sends no incomplete burst");
    sender.shutdown();check(!sender.status().enabled,"Shutdown disables capture and joins encoding worker");
    cfg.enabled=false;route=sender.prepare(cfg,0,0,error);sender.activate(route);sender.capture(noise.constBits(),uint32_t(noise.bytesPerLine()),now());check(net::readable(second.socket,20)==0,"Disabled route stays quiet");
    std::cout<<"PASS: PNG/JPEG sender, byte order, rate gate, multi-packet framing, pacing, destination changes, bounded backlog, invalid configuration and shutdown.\n";return 0;
}catch(const std::exception&e){std::cerr<<"Frame sender test failed: "<<e.what()<<"\n";return 1;}}
