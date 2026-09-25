// (3) RALLY - online: the same stage for up to four players,
// starting ten seconds apart like a real rally, over the network protocol the
// console already speaks (host is the hub; LAN discovery or IP:port).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game.h"

namespace rc {

namespace {
constexpr float DT = 1.0f / 60.0f;
constexpr float HALF = gs::SCREEN_W / 2.0f;
}  // namespace

void RallyChamp::updateLobby(bool confirm, bool back) {
    const gs::Pad& pad = sys_->pad;
    versus_.myName = profile_.name;
    versus_.myId = profile_.id;
    auto fail = [&](const std::string& why) {
        versus_.stop();
        toast_ = why;
        toastT_ = 220;
        lobbyStep_ = 0;
        t_ = 0;
    };
    switch (lobbyStep_) {
        case 0:
            if (pad.pressed(gs::BTN_UP)) { lobbySel_ = (lobbySel_ + 2) % 3; sfx_->menuMove(); }
            if (pad.pressed(gs::BTN_DOWN)) { lobbySel_ = (lobbySel_ + 1) % 3; sfx_->menuMove(); }
            if (back) { mode_ = Mode::Menu; t_ = 0; break; }
            if (!confirm || t_ < 6) break;
            sfx_->menuSelect();
            t_ = 0;
            if (lobbySel_ == 0) {
                lobbyStep_ = 1;
                pickSel_ = std::clamp(pickSel_, 0, NUM_STAGES - 1);
            } else if (lobbySel_ == 1) {
                if (versus_.search()) { lobbyStep_ = 3; lobbySel_ = 0; }
                else fail("COULD NOT OPEN THE NETWORK");
            } else {
                addrEdit_.clear();
                for (char c : sys_->loadBlob("last-address.txt"))
                    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ':') addrEdit_ += c;
                sys_->typed.clear();
                lobbyStep_ = 6;
            }
            break;
        case 1:  // the host picks the stage
            if (pad.pressed(gs::BTN_UP) || pad.pressed(gs::BTN_LEFT)) { pickSel_ = (pickSel_ + NUM_STAGES - 1) % NUM_STAGES; sfx_->menuMove(); }
            if (pad.pressed(gs::BTN_DOWN) || pad.pressed(gs::BTN_RIGHT)) { pickSel_ = (pickSel_ + 1) % NUM_STAGES; sfx_->menuMove(); }
            if (back) { lobbyStep_ = 0; t_ = 0; break; }
            if (confirm && t_ > 5) {
                sfx_->menuSelect();
                if (versus_.host(pickSel_, carId_)) { lobbyStep_ = 2; t_ = 0; }
                else fail("COULD NOT OPEN THE NETWORK");
            }
            break;
        case 2: {
            if (back) { versus_.stop(); lobbyStep_ = 1; t_ = 0; break; }
            const int n = versus_.playerCount();
            const bool autoGo = autoStart_ > 0 && n >= autoStart_ && t_ > 60;
            if (n >= 2 && ((confirm && t_ > 5) || autoGo)) {
                versus_.sendGo();
                startOnlineStage();
            }
            break;
        }
        case 3: {
            const int n = int(versus_.hosts.size());
            if (n) {
                if (pad.pressed(gs::BTN_UP)) { lobbySel_ = (lobbySel_ + n - 1) % n; sfx_->menuMove(); }
                if (pad.pressed(gs::BTN_DOWN)) { lobbySel_ = (lobbySel_ + 1) % n; sfx_->menuMove(); }
                lobbySel_ = std::min(lobbySel_, n - 1);
            }
            if (back) { versus_.stop(); lobbyStep_ = 0; t_ = 0; }
            else if (confirm && t_ > 5 && n) {
                sfx_->menuSelect();
                versus_.join(versus_.hosts[size_t(lobbySel_)].addr, carId_);
                lobbyStep_ = 4;
                t_ = 0;
            }
            break;
        }
        case 4:
            if (back) { versus_.stop(); lobbyStep_ = 0; t_ = 0; }
            else if (versus_.connected()) { sfx_->checkpoint(); lobbyStep_ = 5; t_ = 0; }
            else if (versus_.phase == rally::Versus::Phase::Lost) fail(versus_.lostReason);
            break;
        case 5:
            if (back) { versus_.stop(); lobbyStep_ = 0; t_ = 0; }
            else if (versus_.phase == rally::Versus::Phase::Lost) fail(versus_.lostReason);
            else if (versus_.takeGo()) startOnlineStage();
            break;
        case 6: {
            static const std::string charset = "0123456789.:";
            for (char c : sys_->typed) {
                if (c == '\b') {
                    if (!addrEdit_.empty()) addrEdit_.pop_back();
                } else if ((std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ':') && addrEdit_.size() < 21) {
                    addrEdit_ += c;
                }
            }
            sys_->typed.clear();
            size_t at = charset.find(pendingChar_);
            if (at == std::string::npos) at = 0;
            if (pad.pressed(gs::BTN_UP)) pendingChar_ = charset[(at + charset.size() - 1) % charset.size()];
            if (pad.pressed(gs::BTN_DOWN)) pendingChar_ = charset[(at + 1) % charset.size()];
            if (pad.pressed(gs::BTN_RIGHT) && addrEdit_.size() < 21) addrEdit_ += pendingChar_;
            if (pad.pressed(gs::BTN_LEFT) && !addrEdit_.empty()) addrEdit_.pop_back();
            if (back) { lobbyStep_ = 0; t_ = 0; break; }
            if (!pad.pressed(gs::BTN_START) || t_ < 6) break;
            gs::NetAddr a;
            const size_t colon = addrEdit_.find(':');
            const std::string ip = addrEdit_.substr(0, colon);
            const long port = colon == std::string::npos ? rally::GAME_PORT : std::strtol(addrEdit_.c_str() + colon + 1, nullptr, 10);
            if (port < 1 || port > 65535 || !gs::NetAddr::parse(ip, uint16_t(port), &a)) {
                toast_ = "TRY AN ADDRESS LIKE 203.0.113.5:47017";
                toastT_ = 200;
                break;
            }
            sys_->saveBlob("last-address.txt", addrEdit_);
            sfx_->menuSelect();
            versus_.join(a, carId_);
            lobbyStep_ = 4;
            t_ = 0;
            break;
        }
    }
}

void RallyChamp::startOnlineStage() {
    game_ = Game::Online;
    attract_ = false;
    car_.damage = Damage{};
    loadStage(std::clamp(versus_.stage, 0, NUM_STAGES - 1));
    // Ten seconds between starters, in slot order: the host goes first.
    const int me = versus_.mySlot;
    startGo_ = 60 * 8 + me * 600;
    others_.clear();
    const int pals[3] = {PAL_RIVAL, PAL_RIVAL2, PAL_RIVAL3};
    int used = 0;
    for (int s = 0; s < rally::MAX_PLAYERS; s++) {
        if (s == me || !versus_.players[s].active) continue;
        Other o;
        o.slot = s;
        o.startAt = (s - me) * 10.0f;
        o.name = versus_.players[s].name;
        o.pal = pals[std::min(used++, 2)];
        setCarPalette(*vdp_, o.pal, versus_.players[s].car);
        o.s = (course_.startSeg + 0.5f) * SEG / U;
        others_.push_back(o);
    }
    for (bool& b : leftShown_) b = false;
    beginStart();
}

void RallyChamp::updateOnline() {
    if (versus_.phase == rally::Versus::Phase::Idle) return;
    versus_.tick();
    if (versus_.phase == rally::Versus::Phase::Ready) {
        rally::CarState me;
        me.dist = car_.s;
        me.x = car_.x;
        me.speed = car_.along();
        me.yaw = car_.psi;
        me.time = finished_ ? myTime_[stage_] : stageTime_ + car_.penalty;
        me.car = carId_;
        me.lap = mode_ == Mode::Stage ? 1 : 0;
        me.finished = finished_;
        versus_.sendState(me);
    }
    if (game_ != Game::Online) return;
    for (Other& o : others_) {
        if (o.slot < 0) continue;
        const rally::NetPlayer& p = versus_.players[o.slot];
        if (p.seen) {
            const int age = std::min(p.framesSince, 30);
            o.s = p.state.dist + p.state.speed * DT * age;
            o.x = p.state.x;
            o.psi = p.state.yaw;
            o.speed = p.state.speed;
            o.running = p.state.lap > 0 || p.state.finished;
            o.finished = p.state.finished;
            if (!o.running) o.s = (course_.startSeg - 30) * SEG / U;  // still queued for the start: out of our way
            o.y = car_.ground(course_, o.s);
        }
        if (!p.active && versus_.started && !leftShown_[o.slot] && mode_ == Mode::Stage) {
            leftShown_[o.slot] = true;
            say({p.name.empty() ? std::string("A PLAYER LEFT") : p.name + " LEFT"}, 150, PAL_RED);
        }
    }
}

void RallyChamp::drawLobby() {
    text("ONLINE", HALF, 12, 1.5f, PAL_YELLOW);
    auto roster = [&](int row) {
        for (int s = 0, line = 0; s < rally::MAX_PLAYERS; s++) {
            const rally::NetPlayer& p = versus_.players[s];
            if (!p.active) continue;
            std::string n = p.name.empty() ? "PLAYER" : p.name;
            n.resize(13, ' ');
            std::string extra = "STARTS +" + std::to_string(s * 10) + "S";
            if (versus_.isHost && s > 0 && p.pingMs >= 0) extra += " " + std::to_string(p.pingMs) + "MS";
            hud(4, row + line * 2, (s == versus_.mySlot ? "> " : "  ") + n + extra, s == versus_.mySlot ? PAL_YELLOW : PAL_HUD);
            line++;
        }
    };
    auto stageName = [&](int st) {
        const Venue& V = venue(std::clamp(st, 0, NUM_STAGES - 1) / 3);
        return std::string(V.name) + " - " + V.stages[std::clamp(st, 0, NUM_STAGES - 1) % 3];
    };
    switch (lobbyStep_) {
        case 0: {
            const char* opts[] = {"HOST A STAGE", "FIND GAMES ON LAN", "JOIN BY ADDRESS"};
            for (int i = 0; i < 3; i++) text(opts[i], HALF, 70 + i * 24.0f, 1.2f, lobbySel_ == i ? PAL_YELLOW : PAL_HUD);
            text(">", 30, 70 + lobbySel_ * 24.0f, 1.2f, PAL_YELLOW, -1);
            hud(3, 20, "UP TO 4 CREWS. STARTS 10S APART.", PAL_HUD);
            hud(3, 21, "LAN, OR THE INTERNET BY ADDRESS.", PAL_HUD);
            break;
        }
        case 1: {
            hud(14, 7, "CHOOSE STAGE", PAL_HUD);
            const std::string n = stageName(pickSel_);
            hud(20 - int(n.size()) / 2, 11, n, PAL_YELLOW);
            hud(15, 13, "< SS" + std::to_string(pickSel_ % 3 + 1) + " >", PAL_HUD);
            break;
        }
        case 2: {
            const std::string ip = gs::Link::localAddress();
            const std::string n = stageName(versus_.stage);
            hud(20 - int(n.size()) / 2, 5, n, PAL_YELLOW);
            if (!ip.empty()) {
                const std::string line = "ADDRESS " + ip + ":" + std::to_string(versus_.gamePort);
                hud(20 - int(line.size()) / 2, 7, line, PAL_HUD);
            }
            roster(9);
            const int count = versus_.playerCount();
            hud(count >= 2 ? 11 : 10, 18, count >= 2 ? "PRESS START TO GO" : "WAITING FOR PLAYERS", frameNo_ % 60 < 40 ? PAL_YELLOW : PAL_HUD);
            hud(1, 20, "INTERNET: FORWARD UDP PORT " + std::to_string(versus_.gamePort) + " ON YOUR", PAL_HUD);
            hud(1, 21, "ROUTER TO THIS MACHINE, AND SHARE YOUR", PAL_HUD);
            hud(1, 22, "PUBLIC IP ADDRESS WITH THE OTHERS.", PAL_HUD);
            break;
        }
        case 3: {
            hud(11, 6, "GAMES ON YOUR NETWORK", PAL_HUD);
            if (versus_.hosts.empty()) hud(12, 12, "SEARCHING...", frameNo_ % 60 < 40 ? PAL_YELLOW : PAL_HUD);
            for (size_t i = 0; i < versus_.hosts.size() && i < 7; i++) {
                const rally::HostInfo& h = versus_.hosts[i];
                std::string who = h.name.empty() ? h.addr.str() : h.name;
                who.resize(13, ' ');
                std::string line = who + venue(std::clamp(h.stage, 0, NUM_STAGES - 1) / 3).name + " " + std::to_string(h.players) + "/4";
                if (h.version != S3_VERSION) line += " V" + h.version;
                hud(2, 9 + int(i) * 2, (int(i) == lobbySel_ ? "> " : "  ") + line, int(i) == lobbySel_ ? PAL_YELLOW : PAL_HUD);
            }
            break;
        }
        case 4:
            hud(13, 12, "CONNECTING...", frameNo_ % 60 < 40 ? PAL_YELLOW : PAL_HUD);
            break;
        case 5: {
            const std::string n = stageName(versus_.stage);
            hud(20 - int(n.size()) / 2, 5, n, PAL_YELLOW);
            roster(8);
            hud(7, 18, "WAITING FOR THE HOST TO START", frameNo_ % 60 < 40 ? PAL_YELLOW : PAL_HUD);
            if (versus_.pingMs() >= 0) hud(15, 20, "PING " + std::to_string(versus_.pingMs()) + "MS", PAL_HUD);
            break;
        }
        case 6: {
            hud(12, 6, "JOIN BY ADDRESS", PAL_HUD);
            std::string shown = addrEdit_;
            if (frameNo_ % 40 < 26) shown += sys_->ctl.connected ? pendingChar_ : '_';
            text(shown.empty() ? " " : shown, HALF, 80, 1.6f, PAL_YELLOW);
            hud(5, 16, "THE HOST'S IP ADDRESS, OR IP:PORT", PAL_HUD);
            hud(7, 17, "(THE PORT IS 47017 IF LEFT OFF)", PAL_HUD);
            if (sys_->ctl.connected) hud(1, 20, "PAD: UP/DOWN DIGIT  RIGHT ADD  LEFT DEL", PAL_HUD);
            hud(7, 22, "TYPE IT, THEN PRESS ENTER", PAL_HUD);
            break;
        }
    }
    if (lobbyStep_ != 5 && lobbyStep_ != 2) hud(8, 26, "ENTER SELECT   ESC BACK", PAL_HUD);
}

bool RallyChamp::testHost(int stage, uint16_t port, int autoStart) {
    versus_.myName = profile_.name;
    versus_.myId = profile_.id;
    if (!versus_.host(stage, carId_, port)) return false;
    game_ = Game::Online;
    attract_ = false;
    autoStart_ = autoStart;
    mode_ = Mode::Lobby;
    lobbyStep_ = 2;
    t_ = 0;
    return true;
}

bool RallyChamp::testJoin(const std::string& ip, uint16_t port, int car) {
    versus_.myName = profile_.name;
    versus_.myId = profile_.id;
    carId_ = car;
    game_ = Game::Online;
    attract_ = false;
    mode_ = Mode::Lobby;
    t_ = 0;
    if (ip == "discover") {
        lobbyStep_ = 3;
        lobbySel_ = 0;
        return versus_.search();
    }
    gs::NetAddr a;
    if (!gs::NetAddr::parse(ip, port, &a)) return false;
    versus_.join(a, carId_);
    lobbyStep_ = 4;
    return versus_.phase == rally::Versus::Phase::Joining;
}

RallyChamp::VersusReport RallyChamp::versusReport() const {
    VersusReport r;
    r.finished = finished_ && game_ == Game::Online;
    r.time = finished_ ? myTime_[std::max(0, stage_)] : stageTime_;
    r.mySlot = versus_.mySlot;
    r.racing = mode_ == Mode::Start || mode_ == Mode::Stage || mode_ == Mode::Finish || mode_ == Mode::Result;
    for (int s = 0; s < rally::MAX_PLAYERS; s++) {
        const rally::NetPlayer& p = versus_.players[s];
        r.active[s] = p.active;
        r.seen[s] = p.seen;
        r.names[s] = p.name;
        r.ids[s] = p.id;
        r.finishedSlot[s] = p.state.finished;
        r.times[s] = p.state.time;
    }
    // Our place on the stage: fastest time wins (the start interval is already out of the clocks).
    r.rank = 1;
    for (int s = 0; s < rally::MAX_PLAYERS; s++)
        if (s != r.mySlot && r.active[s] && r.finishedSlot[s] && r.times[s] < r.time - 0.05f) r.rank++;
    return r;
}

std::string RallyChamp::debugLine() const {
    char buf[160];
    std::snprintf(buf, sizeof buf, "mode %d seg %d/%d x %.2f u %.1f state %d", int(mode_), car_.segIndex(course_), course_.finishSeg, car_.x, car_.u,
                  int(car_.state));
    return buf;
}

}  // namespace rc

namespace rc {

// Trailer hook: a stage of a single rally, already under way at `metresIn`.
void RallyChamp::demoStage(int stage, View v, float metresIn, int car) {
    game_ = Game::Rally;
    attract_ = false;
    carId_ = car;
    view_ = v;
    car_.damage = Damage{};
    startOrder_.clear();
    for (int i = 0; i < 15; i++) startOrder_.push_back(i);
    startOrder_.insert(startOrder_.begin() + 7, -1);
    for (Crew& c : crews_) c.out = false;
    loadStage(stage);
    mode_ = Mode::Stage;
    t_ = 0;
    started_ = true;
    car_.s += metresIn;
    car_.u = 22;
    camY_ = car_.y + 2;
    stageTime_ = metresIn / 22;
    // Skip the notes already behind us, and the splits.
    const int i = car_.segIndex(course_);
    while (nextNote_ < int(course_.notes.size()) && course_.notes[size_t(nextNote_)].seg < i) nextNote_++;
    while (splitsDone_ < int(course_.splits.size()) && course_.splits[size_t(splitsDone_)].seg < i) splitsDone_++;
}

}  // namespace rc

namespace rc {

void RallyChamp::demoStart(int stage, View v, int car) {
    demoStage(stage, v, 0, car);
    car_.reset(course_, course_.startSeg, carId_);
    stageTime_ = 0;
    started_ = false;
    nextNote_ = splitsDone_ = 0;
    camY_ = car_.y + 2;
    beginStart();
}

}  // namespace rc
