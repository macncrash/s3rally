#include "link.h"

#include <cstdio>

#ifndef __EMSCRIPTEN__
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#endif

namespace gs {

std::string NetAddr::str() const {
    char b[32];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return b;
}

bool NetAddr::parse(const std::string& dotted, uint16_t port, NetAddr* out) {
    unsigned a, b, c, d;
    char extra;
    if (std::sscanf(dotted.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    out->ip = (a << 24) | (b << 16) | (c << 8) | d;
    out->port = port;
    return true;
}

#ifdef __EMSCRIPTEN__

bool Link::available() { return false; }
bool Link::open(uint16_t, bool) { return false; }
void Link::close() {}
bool Link::send(const NetAddr&, const void*, size_t) { return false; }
bool Link::broadcast(uint16_t, const void*, size_t) { return false; }
int Link::recv(void*, size_t, NetAddr*) { return -1; }
std::string Link::localAddress() { return ""; }

#else

bool Link::available() { return true; }

bool Link::open(uint16_t port, bool reuse) {
    close();
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;
    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    if (reuse) {  // several listeners (e.g. two copies on one machine) can share the discovery port
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::fcntl(fd_, F_SETFL, O_NONBLOCK) != 0) {
        close();
        return false;
    }
    return true;
}

void Link::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

bool Link::send(const NetAddr& to, const void* data, size_t len) {
    if (fd_ < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(to.ip);
    a.sin_port = htons(to.port);
    return ::sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&a), sizeof a) == ssize_t(len);
}

bool Link::broadcast(uint16_t port, const void* data, size_t len) {
    if (fd_ < 0) return false;
    // Each interface's own broadcast address (e.g. 192.168.1.255): the generic
    // 255.255.255.255 is not reliably delivered on every OS, so send both.
    bool ok = false;
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_BROADCAST) || !(i->ifa_flags & IFF_UP) ||
                !i->ifa_broadaddr)
                continue;
            ok |= send({ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_broadaddr)->sin_addr.s_addr), port}, data, len);
        }
        ::freeifaddrs(list);
    }
    ok |= send({0xffffffffu, port}, data, len);
    // Also reach copies running on this machine (limited broadcast may not loop back).
    ok |= send({0x7f000001u, port}, data, len);
    return ok;
}

int Link::recv(void* buf, size_t cap, NetAddr* from) {
    if (fd_ < 0) return -1;
    sockaddr_in a{};
    socklen_t al = sizeof a;
    ssize_t n = ::recvfrom(fd_, buf, cap, 0, reinterpret_cast<sockaddr*>(&a), &al);
    if (n < 0) return -1;
    if (from) {
        from->ip = ntohl(a.sin_addr.s_addr);
        from->port = ntohs(a.sin_port);
    }
    return int(n);
}

std::string Link::localAddress() {
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) return "";
    std::string best;
    for (ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
        NetAddr n{ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr), 0};
        best = n.str();
        if (std::strncmp(i->ifa_name, "en", 2) == 0 || std::strncmp(i->ifa_name, "eth", 3) == 0 || std::strncmp(i->ifa_name, "wl", 2) == 0)
            break;  // prefer the main wired/wireless interface
    }
    ::freeifaddrs(list);
    return best;
}

#endif

}  // namespace gs
