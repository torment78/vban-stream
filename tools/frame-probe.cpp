// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded LAN diagnostic receiver: saves counters/headers, never pictures.
#include "socket-platform.hpp"
#include "frame-protocol.hpp"
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>
#include <iostream>
#include <cstring>
using Clock=std::chrono::steady_clock;
struct Decoder {
    std::mutex mutex; std::condition_variable wake;std::deque<QByteArray> queue;bool stop=false;
    uint64_t decoded=0,qt_errors=0,gdi_errors=0,queue_drops=0;double decode_ms=0;int width=0,height=0;std::string last_error;
    std::thread worker;
    Decoder():worker([this]{run();}){}
    ~Decoder(){finish();}
    void finish(){ {std::lock_guard lock(mutex);stop=true;}wake.notify_all();if(worker.joinable())worker.join(); }
    void put(QByteArray image){std::lock_guard lock(mutex);if(queue.size()>=4){++queue_drops;return;}queue.push_back(std::move(image));wake.notify_one();}
    void run(){for(;;){QByteArray bytes;{std::unique_lock lock(mutex);wake.wait(lock,[this]{return stop||!queue.empty();});if(queue.empty())return;bytes=std::move(queue.front());queue.pop_front();}
        auto start=Clock::now();QBuffer input(&bytes);input.open(QIODevice::ReadOnly);QImageReader reader(&input);auto size=reader.size();
        if(!size.isValid()||size.width()>4096||size.height()>2160){++qt_errors;last_error="invalid or oversized dimensions";continue;}
        auto image=reader.read();if(image.isNull()){++qt_errors;last_error=reader.errorString().toStdString();continue;}
        width=image.width();height=image.height();++decoded;
        IStream *stream=SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.constData()),UINT(bytes.size()));
        if(!stream){++gdi_errors;}else{{Gdiplus::Bitmap bitmap(stream,FALSE);Gdiplus::BitmapData data{};Gdiplus::Rect rect(0,0,width,height);
            if(bitmap.GetLastStatus()!=Gdiplus::Ok||bitmap.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok)++gdi_errors;
            else bitmap.UnlockBits(&data);}stream->Release();}
        decode_ms+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    }}
};
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    if(argc!=4)throw std::runtime_error("Usage: frame-probe <port> <seconds: 1-60> <sender-ip>");
    const int port=std::stoi(argv[1]);const int seconds=std::stoi(argv[2]);
    const std::string sender=argv[3];
    if(port<1024||port>65535||seconds<1||seconds>60)throw std::runtime_error("invalid port or duration");
    if(vban::net::startup())throw std::runtime_error("Winsock startup");
    in_addr sender_address{};if(vban::net::parse(AF_INET,sender.c_str(),&sender_address)!=1)throw std::runtime_error("Invalid sender IPv4 address");
    auto socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(socket==vban::net::invalid)throw std::runtime_error("socket creation");
    if(!vban::net::exclusive(socket))throw std::runtime_error("Cannot reserve exclusive diagnostic port");const auto buffer_size=vban::net::receive_buffer(socket);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(uint16_t(port));
    if(bind(socket,reinterpret_cast<sockaddr*>(&local),sizeof(local)))throw std::runtime_error("Port already in use or unavailable: "+std::to_string(vban::net::error()));
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR token=0;if(Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok)throw std::runtime_error("GDI+ startup");
    Decoder decoder;
    uint64_t packets=0,bytes=0,starts=0,ends=0,complete=0,incomplete=0,missing=0,duplicates=0,invalid=0,changed_id=0,out_of_order=0,sequence_jumps=0,ignored=0;
    bool active=false,have_start=false;uint32_t frame=0,last_start=0;uint16_t last_index=0;std::map<uint16_t,QByteArray> pieces;size_t image_size=0;
    vban::FrameAssembler strict;
    QJsonArray sample_frames,arrival;uint32_t sampled_frame=0;
    std::set<int> source_ports;std::set<std::string> names;std::map<int,uint64_t> flags;QJsonArray headers;
    auto abandon=[&]{if(active){++incomplete;active=false;pieces.clear();image_size=0;}};
    const auto start=Clock::now();std::cerr<<"Listening on UDP "<<port<<" for "<<sender<<" for "<<seconds<<" seconds; no image files are saved.\n";
    while(Clock::now()-start<std::chrono::seconds(seconds)){
        if(vban::net::readable(socket,20)!=1)continue;
        char data[65536];sockaddr_in peer{};vban::net::Length length=sizeof(peer);
        const int n=recvfrom(socket,data,sizeof(data),0,reinterpret_cast<sockaddr*>(&peer),&length);if(n<0)continue;
        char ip[INET_ADDRSTRLEN]{};vban::net::format(AF_INET,&peer.sin_addr,ip,sizeof(ip));
        if(sender!=ip){++ignored;continue;}
        if(n<=28||std::memcmp(data,"VBAN",4)||(uint8_t(data[4])&0xe0)!=0x80){++ignored;continue;}
        ++packets;bytes+=uint64_t(n)+66;source_ports.insert(ntohs(peer.sin_port));
        std::string name(data+8,data+24);name.resize(name.find('\0')==std::string::npos?name.size():name.find('\0'));names.insert(name);
        const unsigned bits=uint8_t(data[7]);++flags[int(bits)];uint32_t id=0;std::memcpy(&id,data+24,4);const uint16_t index=uint16_t(uint8_t(data[5]))|(uint16_t(uint8_t(data[6]))<<8);
        strict.push(reinterpret_cast<const uint8_t*>(data),size_t(n),uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()));
        if(bits&1){if(!arrival.empty()&&sample_frames.size()<12)sample_frames.append(QJsonObject{{"frame",qint64(sampled_frame)},{"arrival_indices",arrival}});sampled_frame=id;arrival=QJsonArray();}
        if(sample_frames.size()<12&&arrival.size()<256)arrival.append(int(index));
        if(headers.size()<8)headers.append(QString::fromLatin1(QByteArray(data,28).toHex(' ')));
        if(n>1464||(bits!=1&&bits!=2&&bits!=4&&bits!=5&&bits!=6)||names.size()>1||source_ports.size()>1){++invalid;abandon();continue;}
        if(bits&1){++starts;abandon();if(have_start&&id!=last_start+1)++sequence_jumps;have_start=true;last_start=id;frame=id;last_index=0;active=index==0;pieces.clear();image_size=0;if(!active){++invalid;continue;}}
        if(!active)continue;if(id!=frame){++changed_id;abandon();continue;}
        if(pieces.count(index)){++duplicates;continue;}
        if(!pieces.empty()&&index!=uint16_t(last_index+1))++out_of_order;last_index=index;
        image_size+=size_t(n-28);if(image_size>8*1024*1024){++invalid;abandon();continue;}
        pieces.emplace(index,QByteArray(data+28,n-28));
        if(bits&4){++ends;bool contiguous=pieces.size()==size_t(index)+1;uint32_t expected=0;for(const auto &p:pieces)if(p.first!=expected++)contiguous=false;
            if(!contiguous){missing+=size_t(index)+1>pieces.size()?size_t(index)+1-pieces.size():0;abandon();continue;}
            QByteArray image;image.reserve(qsizetype(image_size));for(const auto &p:pieces)image+=p.second;
            ++complete;active=false;pieces.clear();image_size=0;decoder.put(std::move(image));}
    }
    const auto elapsed=std::chrono::duration<double>(Clock::now()-start).count();vban::net::close(socket);decoder.finish();Gdiplus::GdiplusShutdown(token);vban::net::cleanup();
    QJsonArray ports,streams;QJsonObject kinds;for(auto p:source_ports)ports.append(p);for(auto &s:names)streams.append(QString::fromStdString(s));for(auto [k,v]:flags)kinds[QString::number(k)]=qint64(v);
    QJsonObject result{{"strict_complete",qint64(strict.counters().completed)},{"strict_incomplete",qint64(strict.counters().incomplete)},{"strict_fps",double(strict.counters().completed)/elapsed},{"sample_frames",sample_frames},{"port",port},{"sender",QString::fromStdString(sender)},{"seconds",elapsed},{"receive_buffer",buffer_size},{"packets",qint64(packets)},{"wire_mbps",double(bytes)*8/elapsed/1e6},{"starts",qint64(starts)},{"ends",qint64(ends)},{"complete",qint64(complete)},{"complete_fps",double(complete)/elapsed},{"incomplete",qint64(incomplete)},{"missing_fragments",qint64(missing)},{"duplicates",qint64(duplicates)},{"invalid",qint64(invalid)},{"changed_frame_id",qint64(changed_id)},{"out_of_order",qint64(out_of_order)},{"frame_sequence_jumps",qint64(sequence_jumps)},{"ignored",qint64(ignored)},{"partial_image_at_stop",active},{"qt_decoded",qint64(decoder.decoded)},{"qt_errors",qint64(decoder.qt_errors)},{"gdi_errors",qint64(decoder.gdi_errors)},{"decoder_queue_drops",qint64(decoder.queue_drops)},{"mean_decode_both_ms",decoder.decoded?decoder.decode_ms/double(decoder.decoded):0},{"width",decoder.width},{"height",decoder.height},{"last_decode_error",QString::fromStdString(decoder.last_error)},{"source_ports",ports},{"streams",streams},{"flags",kinds},{"first_headers",headers}};
    std::cout<<QJsonDocument(result).toJson().toStdString();return packets?0:2;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
