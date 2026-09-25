// S3 RALLY head-to-head: up to four players over a local network or
// the internet (dial the host's address), no server needed.
//
// The host is the hub: players send it their car state, and it relays every
// state to everyone else, so only the host needs a reachable port. Each
// machine simulates its own car; the others are drawn from those updates.
// A future internet server takes the host's place without the game changing.
//
// Protocol v3 (one UDP datagram per message, see Packet in versus.cpp):
//   QUERY     player -> broadcast    any games here? (sent every half second while searching)
//   ANNOUNCE  host -> that player    a game is open: stage, player count, port, host name
//   HELLO     player -> host         join request: car, name, ID (resent until answered)
//   WELCOME   host -> player         you're in: your slot and the stage
//   FULL      host -> player         no room (4 players) or already racing
//   ROSTER    host -> players        one per slot: who is in the game (sent periodically)
//   GO        host -> players        start the countdown now (sent a few times)
//   STATE     player -> host -> all  position, speed, lap, finish, race time (60 Hz)
//   PING/PONG both ways              round-trip time
//   BYE       either                 leaving
#pragma once
#include <string>
#include <vector>

#include "console/link.h"

namespace rally {

constexpr uint16_t DISCOVERY_PORT = 47016;
constexpr uint16_t GAME_PORT = 47017;
constexpr int MAX_PLAYERS = 4;

struct HostInfo {
    gs::NetAddr addr;  // host's game port
    int stage = 0, players = 1;
    std::string version, name, id;
    int age = 0;  // frames since last announcement
};

struct CarState {
    float dist = 0, x = 0, speed = 0, yaw = 0, time = 0;
    int lap = 0, car = 0;
    bool finished = false;
};

struct NetPlayer {
    bool active = false;
    std::string name, id;
    int car = 0;
    CarState state;
    bool seen = false;       // received at least one STATE
    int framesSince = 0;     // since the last message from them (host) or their state (relayed)
    uint32_t seq = 0;        // last STATE sequence number accepted
    gs::NetAddr addr;        // host only: where this player is
    int pingMs = -1;         // host only: round trip to this player
};

class Versus {
public:
    enum class Phase { Idle, Hosting, Searching, Joining, Ready, Lost };

    static bool available() { return gs::Link::available(); }
    bool host(int stage, int car, uint16_t port = GAME_PORT);
    bool search();                                     // LAN discovery
    void join(const gs::NetAddr& host, int car);       // by discovery or typed address
    void stop();
    void tick();                                       // pump the network once per frame
    void sendState(const CarState& me);
    void sendGo();                                     // host: start the countdown for everyone
    bool takeGo();                                     // player: true once when the host has started
    bool connected() const;                            // host: someone joined; player: welcomed
    int playerCount() const;
    int pingMs() const;                                // player: to the host; host: worst player

    Phase phase = Phase::Idle;
    bool isHost = false;
    bool started = false;  // countdown begun: no more joins
    int stage = 0;
    int myCar = 0;
    int mySlot = 0;
    std::string myName, myId;
    std::string lostReason;
    NetPlayer players[MAX_PLAYERS];  // slot 0 is the host
    std::vector<HostInfo> hosts;
    uint16_t gamePort = 0;
    uint16_t discoveryPort = DISCOVERY_PORT;  // overridable (tests)
    // Packet signature: each cartridge has its own, so games never see each other's sessions.
    char magic[5] = "GSR1";

    struct Packet;

private:
    void handle(const void* data, int len, const gs::NetAddr& from);
    void send(const gs::NetAddr& to, int type, int slot = -1);
    void sendRoster();
    int slotOf(const gs::NetAddr& a) const;

    gs::Link game_, disco_;
    gs::NetAddr hostAddr_;  // player: the host
    long frame_ = 0;
    int goToSend_ = 0;
    bool goReceived_ = false;
    uint32_t seq_ = 0;
    int hostSilence_ = 0;  // player: frames since anything arrived from the host
    int pingMs_ = -1;      // player: to the host
};

}  // namespace rally
