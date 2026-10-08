// SPDX-License-Identifier: GPL-2.0-or-later
// Full path: UDP -> distributed DLL -> JPEG/PNG decoder -> OBS D3D11 -> raw output pixels.
#include "socket-platform.hpp"
#include "frame-protocol.hpp"
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
#include "program-display-fixture.hpp"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <graphics/vec2.h>
struct MouseListener {
    vban::net::Socket socket=vban::net::invalid;
    uint16_t port=0;
    uint32_t sequence=0;
    MouseListener() {
        socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address)))throw std::runtime_error("Mouse listener bind");
        vban::net::Length length=sizeof(address);getsockname(socket,reinterpret_cast<sockaddr*>(&address),&length);port=ntohs(address.sin_port);
    }
    ~MouseListener(){vban::net::close(socket);}
    void expect(const char *command,int x,int y) {
        if(vban::net::readable(socket,1000)!=1)throw std::runtime_error("Mouse command missing");
        char packet[1464]{};int n=recv(socket,packet,sizeof(packet),0);
        uint32_t counter=0;for(int i=0;i<4;++i)counter|=uint32_t(uint8_t(packet[24+i]))<<(8*i);
        const auto wanted=QString("System.Mouse=(%1, %2, %3);").arg(command).arg(x).arg(y).toStdString();
        if(n<=28||std::memcmp(packet,"VBAN\x52\0\0\x10",8)||counter!=sequence++||std::string(packet+28,packet+n)!=wanted)
            throw std::runtime_error("Unexpected Program mouse packet: "+std::string(packet+28,packet+std::max(n,28))+"; expected "+wanted);
    }
    void quiet(){if(vban::net::readable(socket,10)!=0)throw std::runtime_error("Unexpected mouse transmission");}
};
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
    auto *layout=new QHBoxLayout(&window);layout->setObjectName("previewLayout");
    auto *preview=new OBSBasicPreview(&window);auto *program=new OBSQTDisplay(&window);
    preview->setFixedSize(340,200);program->setFixedSize(340,200);layout->addWidget(preview);layout->addWidget(program);
    window.show();app.processEvents(); // Minimal Qt backend: no desktop window or global input.
    auto *frontend=new TestFrontend(window);frontend->studio_mode=true;obs_frontend_set_callbacks_internal(frontend);
    const QString config=QString::fromLocal8Bit(argv[3]);QDir().mkpath(config+"/obs-vban-audio");
    check(vban::net::startup()==0,"Sockets");
    MouseListener mouse[2], outgoing;
    vban::net::receive_buffer(outgoing.socket);
    vban::FrameAssembler program_frames;int sent_red=0,sent_blue=0;
    auto receive_program=[&]{
        while(vban::net::readable(outgoing.socket,0)>0){
            std::array<uint8_t,1465> bytes{};const int n=recv(outgoing.socket,reinterpret_cast<char*>(bytes.data()),int(bytes.size()),0);
            check(n>28&&n<=1464&&!std::memcmp(bytes.data()+8,"OBS-PROGRAM",11),"Program wire name and datagram length");
            const auto now=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
            auto complete=program_frames.push(bytes.data(),size_t(n),now);
            if(complete){const auto decoded=QImage::fromData(complete->data(),int(complete->size()));check(decoded.size()==QSize(320,180),"Outgoing Program dimensions");
                const auto pixel=decoded.pixelColor(160,90);if(pixel.red()>220&&pixel.blue()<25)++sent_red;if(pixel.blue()>220&&pixel.red()<25)++sent_blue;}
        }
    };
    QJsonArray mice;for(int i=0;i<2;++i)mice.append(QJsonObject{{"enabled",true},{"destination_ip","127.0.0.1"},{"port",mouse[i].port},{"stream_name",QString("Command%1").arg(i+1)}});
    auto sender=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    auto reserve=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in dest{};dest.sin_family=AF_INET;dest.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(bind(reserve,reinterpret_cast<sockaddr*>(&dest),sizeof(dest))==0,"Reserve port");
    vban::net::Length length=sizeof(dest);getsockname(reserve,reinterpret_cast<sockaddr*>(&dest),&length);vban::net::close(reserve);
    QJsonArray videos;for(int i=0;i<2;++i)videos.append(QJsonObject{{"enabled",true},{"label",QString("Video %1").arg(i+1)},{"sender_ip","127.0.0.1"},{"stream_name",QString("VIDEO%1").arg(i+1)}});
    QFile file(config+"/obs-vban-audio/settings.json");check(file.open(QIODevice::WriteOnly),"Isolated settings");
    file.write(QJsonDocument(QJsonObject{{"version",1},{"port",ntohs(dest.sin_port)},{"videos",videos},{"mouse_returns",mice},{"frame_output",QJsonObject{{"enabled",true},{"destination_ip","127.0.0.1"},{"port",outgoing.port},{"format","PNG"}}}}).toJson());file.close();
    check(obs_startup("en-US",config.toUtf8().constData(),nullptr),"OBS startup");
    auto *private_data=obs_get_private_data();obs_data_set_bool(private_data,"AbsoluteCoordinates",true);obs_data_release(private_data);
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
        while(std::chrono::steady_clock::now()<end){if(transmit){++frame;send(png,"VIDEO1");send(jpeg,"VIDEO2");}app.processEvents();receive_program();std::this_thread::sleep_for(std::chrono::milliseconds(40));}
    };
    pump(1600,true);
    auto *props=obs_source_properties(first);
    std::cout<<"First source: "<<obs_property_description(obs_properties_get(props,"status"))<<"; size="<<obs_source_get_width(first)<<"x"<<obs_source_get_height(first)<<"; red="<<red<<" blue="<<blue<<" black="<<black<<"\n";
    obs_properties_destroy(props);
    check(red>10,"PNG visible in actual OBS video output");
    check(sent_red>=1,"Actual Program renderer is encoded and delivered over VBAN UDP");
    check(obs_source_get_width(first)==320&&obs_source_get_height(first)==180,"Native image dimensions");
    obs_set_output_source(0,second);pump(1000,true);check(blue>10,"Second JPEG stream visible in actual OBS output");
    check(sent_blue>=1,"Program sender follows the switched Program picture");
    const auto blue_before=blue.load();obs_source_update(first,second_settings);obs_set_output_source(0,first);pump(700,true);
    check(blue>blue_before+5,"Switching source selection displays the other stream");
    const auto black_before=black.load();pump(3600,false);check(black>black_before+5,"Sender timeout clears OBS picture");
    const auto recovery=blue.load();pump(600,true);check(blue>recovery+3,"Picture recovers when sender restarts");
    obs_remove_raw_video_callback(capture,nullptr);
    // Drive only this isolated application's Qt event loop; never inject desktop input.
    obs_data_set_int(settings,"slot",0);obs_source_update(first,settings);
    auto *scene=obs_scene_create("Program mouse fixture");
    auto *item=obs_scene_add(scene,first);auto *item2=obs_scene_add(scene,second);
    vec2 position{20,10},scale{.5f,.5f};obs_sceneitem_set_pos(item,&position);obs_sceneitem_set_scale(item,&scale);obs_sceneitem_force_update_transform(item);
    position={160,90};obs_sceneitem_set_pos(item2,&position);obs_sceneitem_set_scale(item2,&scale);obs_sceneitem_force_update_transform(item2);
    obs_set_output_source(0,obs_scene_get_source(scene));pump(200,true);
    auto event=[&](QWidget *widget,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers=Qt::ControlModifier){
        QMouseEvent input(type,point,point,point,button,buttons,modifiers);QApplication::sendEvent(widget,&input);
    };
    auto down=[&](QWidget *widget,QPointF point,Qt::KeyboardModifiers modifiers=Qt::ControlModifier){event(widget,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,modifiers);};
    auto started=[&](int slot){mouse[slot].expect("MOUSEMOVE",80,60);mouse[slot].expect("LBUTTONDOWN",80,60);};
    auto released=[&](int slot,int x=80,int y=60){for(int i=0;i<3;++i)mouse[slot].expect("LBUTTONUP",x,y);};
    down(preview,{70,50});down(program,{70,50},Qt::NoModifier);mouse[0].quiet();mouse[1].quiet();
    down(program,{70,50});started(0);
    event(program,QEvent::MouseMove,{80,60},Qt::NoButton,Qt::LeftButton);mouse[0].expect("MOUSEMOVE",100,80);
    event(program,QEvent::MouseButtonRelease,{85,65},Qt::LeftButton,Qt::NoButton);mouse[0].expect("MOUSEMOVE",110,90);released(0,110,90);
    down(program,{210,130});started(1);QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(&window,&deactivate);released(1);
    down(program,{70,50});started(0);QKeyEvent ctrl_up(QEvent::KeyRelease,Qt::Key_Control,Qt::NoModifier);QApplication::sendEvent(program,&ctrl_up);released(0);
    down(program,{70,50});started(0);for(const auto &[callback,data]:frontend->events)callback(OBS_FRONTEND_EVENT_SCENE_CHANGED,data);released(0);
    down(program,{70,50});started(0);event(program,QEvent::MouseMove,{210,130},Qt::NoButton,Qt::LeftButton);released(0);mouse[1].quiet();
    event(program,QEvent::MouseButtonPress,{70,50},Qt::RightButton,Qt::RightButton);mouse[0].expect("MOUSEMOVE",80,60);mouse[0].expect("RBUTTONDOWN",80,60);
    event(program,QEvent::MouseButtonRelease,{70,50},Qt::RightButton,Qt::NoButton);mouse[0].expect("MOUSEMOVE",80,60);for(int i=0;i<3;++i)mouse[0].expect("RBUTTONUP",80,60);
    QContextMenuEvent menu(QContextMenuEvent::Mouse,{70,50},{70,50},Qt::ControlModifier);check(QApplication::sendEvent(program,&menu),"Ctrl right-click context menu consumed");
    obs_sceneitem_set_visible(item,false);obs_sceneitem_set_visible(item2,false);
    auto *nested=obs_scene_create("Nested mouse fixture");auto *nested_video=obs_scene_add(nested,first);
    position={10,10};obs_sceneitem_set_pos(nested_video,&position);obs_sceneitem_set_scale(nested_video,&scale);obs_sceneitem_force_update_transform(nested_video);
    auto *nested_item=obs_scene_add(scene,obs_scene_get_source(nested));position={20,0};obs_sceneitem_set_pos(nested_item,&position);obs_sceneitem_force_update_transform(nested_item);
    pump(150,true);down(program,{80,50});started(0);QApplication::sendEvent(program,&ctrl_up);released(0);
    auto *group=obs_scene_insert_group(scene,"Mouse group",&nested_item,1);check(group!=nullptr,"Create grouped nested scene");
    pump(150,true);down(program,{80,50});started(0);QApplication::sendEvent(program,&ctrl_up);released(0);
    obs_sceneitem_remove(group);obs_sceneitem_remove(nested_video);obs_scene_release(nested);
    obs_sceneitem_set_visible(item,true);obs_sceneitem_set_visible(item2,true);
    frontend->studio_mode=false;for(const auto &[callback,data]:frontend->events)callback(OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED,data);
    down(program,{70,50});mouse[0].quiet();frontend->studio_mode=true;
    pump(3300,false);down(program,{70,50});mouse[0].quiet(); // Stale picture cannot receive clicks.
    mouse[1].quiet();obs_set_output_source(0,nullptr);
    obs_sceneitem_remove(item);obs_sceneitem_remove(item2);obs_scene_release(scene);
    obs_source_release(first);obs_source_release(second);obs_data_release(settings);obs_data_release(second_settings);obs_wait_for_destroy_queue();
    obs_shutdown();app.processEvents();obs_frontend_set_callbacks_internal(nullptr);vban::net::close(sender);vban::net::cleanup();
    std::cout<<"PASS: two VBAN Video sources render PNG/JPEG pixels through the real OBS graphics pipeline; selection, timeout, recovery, Program-only Ctrl mouse return and live Program-to-VBAN output verified.\n";return 0;
 }catch(const std::exception &e){std::cerr<<"OBS video check failed: "<<e.what()<<"\n";return 1;}
}
