#include "versus.h"

#include <algorithm>
#include <cstring>

#include "version.h"

namespace rally {

namespace {

constexpr uint8_t PROTOCOL = 1;
enum : uint8_t { ANNOUNCE = 1, HELLO, WELCOME, GO, STATE, BYE };
constexpr int TIMEOUT_FRAMES = 60 * 4;

#pragma pack(push, 1)
struct Packet {
    char magic[4];
    uint8_t proto, type, stage, car, lap, finished;
    uint16_t port;
    uint32_t seq;
    float dist, x, speed, yaw, time;
    char version[16];
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 52, "wire format changed");

Packet make(uint8_t type) {
    Packet p{};
    std::memcpy(p.magic, "GSR1", 4);
    p.proto = PROTOCOL;
    p.type = type;
    std::strncpy(p.version, S3_VERSION, sizeof p.version - 1);
    return p;
}

}  // namespace

bool Versus::host(int st, int car, uint16_t port) {
    stop();
    // Try a few ports so a second copy on the same machine can also host.
    for (uint16_t p = port; p < port + 4; p++)
        if (game_.open(p)) {
            gamePort = p;
            break;
        }
    if (!game_.isOpen() || !disco_.open(0)) {
        stop();
        return false;
    }
    isHost = true;
    stage = st;
    myCar = car;
    phase = Phase::Hosting;
    return true;
}

bool Versus::search() {
    stop();
    if (!disco_.open(discoveryPort, true) || !game_.open(0)) {
        stop();
        return false;
    }
    isHost = false;
    phase = Phase::Searching;
    return true;
}

void Versus::join(const gs::NetAddr& host, int car) {
    if (!game_.isOpen() && !game_.open(0)) return;
    peerAddr_ = host;
    myCar = car;
    isHost = false;
    phase = Phase::Joining;
    frame_ = 0;
}

void Versus::stop() {
    if (phase == Phase::Ready) sendType(BYE);
    game_.close();
    disco_.close();
    phase = Phase::Idle;
    hosts.clear();
    peer = CarState{};
    peerSeen = false;
    framesSincePeer = 0;
    goToSend_ = 0;
    goReceived_ = false;
    seq_ = peerSeq_ = 0;
}

void Versus::sendType(int type) {
    Packet p = make(uint8_t(type));
    p.stage = uint8_t(stage);
    p.car = uint8_t(myCar);
    game_.send(peerAddr_, &p, sizeof p);
}

void Versus::sendState(const CarState& me) {
    if (phase != Phase::Ready) return;
    Packet p = make(STATE);
    p.seq = ++seq_;
    p.car = uint8_t(me.car);
    p.lap = uint8_t(std::clamp(me.lap, 0, 255));
    p.finished = me.finished;
    p.dist = me.dist;
    p.x = me.x;
    p.speed = me.speed;
    p.yaw = me.yaw;
    p.time = me.time;
    game_.send(peerAddr_, &p, sizeof p);
}

void Versus::sendGo() { goToSend_ = 6; }  // a few copies, in case one is dropped

bool Versus::takeGo() {
    const bool g = goReceived_;
    goReceived_ = false;
    return g;
}

void Versus::handle(const void* data, int len, const gs::NetAddr& from) {
    if (len != int(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, data, sizeof p);
    if (std::memcmp(p.magic, "GSR1", 4) != 0 || p.proto != PROTOCOL) return;
    switch (p.type) {
        case ANNOUNCE: {
            if (phase != Phase::Searching) break;
            gs::NetAddr a{from.ip, p.port};
            auto it = std::find_if(hosts.begin(), hosts.end(), [&](const HostInfo& h) { return h.addr == a; });
            if (it == hosts.end()) it = hosts.insert(hosts.end(), HostInfo{a});
            it->stage = p.stage;
            it->car = p.car;
            it->version.assign(p.version, strnlen(p.version, sizeof p.version));
            it->age = 0;
            break;
        }
        case HELLO:
            if (!isHost) break;
            if (phase == Phase::Hosting || (phase == Phase::Ready && from == peerAddr_)) {
                peerAddr_ = from;
                peer.car = p.car;
                phase = Phase::Ready;
                framesSincePeer = 0;
                sendType(WELCOME);
            }
            break;
        case WELCOME:
            if (phase == Phase::Joining && from == peerAddr_) {
                stage = p.stage;
                peer.car = p.car;
                phase = Phase::Ready;
                framesSincePeer = 0;
            }
            break;
        case GO:
            if (!isHost && phase == Phase::Ready && from == peerAddr_) goReceived_ = true;
            break;
        case STATE:
            if (phase != Phase::Ready || !(from == peerAddr_) || p.seq <= peerSeq_) break;  // drop stale/out-of-order
            peerSeq_ = p.seq;
            peer.dist = p.dist;
            peer.x = p.x;
            peer.speed = p.speed;
            peer.yaw = p.yaw;
            peer.time = p.time;
            peer.lap = p.lap;
            peer.car = p.car;
            peer.finished = p.finished != 0;
            peerSeen = true;
            framesSincePeer = 0;
            break;
        case BYE:
            if (from == peerAddr_ && phase == Phase::Ready) phase = Phase::Lost;
            break;
    }
}

void Versus::tick() {
    if (phase == Phase::Idle) return;
    frame_++;
    char buf[256];
    gs::NetAddr from;
    int n;
    for (int guard = 0; guard < 64 && (n = game_.recv(buf, sizeof buf, &from)) >= 0; guard++) handle(buf, n, from);
    for (int guard = 0; guard < 64 && (n = disco_.recv(buf, sizeof buf, &from)) >= 0; guard++) handle(buf, n, from);

    switch (phase) {
        case Phase::Hosting:
            if (frame_ % 30 == 1) {
                Packet p = make(ANNOUNCE);
                p.stage = uint8_t(stage);
                p.car = uint8_t(myCar);
                p.port = gamePort;
                disco_.broadcast(discoveryPort, &p, sizeof p);
            }
            break;
        case Phase::Searching:
            for (auto& h : hosts) h.age++;
            hosts.erase(std::remove_if(hosts.begin(), hosts.end(), [](const HostInfo& h) { return h.age > 150; }), hosts.end());
            break;
        case Phase::Joining:
            if (frame_ % 20 == 1) sendType(HELLO);
            if (frame_ > 60 * 6) phase = Phase::Lost;  // nobody answered
            break;
        case Phase::Ready:
            if (goToSend_ > 0) {
                goToSend_--;
                sendType(GO);
            }
            if (++framesSincePeer > TIMEOUT_FRAMES) phase = Phase::Lost;
            break;
        default:
            break;
    }
}

}  // namespace rally
