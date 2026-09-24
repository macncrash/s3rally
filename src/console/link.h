// S3-16 link port: a tiny non-blocking UDP interface for local-network
// play (the console's answer to a link cable). Not available in a browser,
// which can't open UDP sockets; available() then returns false.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace gs {

struct NetAddr {
    uint32_t ip = 0;    // host byte order
    uint16_t port = 0;  // host byte order
    bool operator==(const NetAddr& o) const { return ip == o.ip && port == o.port; }
    std::string str() const;  // "192.168.1.20"
    static bool parse(const std::string& dotted, uint16_t port, NetAddr* out);
};

class Link {
public:
    ~Link() { close(); }
    static bool available();
    bool open(uint16_t port, bool reuse = false);  // port 0 = any free port
    void close();
    bool isOpen() const { return fd_ >= 0; }
    bool send(const NetAddr& to, const void* data, size_t len);
    bool broadcast(uint16_t port, const void* data, size_t len);
    // Non-blocking: returns bytes received, or -1 if nothing is waiting.
    int recv(void* buf, size_t cap, NetAddr* from);
    static std::string localAddress();  // best guess at this machine's LAN address

private:
    int fd_ = -1;
};

}  // namespace gs
