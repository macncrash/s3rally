// (3) RALLY - the stage: driving, timing, the co-driver,
// the other crews on the road, and the dust they leave hanging in the air.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "game.h"

namespace rc {

namespace {
constexpr float DT = 1.0f / 60.0f;
constexpr float SEG_M = SEG / U;
// Stage times of the AI crews relative to a perfect run, before pace and difficulty.
constexpr float REF = 1.12f;
const float DIFFICULTY[3] = {1.07f, 1.0f, 0.965f};
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

struct Rand {
    uint32_t a;
    float operator()() {
        a = a * 1664525u + 1013904223u;
        return float(a >> 8) / 16777216.0f;
    }
    float gauss() {  // roughly normal, mean 0, sd 1
        float s = 0;
        for (int i = 0; i < 6; i++) s += (*this)();
        return (s - 3) * 1.41f;
    }
};
}  // namespace

std::string fmtTime(float t) {
    if (t < 0) t = 0;
    const int m = int(t / 60), s = int(std::fmod(t, 60.0f)), c = int(std::fmod(t * 100, 100.0f));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d'%02d\"%02d", m, s, c);
    return buf;
}

// ================================================================ input

CarInput RallyChamp::readPad() {
    if (!sys_ || autopilot_ || (sys_->headless && !sys_->scripted)) return botInput();
    const gs::Pad& p = sys_->pad;
    CarInput in;
    in.steer = p.axisX;
    in.analog = p.axisX != 0;
    if (!in.analog) in.steer = (p.down(gs::BTN_RIGHT) ? 1.0f : 0.0f) - (p.down(gs::BTN_LEFT) ? 1.0f : 0.0f);
    in.throttle = std::max(p.down(gs::BTN_C) || p.down(gs::BTN_UP) ? 1.0f : 0.0f, p.accel);
    in.brake = std::max(p.down(gs::BTN_B) || p.down(gs::BTN_DOWN) ? 1.0f : 0.0f, p.brake);
    in.handbrake = p.down(gs::BTN_TURBO);
    in.shiftUp = p.pressed(gs::BTN_Y);
    in.shiftDown = p.pressed(gs::BTN_X);
    in.assist = assist_;
    return in;
}

// The autopilot: a driver who knows the road. Used on the title screen, in
// the tests and the daily report, for empty seats in split screen, and to
// bring the car to a stop after the finish.
CarInput RallyChamp::botInput() {
    CarInput in;
    in.analog = true;
    const Car& c = car_;
    const int i = c.segIndex(course_);
    const float u = std::max(std::fabs(c.u), 1.0f);

    // Pure pursuit: aim at a point ahead on a line that cuts a little to the
    // inside, measured from the direction the car is actually travelling.
    const float L = 8 + u * 0.6f;
    const int j = std::min(course_.N - 1, i + int(L / SEG_M));
    float bend = 0;  // how far the road curves away over L (lateral offset at the aim point)
    {
        float slope = 0;
        for (int k = i; k < j; k++) {
            slope += course_.segs[size_t(k)].kappa * SEG_M;
            bend += slope * SEG_M;
        }
    }
    const Segment& ahead = course_.segs[size_t(j)];
    const float line = clampf(ahead.kappa * 60, -0.35f, 0.35f) * ahead.hw;
    const float travel = c.psi + (std::fabs(c.u) > 2 ? std::atan2(c.v, std::fabs(c.u)) : 0.0f);  // direction of motion vs the road
    const float alpha = std::atan2(line + bend - c.x, L) - travel;
    const CarSpec& cs = carSpec(c.specId);
    const float wb = cs.a + cs.b;
    const float dr = c.drift();
    const float slide = dr > 0.12f ? dr - 0.12f : dr < -0.12f ? dr + 0.12f : 0.0f;  // beyond normal cornering slip
    // Pure pursuit asks for a turn rate; add lock when the car is not turning that fast (at the
    // limit the tyres need more angle than the geometry says).
    const float rWant = 2 * u * std::sin(alpha) / L;
    const float delta = std::atan(2 * wb * std::sin(alpha) / L) + 0.9f * slide + 0.18f * (rWant - c.r);
    const float lock = cs.steerMax / (1 + u / 32);
    in.steer = clampf(delta / lock, -1, 1);

    // Speed: the reference profile a little way ahead, with a margin.
    float vt = 1e9f;
    const int look = std::min(course_.N - 1, i + int((4 + u * 0.35f) / SEG_M));
    for (int k = i; k <= look; k++) vt = std::min(vt, botV_[size_t(k)]);
    vt = std::max(vt, 8.0f) * botSkill_ * (cs.frontDrive < 0.2f ? 0.94f : 1.0f);  // (the reference is zero on the start line)
    // Stop at the stop control after the finish.
    if (i >= course_.finishSeg) {
        const float left = std::max(0.0f, (course_.stopSeg - i) * SEG_M - 4);
        vt = std::min(vt, std::sqrt(2 * 4.0f * left));
    }
    if (c.damage.puncture) vt *= 0.8f;
    in.throttle = c.u < vt ? clampf((vt - c.u) * 0.5f + 0.3f, 0, 1) : 0;
    // Traction control of the right foot: ease off when the car is already sideways.
    in.throttle *= clampf(1 - (std::fabs(dr) - 0.1f) * (cs.frontDrive < 0.2f ? 5.0f : 2.5f), 0.15f, 1);
    in.brake = c.u > vt * 1.04f ? clampf((c.u - vt) * 0.25f, 0.2f, 1) : 0;
    if (i >= course_.stopSeg - 2 && c.u < 2) {
        in.throttle = 0;
        in.brake = 1;
    }
    // Pointing the wrong way: straighten up gently.
    if (std::fabs(c.psi) > 1.0f && std::fabs(c.u) < 6) {
        in.steer = c.psi > 0 ? -1.0f : 1.0f;
        in.throttle = 0.4f;
        in.brake = 0;
    }
    // Stuck (nose in a bank, wheels spinning): back out, then go again.
    if (c.state == CarState::Driving && std::fabs(c.u) < 1.0f && in.throttle > 0.3f) stuckT_ += 1;
    else if (std::fabs(c.u) > 3) stuckT_ = 0;
    if (stuckT_ > 90) {
        in.throttle = 0;
        in.brake = 1;  // held at a standstill this selects reverse
        in.steer = c.x * c.psi > 0 ? (c.psi > 0 ? 1.0f : -1.0f) : 0.0f;
        if (stuckT_ > 90 + 100) stuckT_ = 0;
    }
    return in;
}

// ================================================================ one frame of driving

void RallyChamp::drive(const CarInput& in, bool attract) {
    lastIn_ = in;
    car_.step(course_, in, DT, manual_ && !attract && mode_ == Mode::Stage);
    const CarEvents& ev = car_.ev;
    const bool live = mode_ == Mode::Stage;
    topSpeed_ = std::max(topSpeed_, car_.kmh());
    if (car_.airborne) airTotal_ += DT;
    if (ev.launched) {
        jumps_++;
        if (live) sys_->rumble(0.2f, 0.4f, 80);
    }
    if (ev.shifted && !attract) sfx_->shift();
    if (ev.landed > 0.5f) {
        const float k = clampf(ev.landed / 8, 0, 1);
        shake_ = std::max(shake_, 4 + k * 12);
        if (!attract) {
            sfx_->crash(ev.landed > 6);
            if (live) sys_->rumble(0.4f + k * 0.5f, 0.3f + k * 0.5f, 150 + int(k * 250));
        }
        if (ev.landed > 5) {
            hardLandings_++;
            if (live) say({"HARD LANDING"}, 90, PAL_RED);
        }
    }
    if (ev.hit > 0.5f) {
        shake_ = std::max(shake_, ev.hitSoft ? 4.0f : 14.0f);
        if (!attract) {
            sfx_->crash(!ev.hitSoft && ev.hit > 8);
            if (live) sys_->rumble(0.8f, 0.8f, ev.hitSoft ? 120 : 400);
        }
        for (int k = 0; k < 10; k++)
            parts_.push_back({160.0f + (std::rand() % 80 - 40), 190, (std::rand() % 100 - 50) / 25.0f, -(std::rand() % 100) / 40.0f, 10, 0.6f, 0, 30, 1});
    }
    if (ev.bank > 1) {
        shake_ = std::max(shake_, 3.0f);
        for (int k = 0; k < 8; k++)
            parts_.push_back({160.0f + (car_.x > 0 ? 50 : -50), 190, (car_.x > 0 ? 1.0f : -1.0f) * (std::rand() % 100) / 40.0f,
                              -(std::rand() % 100) / 35.0f, 14, 0.8f, 0, 34, 4});
    }
    if (ev.crashed) {
        crashes_++;
        shake_ = 18;
        if (!attract) {
            sfx_->crash(true);
            if (live) {
                sys_->rumble(1.0f, 1.0f, 700);
                say({car_.why.empty() ? std::string("CRASH!") : car_.why}, 150, PAL_RED);
            }
        }
    }
    if (ev.offRoad && live) say({car_.why.empty() ? std::string("OFF THE ROAD") : car_.why, "RECOVERY TIME"}, 180, PAL_RED);
    if (ev.puncture && live) {
        voice_->say(P_PUNCTURE, true);
        say({"PUNCTURE!"}, 150, PAL_RED);
    }
    if (ev.rockStrike && !attract) {
        sfx_->bump();
        if (live) sys_->rumble(0.6f, 0.9f, 160);
    }
    // Rough ground through the seat of your pants.
    if (live && !car_.airborne && frameNo_ % 6 == 0 && car_.speed() > 8 &&
        (SURF[car_.seg(course_).surf].rough || std::fabs(car_.x) > car_.seg(course_).hw + 0.5f || (car_.seg(course_).flags & F_BUMPS)))
        sys_->rumble(0.25f, 0.05f, 110);
    // Beached off the road and going nowhere: spectators push the car back out.
    if (car_.state == CarState::Driving && std::fabs(car_.x) > car_.seg(course_).hw + 0.5f && car_.s - progressS_ < 3) {
        if (++noProgress_ > 60 * 10) {
            car_.recover("PUSHED OUT", 5);
            if (live) say({"STUCK!", "PUSHED OUT +5S"}, 150, PAL_RED);
            noProgress_ = 0;
        }
    } else {
        noProgress_ = 0;
        progressS_ = car_.s;
    }
    if (shake_ > 0) shake_ *= 0.88f;
    if (shake_ < 0.3f) shake_ = 0;

    // Windscreen: mud, snow and spray build up; the wipers clear most of it.
    const Segment& g = car_.seg(course_);
    const float sp = car_.speed();
    const int dust = SURF[g.surf].dust;
    if (dust == 4 || dust == 2) dirt_ += DT * clampf(sp / 30, 0, 1) * (dust == 2 ? 0.6f : 0.25f);
    for (const Cloud& cl : clouds_)
        if (std::fabs(cl.s - car_.s) < 8 && std::fabs(cl.x - car_.x) < 4) dirt_ += DT * 0.02f;
    dirt_ = clampf(dirt_, 0, 1);
    if (dirt_ > 0.3f && wiperT_ == 0) wiperT_ = 40;
    if (wiperT_ > 0 && --wiperT_ == 20) dirt_ *= 0.35f;
    if (!attract || mode_ == Mode::Title) emitEffects();
}

void RallyChamp::emitEffects() {
    const Segment& g = car_.seg(course_);
    const float sp = car_.speed();
    const float p = clampf(sp / 45, 0, 1);
    if (car_.airborne || sp < 4 || view_ == View::Cockpit) return;
    const int dust = std::fabs(car_.x) > g.hw + 0.5f ? 1 : SURF[g.surf].dust;
    const bool heavy = car_.skid > 0.3f || car_.wheelspin;
    // Emission points: behind the rear wheels, where the car is on screen.
    const float cx = 160, cy = view_ == View::Far ? 196.0f : 206.0f, half = view_ == View::Far ? 26.0f : 38.0f;
    if (frameNo_ % (heavy ? 2 : 3)) return;
    for (int side : {-1, 1}) {
        if (!heavy && (std::rand() & 1)) continue;
        const float x = cx + side * half + (std::rand() % 7 - 3) - car_.steerIn * 6;
        const float vx = side * (0.3f + (std::rand() % 60) / 100.0f) - car_.v * 0.12f;
        switch (dust) {
            case 1:
                parts_.push_back({x, cy, vx, -0.4f - (std::rand() % 50) / 100.0f, 8 + p * 10, 0.7f + (heavy ? 0.4f : 0.0f), 0, heavy ? 30 : 20, 0});
                if (heavy) parts_.push_back({x, cy, vx * 2, -1.5f - (std::rand() % 100) / 60.0f, 8, 0.2f, 0, 18, 1});
                break;
            case 2:
                parts_.push_back({x, cy + 6, side * 1.4f, -0.8f, 24 + p * 30, 1.2f, 0, 18, 2});
                break;
            case 3:
                parts_.push_back({x, cy, vx, -0.6f - (std::rand() % 50) / 80.0f, 12 + p * 14, 1.1f, 0, 30, 4});
                break;
            case 4:
                parts_.push_back({x, cy, vx * 1.5f, -1.2f - (std::rand() % 100) / 70.0f, 10, 0.3f, 0, 24, 5});
                break;
            default:
                break;
        }
    }
}

void RallyChamp::updateParticles() {
    for (Particle& p : parts_) {
        p.x += p.vx;
        p.y += p.vy;
        p.size += p.grow;
        if (p.kind == 1 || p.kind == 5 || p.kind == 3) p.vy += 0.12f;
        else p.vy *= 0.97f;
        p.life++;
    }
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.life >= p.max || p.y > 240; }), parts_.end());
    if (parts_.size() > 70) parts_.erase(parts_.begin(), parts_.end() - 70);
    for (Cloud& c : clouds_) {
        c.life++;
        c.size += 0.02f;
        c.h += 0.004f;
    }
    clouds_.erase(std::remove_if(clouds_.begin(), clouds_.end(), [](const Cloud& c) { return c.life >= c.max; }), clouds_.end());
    if (clouds_.size() > 120) clouds_.erase(clouds_.begin(), clouds_.end() - 120);
    if (venue(venue_).weather == 1)
        for (Flake& f : flakes_) {
            const float sp = car_.speed() / 45;
            f.y += 0.6f + f.s * 0.8f + sp * 1.5f;
            f.x += (f.x - 160) / 160 * sp * 2.5f * f.s - car_.steerIn * sp * 1.5f;
            if (f.y > 224 || f.x < -4 || f.x > 324) {
                f.x = float(std::rand() % 320);
                f.y = -4 - float(std::rand() % 20);
            }
        }
}

// ================================================================ timing and the co-driver

float RallyChamp::splitOf(float time, int k) const {
    if (k < 0 || k >= int(course_.splits.size())) return 0;
    const float total = profileT_[size_t(course_.finishSeg)];
    return total > 0 ? time * profileT_[size_t(course_.splits[size_t(k)].seg)] / total : 0;
}

void RallyChamp::stageClock() {
    const int i = car_.segIndex(course_);
    while (splitsDone_ < int(course_.splits.size()) && i >= course_.splits[size_t(splitsDone_)].seg) {
        const int k = splitsDone_++;
        mySplit_[k] = stageTime_ + car_.penalty;
        // Against the quickest time so far at this split.
        float best = 1e9f;
        if (game_ == Game::TimeAttack) {
            if (best_[stage_] > 0) best = splitOf(best_[stage_], k);
        } else {
            for (const Crew& c : crews_)
                if (!c.out && c.split[stage_][k] > 0) best = std::min(best, c.split[stage_][k]);
        }
        sfx_->checkpoint();
        if (best < 1e8f) {
            lastSplitDelta_ = mySplit_[k] - best;
            splitShow_ = 240;
        }
    }
    if (i >= course_.finishSeg && !finished_) finishStage();
}

void RallyChamp::callNotes() {
    const int i = car_.segIndex(course_);
    // Read everything that's due; if several are due at once they join up.
    std::vector<int> words;
    while (nextNote_ < int(course_.notes.size()) && i >= course_.notes[size_t(nextNote_)].seg) {
        const Note& n = course_.notes[size_t(nextNote_++)];
        if (n.at < i - 5) continue;  // already past it: don't bother
        words.insert(words.end(), n.words.begin(), n.words.end());
    }
    if (!words.empty()) voice_->call(words);
}

void RallyChamp::recordGhost() {
    if (game_ != Game::TimeAttack || frameNo_ % 6) return;
    ghostRec_.push_back(car_.s);
    ghostRec_.push_back(car_.x);
    ghostRec_.push_back(car_.psi);
}

// ================================================================ the other crews

void RallyChamp::crewTimes(int st) {
    static uint32_t attempt = 0;
    Rand r{uint32_t(st * 7717 + 13) ^ (attempt++ * 2654435761u)};
    const int v = st / STAGES_PER_VENUE;
    const float total = profileT_.empty() ? course_.idealTime : profileT_[size_t(course_.finishSeg)];
    for (Crew& c : crews_) {
        if (c.out) {
            c.time[st] = 0;
            continue;
        }
        float t = course_.idealTime * REF * DIFFICULTY[difficulty_] * c.pace * c.home[v] * (1 + r.gauss() * 0.007f);
        c.split[st][0] = total > 0 ? t * profileT_[size_t(course_.splits[0].seg)] / total : 0;
        c.split[st][1] = total > 0 ? t * profileT_[size_t(course_.splits[1].seg)] / total : 0;
        float incident = 0;
        const float roll = r();
        if (roll < 0.01f) {
            c.out = true;  // retired
            c.time[st] = 0;
            continue;
        }
        if (roll < 0.07f) incident = 5 + r() * 35;  // a spin, a stall, a puncture
        c.time[st] = t + incident;
        // Remember the incident so the car on the road stops for it.
        if (incident > 0) c.split[st][1] += incident * 0.5f;
        crewIncident_[size_t(&c - crews_.data())] = incident;
    }
}

void RallyChamp::planStageField() {
    others_.clear();
    for (float& y : yieldLost_) y = 0;
    int me = 0;
    for (size_t k = 0; k < startOrder_.size(); k++)
        if (startOrder_[k] < 0) me = int(k);
    const int pals[3] = {PAL_RIVAL, PAL_RIVAL2, PAL_RIVAL3};
    int used = 0;
    for (int off : {-1, 1, -2}) {
        const int k = me + off;
        if (k < 0 || k >= int(startOrder_.size())) continue;
        const int ci = startOrder_[size_t(k)];
        if (ci < 0 || crews_[size_t(ci)].out || crews_[size_t(ci)].time[stage_] <= 0) continue;
        Other o;
        o.crew = ci;
        o.startAt = off * 10.0f;
        o.stageTime = crews_[size_t(ci)].time[stage_];
        o.name = crews_[size_t(ci)].name;
        o.pal = pals[used++];
        setLivery(*vdp_, o.pal, crews_[size_t(ci)].livery);
        const float inc = crewIncident_[size_t(ci)];
        if (inc > 0) {
            o.stopFor = inc;
            o.stopAt = float(course_.startSeg + int((course_.finishSeg - course_.startSeg) * (0.2f + 0.6f * float(std::rand() % 100) / 100)));
            o.stageTime -= inc;  // driving time; the stop is added on the road
        }
        o.s = (course_.startSeg + 0.5f) * SEG_M;
        others_.push_back(o);
    }
}

void RallyChamp::updateOthers() {
    const bool stage = mode_ == Mode::Start || mode_ == Mode::Stage || mode_ == Mode::Finish;
    if (!stage || game_ == Game::Online) return;
    // Everyone's clock: ours starts at GO; before that we are counting down on the line.
    const float now = mode_ == Mode::Start ? (t_ - startGo_) / 60.0f : stageTime_;
    const float total = profileT_[size_t(course_.finishSeg)];
    for (Other& o : others_) {
        if (o.crew < 0) continue;
        const float e = now - o.startAt;  // seconds since they started
        if (e < 0) {
            o.running = false;
            o.s = (course_.startSeg - 30) * SEG_M;  // queued behind the start, out of our way
            o.speed = 0;
            continue;
        }
        o.running = true;
        const float k = o.stageTime / std::max(total, 1.0f);  // their pace against the reference
        float ref = e / k;  // how far along the reference run they are
        if (o.stopAt >= 0) {
            const float tStop = profileT_[size_t(int(o.stopAt))];
            if (ref > tStop) ref = std::max(tStop, ref - o.stopFor / k);
        }
        ref -= yieldLost_[size_t(&o - others_.data())] / k;  // time lost pulling over for us
        // Find where that reference time puts them.
        const auto it = std::lower_bound(profileT_.begin() + course_.startSeg, profileT_.end(), ref);
        const int seg = std::min(course_.N - 2, int(it - profileT_.begin()));
        const float s = (seg + 0.5f) * SEG_M;
        o.speed = profile_v[size_t(seg)] / k;
        if (o.stopAt >= 0 && std::fabs(seg - o.stopAt) < 2 && ref <= profileT_[size_t(int(o.stopAt))] + 0.01f) o.speed = 0;
        o.s += clampf(s - o.s, -5, 60);
        const Segment& g = course_.segs[size_t(seg)];
        float wantX = clampf(g.kappa * 70, -0.4f, 0.4f) * g.hw;
        // A faster car close behind: pull over and let it by.
        const float gap = o.s - car_.s;
        o.yielding = o.startAt < 0 && gap > 0 && gap < 35 && car_.speed() > o.speed * 0.9f && mode_ == Mode::Stage;
        if (o.yielding) {
            wantX = g.hw * 0.75f;
            yieldLost_[size_t(&o - others_.data())] += DT * 0.55f;
        }
        // Coming up behind us: go round.
        if (o.startAt > 0 && -gap > 0 && -gap < 25) wantX = car_.x > 0 ? -g.hw * 0.6f : g.hw * 0.6f;
        o.x += (wantX - o.x) * std::min(1.0f, DT * 1.5f);
        o.psi = clampf((wantX - o.x) * 0.2f + g.kappa * 3, -0.4f, 0.4f);
        o.y = car_.ground(course_, o.s);
        o.finished = seg >= course_.finishSeg;
        // Dust hanging behind them.
        if (SURF[g.surf].dust == 1 && o.speed > 10 && frameNo_ % 4 == 0 && seg < course_.stopSeg && seg > course_.startSeg + 60)
            clouds_.push_back({o.s - 2, o.x, 0.6f, 2.2f, 0, 60 * (venue_ == 3 ? 9 : 5)});
        // Contact with us.
        if (mode_ == Mode::Stage && o.startAt < 0 && std::fabs(gap) < 4.2f && std::fabs(o.x - car_.x) < 1.8f && contactCool_ == 0) {
            car_.u *= gap > 0 ? 0.7f : 1.05f;
            car_.x += car_.x < o.x ? -0.3f : 0.3f;
            car_.damage.body = std::min(1.0f, car_.damage.body + 0.03f);
            sfx_->bump();
            sys_->rumble(0.5f, 0.5f, 150);
            contactCool_ = 30;
        }
        // The car behind is catching us.
        if (mode_ == Mode::Stage && o.startAt > 0 && -gap < 70 && -gap > 0 && carBehindWarned_ == 0) {
            carBehindWarned_ = 60 * 20;
            voice_->say(P_CAR_BEHIND, true);
            say({"CAR BEHIND!"}, 120, PAL_RED);
        }
    }
    if (contactCool_ > 0) contactCool_--;
    if (carBehindWarned_ > 0) carBehindWarned_--;
}

// ================================================================ sound

void RallyChamp::updateSound() {
    const bool on = mode_ == Mode::Start || mode_ == Mode::Stage || mode_ == Mode::Finish || mode_ == Mode::Title ||
                    mode_ == Mode::Menu || mode_ == Mode::Pick || mode_ == Mode::CarSelect;
    const bool quiet = mode_ != Mode::Start && mode_ != Mode::Stage && mode_ != Mode::Finish;
    if (!on || paused_) {
        sfx_->engine(0, 0, false);
        sfx_->rivalEngine(0, 0);
        sfx_->road(0, 0, 0);
        return;
    }
    const CarSpec& cs = carSpec(car_.specId);
    const float rpm = clampf(car_.rpm / cs.rpmMax, 0, 1.05f);
    sfx_->engine(quiet ? rpm * 0.6f : rpm, quiet ? 0 : car_.throttle, !quiet || mode_ == Mode::Title);
    const Segment& g = car_.seg(course_);
    const float p = clampf(car_.speed() / 45, 0, 1);
    const int dust = SURF[g.surf].dust;
    const float gravel = car_.airborne ? 0 : p * (dust == 0 ? 0.3f : dust == 3 ? 0.6f : 1.0f) * (std::fabs(car_.x) > g.hw ? 1.6f : 1.0f);
    sfx_->road(quiet ? gravel * 0.3f : gravel, car_.airborne ? 0 : car_.skid, g.surf == WATER ? p : 0);
    // The nearest other car.
    float best = 1e9f, freq = 0, vol = 0;
    for (const Other& o : others_) {
        const float d = std::fabs(o.s - car_.s);
        if (d < best && o.running && !o.finished) {
            best = d;
            freq = 40 + o.speed / 45 * 120;
            vol = 0.08f * std::max(0.0f, 1 - d / 80);
        }
    }
    sfx_->rivalEngine(quiet ? 0 : freq, quiet ? 0 : vol);
}

// ================================================================ headless

RallyChamp::SimReport RallyChamp::simulateStage(int st, int car, std::vector<std::string>* shots, const std::string& shotDir) {
    game_ = Game::Rally;
    attract_ = false;
    carId_ = car;
    manual_ = false;
    car_.damage = Damage{};
    startOrder_.clear();
    for (int i = 0; i < 15; i++) startOrder_.push_back(i);
    startOrder_.insert(startOrder_.begin() + 7, -1);
    for (Crew& c : crews_) c.out = false;
    loadStage(st);
    beginStart();
    SimReport rep{};
    rep.stage = st;
    rep.ideal = course_.idealTime;
    rep.notes = int(course_.notes.size());
    int frames = 0;
    const int shotAt[3] = {60 * 6, 60 * 30, 60 * 60};
    while ((mode_ == Mode::Start || mode_ == Mode::Stage) && frames < 60 * 600) {
        frame(*sys_);
        frames++;
        if (std::getenv("S3_RC_DEBUG") && frames % (std::getenv("S3_RC_FAST") ? 6 : 60) == 0) {  // telemetry for tuning
            const Segment& g = car_.seg(course_);
            std::printf("st %5.2f th %.2f br %.2f ", lastIn_.steer, lastIn_.throttle, lastIn_.brake);
            std::printf("t %5.1f mode %d seg %5d/%d x %5.2f psi %5.2f u %5.1f v %5.1f r %5.2f gear %d rpm %4.0f st %d air %d k %.4f hw %.1f surf %d dmg %.2f %s\n",
                        stageTime_, int(mode_), g.i, course_.finishSeg, car_.x, car_.psi, car_.u, car_.v, car_.r, car_.gear, car_.rpm,
                        int(car_.state), int(car_.airborne), g.kappa, g.hw, int(g.surf), car_.damage.total(), car_.why.c_str());
        }
        // S3_RC_MOMENTS=DIR: screenshots of the big moments (in the air, rolling, sideways).
        if (const char* md = std::getenv("S3_RC_MOMENTS")) {
            static int air = 0, roll = 0, slide = 0, cool = 0;
            if (frames == 1) air = roll = slide = cool = 0;
            if (cool > 0) cool--;
            const char* what = nullptr;
            air = car_.airborne ? air + 1 : 0;
            roll = car_.state == CarState::Rolling ? roll + 1 : 0;
            slide = std::fabs(car_.drift()) > 0.3f && car_.speed() > 12 ? slide + 1 : 0;
            if (air == 12) what = "air";
            else if (roll == 25) what = "roll";
            else if (slide == 15) what = "slide";
            if (what && cool == 0) {
                cool = 120;
                sys_->render();
                static int n = 0;
                sys_->saveScreenshot(std::string(md) + "/m" + std::to_string(n++) + "-s" + std::to_string(st) + "-" + what + ".png");
            }
        }
        if (shots)
            for (int k = 0; k < 3; k++)
                if (frames == shotAt[k] + 480) {
                    sys_->render();
                    const std::string p = shotDir + "/rc-stage" + std::to_string(st) + "-" + std::to_string(k) + ".png";
                    if (sys_->saveScreenshot(p)) shots->push_back(p);
                }
    }
    rep.finished = finished_ && car_.state != CarState::Out;
    rep.time = myTime_[st];
    rep.topKmh = topSpeed_;
    rep.airTime = airTotal_;
    rep.jumps = jumps_;
    rep.crashes = crashes_;
    rep.hardLandings = hardLandings_;
    rep.damage = car_.damage.total();
    int rank = 1;
    for (const Crew& c : crews_)
        if (!c.out && c.time[st] > 0 && c.time[st] < rep.time) rank++;
    rep.rank = rank;
    return rep;
}

}  // namespace rc
