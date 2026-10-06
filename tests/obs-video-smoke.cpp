// SPDX-License-Identifier: GPL-2.0-or-later
// Full path: UDP -> distributed DLL -> JPEG/PNG decoder -> OBS D3D11 -> raw output pixels.
#include "socket-platform.hpp"
#include <obs.h>
#include <QApplication>
#include <QAction>
#include <QWidget>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "frontend-stub.hpp"
static void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
static std::atomic<int> red{0},blue{0},black{0};
static void capture(void *,video_data *frame){
    const auto *p=frame->data[0]+90*frame->linesize[0]+160*4;
    if(p[2]>220&&p[1]<25&&p[0]<25)++red;
    if(p[0]>220&&p[2]<25&&p[1]<25)++blue;
    if(p[0]<10&&p[1]<10&&p[2]<10)++black;
}
int main(int argc,char **argv){
 try{
    check(argc==5,"Usage: obs-video-smoke plugin data config obs-runtime-root");
    qputenv("QT_QPA_PLATFORM","minimal:enable_fonts");QApplication app(argc,argv);QWidget window;
    auto *frontend=new TestFrontend(window);obs_frontend_set_callbacks_internal(frontend);
    const QString config=QString::fromLocal8Bit(argv[3]);QDir().mkpath(config+"/obs-vban-audio");
    check(vban::net::startup()==0,"Sockets");
    auto sender=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    auto reserve=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in dest{};dest.sin_family=AF_INET;dest.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(bind(reserve,reinterpret_cast<sockaddr*>(&dest),sizeof(dest))==0,"Reserve port");
    vban::net::Length length=sizeof(dest);getsockname(reserve,reinterpret_cast<sockaddr*>(&dest),&length);vban::net::close(reserve);
    QJsonArray videos;for(int i=0;i<2;++i)videos.append(QJsonObject{{"enabled",true},{"label",QString("Video %1").arg(i+1)},{"sender_ip","127.0.0.1"},{"stream_name",QString("VIDEO%1").arg(i+1)}});
    QFile file(config+"/obs-vban-audio/settings.json");check(file.open(QIODevice::WriteOnly),"Isolated settings");
    file.write(QJsonDocument(QJsonObject{{"version",1},{"port",ntohs(dest.sin_port)},{"videos",videos}}).toJson());file.close();
    check(obs_startup("en-US",config.toUtf8().constData(),nullptr),"OBS startup");
    const QString runtime=QString::fromLocal8Bit(argv[4]);
    const auto assets=(runtime+"/data/libobs/").toUtf8();
    // Standalone test hosts need an explicit shader path; this API remains in OBS 32.2.1.
#pragma warning(push)
#pragma warning(disable: 4996)
    obs_add_data_path(assets.constData());
#pragma warning(pop)
    const auto renderer=(runtime+"/bin/64bit/libobs-d3d11.dll").toUtf8();
    obs_video_info info{};info.graphics_module=renderer.constData();info.fps_num=30;info.fps_den=1;
    info.base_width=info.output_width=320;info.base_height=info.output_height=180;
    info.output_format=VIDEO_FORMAT_BGRA;info.colorspace=VIDEO_CS_SRGB;info.range=VIDEO_RANGE_FULL;info.scale_type=OBS_SCALE_BILINEAR;
    check(obs_reset_video(&info)==OBS_VIDEO_SUCCESS,"Start real OBS D3D11 renderer");
    const auto dll_path=QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1])).toUtf8();
    obs_module_t *module=nullptr;check(obs_open_module(&module,dll_path.constData(),argv[2])==MODULE_SUCCESS,"Load plugin");check(obs_init_module(module),"Initialize plugin");
    auto *settings=obs_data_create();obs_data_set_int(settings,"slot",0);
    auto *first=obs_source_create("vban_video_input","Mixer video",settings,nullptr);
    auto *second_settings=obs_data_create();obs_data_set_int(second_settings,"slot",1);
    auto *second=obs_source_create("vban_video_input","Second video",second_settings,nullptr);
    check(first&&second,"Two video sources");
    obs_set_output_source(0,first);obs_add_raw_video_callback(nullptr,capture,nullptr);
    QByteArray png,jpeg;QImage image(320,180,QImage::Format_RGB32);image.fill(Qt::red);
    QBuffer p(&png);p.open(QIODevice::WriteOnly);check(image.save(&p,"PNG"),"PNG encoder");
    image.fill(Qt::blue);QBuffer j(&jpeg);j.open(QIODevice::WriteOnly);check(image.save(&j,"JPEG",95),"JPEG encoder");
    uint32_t frame=0;
    auto send=[&](const QByteArray &bytes,const char *name){
        uint16_t index=0;
        for(qsizetype offset=0;offset<bytes.size();offset+=1436,++index){
            auto part=bytes.mid(offset,1436);std::vector<uint8_t> packet(28+static_cast<size_t>(part.size()));
            std::memcpy(packet.data(),"VBAN",4);packet[4]=0x89;packet[5]=uint8_t(index);packet[6]=uint8_t(index>>8);
            packet[7]=offset==0?1:2;if(offset+part.size()==bytes.size())packet[7]=offset==0?5:4;
            std::memcpy(packet.data()+8,name,std::strlen(name));std::memcpy(packet.data()+24,&frame,4);
            std::memcpy(packet.data()+28,part.constData(),static_cast<size_t>(part.size()));
            check(sendto(sender,reinterpret_cast<char*>(packet.data()),static_cast<int>(packet.size()),0,reinterpret_cast<sockaddr*>(&dest),sizeof(dest))==int(packet.size()),"Send VBAN frame");
        }
    };
    auto pump=[&](int ms,bool transmit){
        const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
        while(std::chrono::steady_clock::now()<end){if(transmit){++frame;send(png,"VIDEO1");send(jpeg,"VIDEO2");}app.processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(40));}
    };
    pump(1600,true);
    auto *props=obs_source_properties(first);
    std::cout<<"First source: "<<obs_property_description(obs_properties_get(props,"status"))<<"; size="<<obs_source_get_width(first)<<"x"<<obs_source_get_height(first)<<"; red="<<red<<" blue="<<blue<<" black="<<black<<"\n";
    obs_properties_destroy(props);
    check(red>10,"PNG visible in actual OBS video output");
    check(obs_source_get_width(first)==320&&obs_source_get_height(first)==180,"Native image dimensions");
    obs_set_output_source(0,second);pump(1000,true);check(blue>10,"Second JPEG stream visible in actual OBS output");
    const auto blue_before=blue.load();obs_source_update(first,second_settings);obs_set_output_source(0,first);pump(700,true);
    check(blue>blue_before+5,"Switching source selection displays the other stream");
    const auto black_before=black.load();pump(3600,false);check(black>black_before+5,"Sender timeout clears OBS picture");
    const auto recovery=blue.load();pump(600,true);check(blue>recovery+3,"Picture recovers when sender restarts");
    obs_remove_raw_video_callback(capture,nullptr);obs_set_output_source(0,nullptr);
    obs_source_release(first);obs_source_release(second);obs_data_release(settings);obs_data_release(second_settings);obs_wait_for_destroy_queue();
    obs_shutdown();app.processEvents();obs_frontend_set_callbacks_internal(nullptr);vban::net::close(sender);vban::net::cleanup();
    std::cout<<"PASS: two VBAN Video sources render PNG/JPEG pixels through the real OBS graphics pipeline; selection, timeout and recovery verified.\n";return 0;
 }catch(const std::exception &e){std::cerr<<"OBS video check failed: "<<e.what()<<"\n";return 1;}
}
