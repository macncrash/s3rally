#include "versus.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "version.h"

namespace rally {

namespace {

constexpr uint8_t PROTOCOL = 3;  // 3: up to four players, relayed through the host
enum : uint8_t { ANNOUNCE = 1, HELLO, WELCOME, FULL, ROSTER, GO, STATE, PING, PONG, BYE, QUERY };
constexpr int TIMEOUT_FRAMES = 60 * 5;
constexpr uint8_t FLAG_ACTIVE = 1, FLAG_FINISHED = 2;

// S3_NET_DEBUG=1 logs the protocol to stderr.
bool netDebug() {
    static const bool on = std::getenv("S3_NET_DEBUG") != nullptr;
    return on;
}
#define NETLOG(...)                                                  \
    do {                                                             \
        if (netDebug()) std::fprintf(stderr, "[net] " __VA_ARGS__); \
    } while (0)

std::string field(const char* s, size_t cap) { return std::string(s, strnlen(s, cap)); }

}  // namespace

#pragma pack(push, 1)
struct Versus::Packet {
    char magic[4];
    uint8_t proto, type, stage, car, lap, flags, slot, count;
    uint16_t port;
    uint32_t seq, t, echo;
    float dist, x, speed, yaw, time;
    char version[16];
    char name[13];
    char id[37];
};
#pragma pack(pop)
static_assert(sizeof(Versus::Packet) == 112, "wire format changed");

bool Versus::host(int st, int car, uint16_t port) {
    stop();
    for (uint16_t p = port; p < port + 4; p++)  // a second copy on one machine can still host
        if (game_.open(p)) {
            gamePort = p;
            break;
        }
    // Listen for "any games here?" queries. Shared, in case another copy on this machine hosts too.
    if (!game_.isOpen() || !disco_.open(discoveryPort, true)) {
        stop();
        return false;
    }
    isHost = true;
    stage = st;
    myCar = car;
    mySlot = 0;
    players[0].active = true;
    players[0].name = myName;
    players[0].id = myId;
    players[0].car = car;
    phase = Phase::Hosting;
    NETLOG("hosting on port %d\n", gamePort);
    return true;
}

bool Versus::search() {
    stop();
    if (!game_.open(0)) {  // queries go out, and answers come back, on our own port
        NETLOG("could not open a socket to search\n");
        stop();
        return false;
    }
    isHost = false;
    phase = Phase::Searching;
    return true;
}

void Versus::join(const gs::NetAddr& host, int car) {
    if (!game_.isOpen() && !game_.open(0)) {
        lostReason = "COULD NOT OPEN THE NETWORK";
        phase = Phase::Lost;
        return;
    }
    // Fresh session state (keep the socket: discovery may have just opened it).
    for (auto& p : players) p = NetPlayer{};
    started = goReceived_ = false;
    seq_ = 0;
    mySlot = 0;
    hostSilence_ = 0;
    pingMs_ = -1;
    lostReason.clear();
    hostAddr_ = host;
    myCar = car;
    isHost = false;
    phase = Phase::Joining;
    frame_ = 0;
    NETLOG("joining %s:%d\n", host.str().c_str(), host.port);
}

void Versus::stop() {
    if (phase == Phase::Ready) {
        if (isHost) {
            for (int s = 1; s < MAX_PLAYERS; s++)
                if (players[s].active) send(players[s].addr, BYE, 0);
        } else {
            send(hostAddr_, BYE, mySlot);
        }
    }
    game_.close();
    disco_.close();
    phase = Phase::Idle;
    hosts.clear();
    for (auto& p : players) p = NetPlayer{};
    started = false;
    goToSend_ = 0;
    goReceived_ = false;
    seq_ = 0;
    mySlot = 0;
    hostSilence_ = 0;
    pingMs_ = -1;
}

bool Versus::connected() const {
    if (phase != Phase::Ready) return false;
    return !isHost || playerCount() > 1;
}

int Versus::playerCount() const {
    int n = 0;
    for (const auto& p : players) n += p.active;
    return n;
}

int Versus::pingMs() const {
    if (!isHost) return pingMs_;
    int worst = -1;
    for (int s = 1; s < MAX_PLAYERS; s++)
        if (players[s].active) worst = std::max(worst, players[s].pingMs);
    return worst;
}

int Versus::slotOf(const gs::NetAddr& a) const {
    for (int s = 1; s < MAX_PLAYERS; s++)
        if (players[s].active && players[s].addr == a) return s;
    return -1;
}

void Versus::send(const gs::NetAddr& to, int type, int slot) {
    Packet p{};
    std::memcpy(p.magic, magic, 4);
    p.proto = PROTOCOL;
    p.type = uint8_t(type);
    p.stage = uint8_t(stage);
    p.car = uint8_t(myCar);
    p.slot = uint8_t(slot < 0 ? mySlot : slot);
    p.count = uint8_t(playerCount());
    p.port = gamePort;
    p.t = uint32_t(frame_);
    std::strncpy(p.version, S3_VERSION, sizeof p.version - 1);
    std::strncpy(p.name, myName.c_str(), sizeof p.name - 1);
    std::strncpy(p.id, myId.c_str(), sizeof p.id - 1);
    game_.send(to, &p, sizeof p);
}

// Host: tell every player who is in each slot.
void Versus::sendRoster() {
    for (int s = 0; s < MAX_PLAYERS; s++) {
        Packet p{};
        std::memcpy(p.magic, magic, 4);
        p.proto = PROTOCOL;
        p.type = ROSTER;
        p.stage = uint8_t(stage);
        p.slot = uint8_t(s);
        p.car = uint8_t(players[s].car);
        p.flags = players[s].active ? FLAG_ACTIVE : 0;
        p.count = uint8_t(playerCount());
        std::strncpy(p.name, players[s].name.c_str(), sizeof p.name - 1);
        std::strncpy(p.id, players[s].id.c_str(), sizeof p.id - 1);
        for (int c = 1; c < MAX_PLAYERS; c++)
            if (players[c].active) game_.send(players[c].addr, &p, sizeof p);
    }
}

void Versus::sendState(const CarState& me) {
    if (phase != Phase::Ready) return;
    Packet p{};
    std::memcpy(p.magic, magic, 4);
    p.proto = PROTOCOL;
    p.type = STATE;
    p.slot = uint8_t(mySlot);
    p.seq = ++seq_;
    p.car = uint8_t(me.car);
    p.lap = uint8_t(std::clamp(me.lap, 0, 255));
    p.flags = FLAG_ACTIVE | (me.finished ? FLAG_FINISHED : 0);
    p.dist = me.dist;
    p.x = me.x;
    p.speed = me.speed;
    p.yaw = me.yaw;
    p.time = me.time;
    if (isHost) {
        for (int s = 1; s < MAX_PLAYERS; s++)
            if (players[s].active) game_.send(players[s].addr, &p, sizeof p);
    } else {
        game_.send(hostAddr_, &p, sizeof p);
    }
}

void Versus::sendGo() {
    goToSend_ = 6;  // a few copies, in case one is dropped
    started = true;
}

bool Versus::takeGo() {
    const bool g = goReceived_;
    goReceived_ = false;
    return g;
}

void Versus::handle(const void* data, int len, const gs::NetAddr& from) {
    if (len != int(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, data, sizeof p);
    if (std::memcmp(p.magic, magic, 4) != 0 || p.proto != PROTOCOL) return;  // another game, or another version
    NETLOG("%s got type %d slot %d from %s:%d\n", isHost ? "host" : "player", p.type, p.slot, from.str().c_str(), from.port);

    if (p.type == QUERY) {  // someone is looking for games: answer them directly
        if (isHost && !started && (phase == Phase::Hosting || phase == Phase::Ready)) {
            Packet a{};
            std::memcpy(a.magic, magic, 4);
            a.proto = PROTOCOL;
            a.type = ANNOUNCE;
            a.stage = uint8_t(stage);
            a.car = uint8_t(myCar);
            a.count = uint8_t(playerCount());
            a.port = gamePort;
            std::strncpy(a.version, S3_VERSION, sizeof a.version - 1);
            std::strncpy(a.name, myName.c_str(), sizeof a.name - 1);
            std::strncpy(a.id, myId.c_str(), sizeof a.id - 1);
            disco_.send(from, &a, sizeof a);
        }
        return;
    }
    if (p.type == ANNOUNCE) {
        if (phase != Phase::Searching) return;
        gs::NetAddr a{from.ip, p.port};
        auto it = std::find_if(hosts.begin(), hosts.end(), [&](const HostInfo& h) { return h.addr == a; });
        if (it == hosts.end()) it = hosts.insert(hosts.end(), HostInfo{a});
        it->stage = p.stage;
        it->players = p.count;
        it->version = field(p.version, sizeof p.version);
        it->name = field(p.name, sizeof p.name);
        it->id = field(p.id, sizeof p.id);
        it->age = 0;
        return;
    }

    if (isHost) {
        const int s = slotOf(from);
        if (s > 0) players[s].framesSince = 0;
        switch (p.type) {
            case HELLO: {
                if (phase != Phase::Hosting && phase != Phase::Ready) return;
                int slot = s;
                if (slot < 0) {
                    if (started) {
                        send(from, FULL, 0);
                        return;
                    }
                    for (int i = 1; i < MAX_PLAYERS && slot < 0; i++)
                        if (!players[i].active) slot = i;
                    if (slot < 0) {
                        send(from, FULL, 0);
                        return;
                    }
                    NetPlayer& np = players[slot];
                    np = NetPlayer{};
                    np.active = true;
                    np.addr = from;
                    np.name = field(p.name, sizeof p.name);
                    np.id = field(p.id, sizeof p.id);
                    np.car = p.car;
                    phase = Phase::Ready;
                    NETLOG("player %s joined in slot %d\n", np.name.c_str(), slot);
                }
                send(from, WELCOME, slot);
                sendRoster();
                break;
            }
            case STATE:
                if (s < 0 || p.slot != s || p.seq <= players[s].seq) return;  // unknown sender, spoofed slot or stale
                players[s].seq = p.seq;
                players[s].car = p.car;
                players[s].state = {p.dist, p.x, p.speed, p.yaw, p.time, p.lap, p.car, (p.flags & FLAG_FINISHED) != 0};
                players[s].seen = true;
                for (int c = 1; c < MAX_PLAYERS; c++)  // relay to everyone else
                    if (c != s && players[c].active) game_.send(players[c].addr, &p, sizeof p);
                break;
            case PING:
                if (s > 0) {
                    Packet r = p;
                    r.type = PONG;
                    r.echo = p.t;
                    game_.send(from, &r, sizeof r);
                }
                break;
            case PONG:
                if (s > 0) players[s].pingMs = int((frame_ - long(p.echo)) * 1000 / 60);
                break;
            case BYE:
                if (s > 0) {
                    NETLOG("player %s left\n", players[s].name.c_str());
                    players[s].active = false;
                    sendRoster();
                }
                break;
        }
        return;
    }

    // Player side: only the host may talk to us.
    if (!(from == hostAddr_)) return;
    hostSilence_ = 0;
    switch (p.type) {
        case WELCOME:
            if (phase == Phase::Joining) {
                mySlot = p.slot > 0 && p.slot < MAX_PLAYERS ? p.slot : 1;
                stage = p.stage;
                phase = Phase::Ready;
                NETLOG("welcomed into slot %d\n", mySlot);
            }
            break;
        case FULL:
            if (phase == Phase::Joining) {
                lostReason = "THAT GAME IS FULL OR RACING";
                phase = Phase::Lost;
            }
            break;
        case ROSTER:
            if (p.slot < MAX_PLAYERS) {
                NetPlayer& np = players[p.slot];
                const bool wasActive = np.active;
                np.active = (p.flags & FLAG_ACTIVE) != 0;
                np.name = field(p.name, sizeof p.name);
                np.id = field(p.id, sizeof p.id);
                np.car = p.car;
                if (np.active && !wasActive) np.framesSince = 0;
            }
            break;
        case GO:
            if (phase == Phase::Ready) {
                goReceived_ = true;
                started = true;
            }
            break;
        case STATE:
            if (p.slot < MAX_PLAYERS && p.slot != mySlot && p.seq > players[p.slot].seq) {
                NetPlayer& np = players[p.slot];
                np.seq = p.seq;
                np.car = p.car;
                np.state = {p.dist, p.x, p.speed, p.yaw, p.time, p.lap, p.car, (p.flags & FLAG_FINISHED) != 0};
                np.seen = true;
                np.framesSince = 0;
            }
            break;
        case PING: {
            Packet r = p;
            r.type = PONG;
            r.echo = p.t;
            game_.send(from, &r, sizeof r);
            break;
        }
        case PONG:
            pingMs_ = int((frame_ - long(p.echo)) * 1000 / 60);
            break;
        case BYE:
            lostReason = "THE HOST LEFT";
            phase = Phase::Lost;
            break;
    }
}

void Versus::tick() {
    if (phase == Phase::Idle) return;
    frame_++;
    char buf[256];
    gs::NetAddr from;
    int n;
    for (int guard = 0; guard < 256 && (n = game_.recv(buf, sizeof buf, &from)) >= 0; guard++) handle(buf, n, from);
    for (int guard = 0; guard < 64 && (n = disco_.recv(buf, sizeof buf, &from)) >= 0; guard++) handle(buf, n, from);

    for (auto& p : players) p.framesSince++;
    if (isHost && (phase == Phase::Hosting || phase == Phase::Ready)) {
        if (frame_ % 30 == 15) {
            sendRoster();
            for (int s = 1; s < MAX_PLAYERS; s++)
                if (players[s].active) send(players[s].addr, PING, 0);
        }
        if (goToSend_ > 0) {
            goToSend_--;
            for (int s = 1; s < MAX_PLAYERS; s++)
                if (players[s].active) send(players[s].addr, GO, 0);
        }
        bool dropped = false;
        for (int s = 1; s < MAX_PLAYERS; s++)
            if (players[s].active && players[s].framesSince > TIMEOUT_FRAMES) {
                NETLOG("player %s timed out\n", players[s].name.c_str());
                players[s].active = false;
                dropped = true;
            }
        if (dropped) sendRoster();
        return;
    }

    switch (phase) {
        case Phase::Searching:
            if (frame_ % 30 == 1) {  // ask the network (and this machine) for games
                Packet q{};
                std::memcpy(q.magic, magic, 4);
                q.proto = PROTOCOL;
                q.type = QUERY;
                game_.broadcast(discoveryPort, &q, sizeof q);
            }
            for (auto& h : hosts) h.age++;
            hosts.erase(std::remove_if(hosts.begin(), hosts.end(), [](const HostInfo& h) { return h.age > 150; }), hosts.end());
            break;
        case Phase::Joining:
            if (frame_ % 20 == 1) send(hostAddr_, HELLO);
            if (frame_ > 60 * 8) {
                lostReason = "NO ANSWER FROM THAT ADDRESS";
                phase = Phase::Lost;
            }
            break;
        case Phase::Ready:
            if (frame_ % 30 == 7) send(hostAddr_, PING);
            if (++hostSilence_ > TIMEOUT_FRAMES) {
                lostReason = "LOST CONTACT WITH THE HOST";
                phase = Phase::Lost;
            }
            break;
        default:
            break;
    }
}

}  // namespace rally
