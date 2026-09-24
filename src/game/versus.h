// S3 RALLY head-to-head over the local network.
//
// A host announces itself by UDP broadcast; players on the same network see
// it in their lobby and join. Each machine simulates its own car and sends
// its state every frame; the other car is drawn from those updates. This is
// the shape a future internet server will keep: only the transport changes.
//
// Protocol (one UDP datagram per message, see Packet):
//   ANNOUNCE  host -> broadcast   "a game is open": stage, host car, game port, version, host name + ID
//   HELLO     client -> host      "let me in": client car, name + ID (resent until welcomed)
//   WELCOME   host -> client      stage, host car, host name + ID
//   GO        host -> client      start the countdown now (sent a few times)
//   STATE     both ways, 60 Hz    position, speed, lap, finished, race time
//   BYE       either              leaving
#pragma once
#include <string>
#include <vector>

#include "console/link.h"

namespace rally {

constexpr uint16_t DISCOVERY_PORT = 47016;
constexpr uint16_t GAME_PORT = 47017;

struct HostInfo {
    gs::NetAddr addr;  // host's game port
    int stage = 0, car = 0;
    std::string version;
    std::string name, id;  // host player
    int age = 0;  // frames since last announcement
};

struct CarState {
    float dist = 0, x = 0, speed = 0, yaw = 0, time = 0;
    int lap = 0, car = 0;
    bool finished = false;
};

class Versus {
public:
    enum class Phase { Idle, Hosting, Searching, Joining, Ready, Lost };

    static bool available() { return gs::Link::available(); }
    bool host(int stage, int car, uint16_t port = GAME_PORT);
    bool search();
    void join(const gs::NetAddr& host, int car);
    void stop();
    void tick();  // pump the network once per frame
    void sendState(const CarState& me);
    void sendGo();       // host: start the countdown
    bool takeGo();       // client: true once when the host has started
    bool connected() const { return phase == Phase::Ready; }

    Phase phase = Phase::Idle;
    bool isHost = false;
    int stage = 0;
    int myCar = 0;
    CarState peer;
    bool peerSeen = false;     // received at least one STATE
    int framesSincePeer = 0;   // for extrapolation and timeouts
    std::vector<HostInfo> hosts;
    std::string myName, myId;      // our profile, sent when we announce, join or welcome
    std::string peerName, peerId;  // theirs
    uint16_t gamePort = 0;
    uint16_t discoveryPort = DISCOVERY_PORT;  // overridable (tests)

private:
    void handle(const void* data, int len, const gs::NetAddr& from);
    void sendType(int type);

    gs::Link game_, disco_;
    gs::NetAddr peerAddr_;
    long frame_ = 0;
    int goToSend_ = 0;
    bool goReceived_ = false;
    uint32_t seq_ = 0, peerSeq_ = 0;
};

}  // namespace rally
