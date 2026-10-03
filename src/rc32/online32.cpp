// (3) RALLY 32/64 online: the same races as (3) RALLY's, over the same protocol
// (src/game/versus.h) with the same packet signature, so a 16-bit, a 32-bit and a
// 64-bit console can all be in one race. The host is the hub; everyone simulates
// their own car and sends its place on the road; the others are drawn from that.
#include <algorithm>
#include <cstdio>

#include "rally32.h"

namespace rc32 {

namespace {
constexpr float DT = 1.0f / 60.0f;
}

bool Rally32::testHost(int stage, uint16_t port, int autoStart) {
    versus_.myName = myName_;
    if (!versus_.host(stage, carId_, port)) return false;
    autoStart_ = autoStart;
    attract_ = false;
    mode_ = Mode::Lobby;
    t_ = 0;
    return true;
}

bool Rally32::testJoin(const std::string& ip, uint16_t port, int car) {
    versus_.myName = myName_;
    carId_ = car;
    attract_ = false;
    mode_ = Mode::Lobby;
    t_ = 0;
    if (ip == "discover") return versus_.search();
    gs::NetAddr a;
    if (!gs::NetAddr::parse(ip, port, &a)) return false;
    versus_.join(a, carId_);
    return versus_.phase == rally::Versus::Phase::Joining;
}

// Everyone on the line: the host first, then a start every startGap_ seconds in slot order.
void Rally32::startOnline() {
    online_ = true;
    attract_ = false;
    loadStage(std::clamp(versus_.stage, 0, rc::NUM_STAGES - 1));
    car_.reset(course_, course_.startSeg, carId_);
    std::copy(rc::carSpec(carId_).livery, rc::carSpec(carId_).livery + 16, livery_);
    const int me = versus_.mySlot;
    startGo_ = 60 * 5 + me * int(60 * startGap_);
    others_.clear();
    for (int s = 0; s < rally::MAX_PLAYERS; s++) {
        if (s == me || !versus_.players[s].active) continue;
        Other o;
        o.slot = s;
        o.car = versus_.players[s].car;
        o.name = versus_.players[s].name;
        o.startAt = (s - me) * startGap_;
        o.s = car_.s;
        others_.push_back(o);
    }
    time_ = 0;
    mode_ = Mode::Start;
    t_ = 0;
}

// Once a frame: the lobby, our state out, everyone else's in.
void Rally32::updateOnline() {
    if (versus_.phase == rally::Versus::Phase::Idle) return;
    versus_.tick();
    const gs::Pad& pad = sys_->pad;
    if (mode_ == Mode::Lobby) {
        if (pad.pressed(gs::BTN_MODE) || versus_.phase == rally::Versus::Phase::Lost) {
            lobbyMsg_ = versus_.phase == rally::Versus::Phase::Lost ? versus_.lostReason : "";
            versus_.stop();
            mode_ = Mode::Pick;
            t_ = 0;
            return;
        }
        if (versus_.isHost) {
            const int n = versus_.playerCount();
            const bool go = (pad.pressed(gs::BTN_START) && t_ > 6) || (autoStart_ > 0 && n >= autoStart_ && t_ > 60);
            if (n >= 2 && go) {
                versus_.sendGo();
                startOnline();
            }
        } else if (versus_.phase == rally::Versus::Phase::Searching && !versus_.hosts.empty()) {
            versus_.join(versus_.hosts.front().addr, carId_);  // the first game found on the LAN
        } else if (versus_.takeGo()) {
            startOnline();
        }
        return;
    }
    if (versus_.phase == rally::Versus::Phase::Ready && online_) {
        rally::CarState me;
        me.dist = car_.s;
        me.x = car_.x;
        me.speed = car_.along();
        me.yaw = car_.psi;
        me.time = time_;
        me.car = carId_;
        me.lap = mode_ == Mode::Drive || mode_ == Mode::Done ? 1 : 0;
        me.finished = mode_ == Mode::Done;
        versus_.sendState(me);
    }
    for (Other& o : others_) {
        const rally::NetPlayer& p = versus_.players[o.slot];
        if (!p.seen) continue;
        const int age = std::min(p.framesSince, 30);  // carry it on a little between updates
        o.s = p.state.dist + p.state.speed * DT * age;
        o.x = p.state.x;
        o.psi = p.state.yaw;
        o.speed = p.state.speed;
        o.running = (p.state.lap > 0 || p.state.finished) && p.active;
        o.finished = p.state.finished;
    }
}

Rally32::VersusReport Rally32::versusReport() const {
    VersusReport r;
    r.finished = online_ && mode_ == Mode::Done;
    r.time = time_;
    r.mySlot = versus_.mySlot;
    r.racing = mode_ == Mode::Start || mode_ == Mode::Drive || mode_ == Mode::Done;
    for (int s = 0; s < rally::MAX_PLAYERS; s++) {
        const rally::NetPlayer& p = versus_.players[s];
        r.active[s] = p.active;
        r.seen[s] = p.seen;
        r.names[s] = p.name;
        r.ids[s] = p.id;
        r.finishedSlot[s] = p.state.finished;
        r.times[s] = p.state.time;
    }
    r.rank = 1;
    for (int s = 0; s < rally::MAX_PLAYERS; s++)
        if (s != r.mySlot && r.active[s] && r.finishedSlot[s] && r.times[s] < r.time - 0.05f) r.rank++;
    return r;
}

std::string Rally32::debugLine() const {
    char buf[160];
    std::snprintf(buf, sizeof buf, "mode %d seg %d/%d x %.2f u %.1f state %d", int(mode_), car_.segIndex(course_), course_.finishSeg, car_.x,
                  car_.u, int(car_.state));
    return buf;
}

}  // namespace rc32
