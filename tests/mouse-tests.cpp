// SPDX-License-Identifier: GPL-2.0-or-later
#include "socket-platform.hpp"
#include "mouse-return.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace vban;
static void check(bool b, const char *s) { if (!b) throw std::runtime_error(s); }
struct Listener {
    net::Socket socket = net::invalid;
    uint16_t port = 0;
    Listener() {
        socket = ::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        check(bind(socket,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0,"Bind loopback");
        net::Length len=sizeof(a);getsockname(socket,reinterpret_cast<sockaddr*>(&a),&len);port=ntohs(a.sin_port);
    }
    ~Listener(){if(socket!=net::invalid)net::close(socket);}
    std::string receive(const char *name, uint32_t &sequence) {
        check(net::readable(socket,1000)==1,"Expected mouse packet");
        char data[1464]{};const int n=recv(socket,data,sizeof(data),0);
        check(n>28&&!std::memcmp(data,"VBAN\x52\0\0\x10",8),"VBAN TEXT UTF8 header");
        check(std::string(data+8,std::strlen(name))==name,"Independent command stream");
        uint32_t seq=0;for(unsigned i=0;i<4;++i)seq|=uint32_t(uint8_t(data[24+i]))<<(i*8);
        check(seq==sequence++,"Monotonic packet counter");return std::string(data+28,data+n);
    }
};
int main(){try{
    MouseReturn mouse; Listener a,b;
    auto packet=mouse_packet("1234567890123456",0xffffffff,MouseAction::move,4095,4095);
    check(packet.size()==65,"Known command length without trailing NUL");
    check(packet[24]==255&&packet[27]==255&&packet[23]=='6',"16-byte name and counter wrap encoding");
    check(mouse_packet("bad\n",0,MouseAction::move,1,2).empty(),"Reject invalid stream name");
    check(mouse_packet("Command1",0,MouseAction::move,-1,2).empty(),"Reject negative coordinate");
    check(mouse_packet("Command1",0,MouseAction::move,4096,2).empty(),"Reject outside 4K coordinate");
    check(!mouse.press(0,false,1,2),"Disabled by default");
    MouseConfigs cfg;cfg[0]={true,"127.0.0.1",a.port,"Command1"};cfg[1]={true,"127.0.0.1",b.port,"Mixer2"};
    std::string error;auto route=mouse.prepare(cfg,{},error);check(bool(route)&&error.empty(),"Prepare two independent routes");mouse.activate(route);
    uint32_t seq0=0,seq1=0;
    check(mouse.press(0,false,23,47)&&mouse.dragging(),"Press begins drag");
    check(a.receive("Command1",seq0)=="System.Mouse=(MOUSEMOVE, 23, 47);","Exact VBAN-Screen movement syntax");
    check(a.receive("Command1",seq0)=="System.Mouse=(LBUTTONDOWN, 23, 47);","Left down");
    check(!mouse.move(1,30,40),"Drag cannot jump to another return");
    check(net::readable(b.socket,10)==0,"Other receiver untouched during drag");
    check(mouse.move(0,23,90),"Drag move");check(a.receive("Command1",seq0)=="System.Mouse=(MOUSEMOVE, 23, 90);","Drag coordinates");
    auto invalid=cfg;invalid[0].destination_ip="wrong";check(!mouse.prepare(invalid,{},error),"Reject invalid IP transactionally");
    mouse.release();for(int i=0;i<3;++i)check(a.receive("Command1",seq0)=="System.Mouse=(LBUTTONUP, 23, 90);","Redundant button-up to original route");
    check(!mouse.dragging(),"Released state");mouse.release();check(net::readable(a.socket,10)==0,"Repeated local cancellation sends no extra sequence");
    check(mouse.press(1,true,1,2),"Second return right press");b.receive("Mixer2",seq1);
    check(b.receive("Mixer2",seq1)=="System.Mouse=(RBUTTONDOWN, 1, 2);","Right down");
    MouseConfigs disabled;auto off=mouse.prepare(disabled,{},error);mouse.activate(off);
    for(int i=0;i<3;++i)check(b.receive("Mixer2",seq1)=="System.Mouse=(RBUTTONUP, 1, 2);","Route change releases OLD destination");
    check(!mouse.move(1,1,2)&&!mouse.status(1).enabled,"Disabled route stops transmission");
    invalid=cfg;invalid[0].stream_name=std::string(17,'a');check(!mouse.prepare(invalid,{},error),"Reject overlong stream name");
    invalid=cfg;invalid[0].port=0;check(!mouse.prepare(invalid,{},error),"Reject port zero");
    invalid=cfg;invalid[1]=invalid[0];check(!mouse.prepare(invalid,{},error),"Reject duplicate command destinations");
    check(!mouse.prepare(cfg,"192.0.2.222",error),"Reject unavailable source adapter");
    std::cout<<"PASS: VBAN mouse wire format, two loopback routes, drags, cancellation, transactional validation and button release.\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
