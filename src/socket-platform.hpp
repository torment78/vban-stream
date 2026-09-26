// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <initializer_list>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <mswsock.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vban::net {
#ifdef _WIN32
using Socket = SOCKET;
using Length = int;
constexpr Socket invalid = INVALID_SOCKET;
inline int startup() { WSADATA data{}; return WSAStartup(MAKEWORD(2, 2), &data); }
inline void cleanup() { WSACleanup(); }
inline void close(Socket s) { closesocket(s); }
inline int error() { return WSAGetLastError(); }
inline bool interrupted(int e) { return e == WSAEINTR; }
inline bool retry_receive(int e) { return e == WSAEWOULDBLOCK || e == WSAEMSGSIZE || e == WSAECONNRESET || interrupted(e); }
inline bool nonblocking(Socket s) { u_long mode=1; return ioctlsocket(s,FIONBIO,&mode)==0; }
inline bool exclusive(Socket s) { BOOL yes=TRUE; return setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&yes),sizeof(yes))==0; }
inline int readable(Socket s, int milliseconds) {
    fd_set fds; FD_ZERO(&fds); FD_SET(s,&fds);
    timeval timeout{milliseconds/1000,(milliseconds%1000)*1000};
    return select(0,&fds,nullptr,nullptr,&timeout);
}
inline bool select_interface(Socket s, uint32_t index) {
    const DWORD value=htonl(index);
    return setsockopt(s,IPPROTO_IP,IP_UNICAST_IF,reinterpret_cast<const char*>(&value),sizeof(value))==0;
}
inline bool ignore_port_unreachable(Socket s) {
    BOOL reset=FALSE; DWORD returned=0;
    return WSAIoctl(s,SIO_UDP_CONNRESET,&reset,sizeof(reset),nullptr,0,&returned,nullptr,nullptr)==0;
}
inline bool receive_timeout(Socket s, unsigned ms) {
    const DWORD value=ms;
    return setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&value),sizeof(value))==0;
}
inline int parse(int family,const char *text,void *address) { return InetPtonA(family,text,address); }
inline const char *format(int family,const void *address,char *text,size_t size) { return InetNtopA(family,address,text,size); }
#else
using Socket = int;
using Length = socklen_t;
constexpr Socket invalid = -1;
inline int startup() { return 0; }
inline void cleanup() {}
inline void close(Socket s) { ::close(s); }
inline int error() { return errno; }
inline bool interrupted(int e) { return e == EINTR; }
inline bool retry_receive(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == EMSGSIZE || e == ECONNRESET || e == ECONNREFUSED || interrupted(e); }
inline bool nonblocking(Socket s) { const int flags=fcntl(s,F_GETFL,0); return flags>=0 && fcntl(s,F_SETFL,flags|O_NONBLOCK)==0; }
// BSD sockets are exclusive by default: do not enable SO_REUSEADDR/SO_REUSEPORT.
inline bool exclusive(Socket) { return true; }
inline int readable(Socket s, int milliseconds) {
    pollfd fd{s,POLLIN,0};
    const int ready=poll(&fd,1,milliseconds);
    if (ready<=0) return ready;
    if (fd.revents&POLLNVAL) { errno=EBADF; return -1; }
    return (fd.revents&(POLLIN|POLLERR|POLLHUP)) ? 1 : 0;
}
inline bool select_interface(Socket s, uint32_t index) {
    const unsigned value=index; // Darwin's IP_BOUND_IF expects host byte order.
    return setsockopt(s,IPPROTO_IP,IP_BOUND_IF,&value,sizeof(value))==0;
}
// BSD reports an ICMP error on a send; the following datagram can recover normally.
inline bool ignore_port_unreachable(Socket) { return true; }
inline bool receive_timeout(Socket s, unsigned ms) {
    const timeval value{static_cast<time_t>(ms/1000),static_cast<suseconds_t>((ms%1000)*1000)};
    return setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&value,sizeof(value))==0;
}
inline int parse(int family,const char *text,void *address) { return inet_pton(family,text,address); }
inline const char *format(int family,const void *address,char *text,size_t size) { return inet_ntop(family,address,text,static_cast<socklen_t>(size)); }
#endif
constexpr int failure=-1;
// macOS caps socket buffers; request the largest available size without sysctl changes.
inline int receive_buffer(Socket s) {
    for (int size : {1024*1024,512*1024,256*1024,128*1024})
        if (setsockopt(s,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&size),sizeof(size))==0) break;
    int actual=0; Length length=sizeof(actual);
    return getsockopt(s,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<char*>(&actual),&length)==0 ? actual : 0;
}
}
