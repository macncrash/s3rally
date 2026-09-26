#include "replay.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rc {

namespace {
constexpr float DT = 1.0f / 60;
constexpr char MAGIC[4] = {'S', '3', 'R', 'P'};
constexpr uint16_t VERSION = 1;

enum : uint8_t { F_HANDBRAKE = 1, F_UP = 2, F_DOWN = 4, F_ANALOG = 8, F_ASSIST = 16 };

// Little-endian writer and a bounds-checked reader: replays arrive from the internet.
struct Out {
    std::string b;
    void u8(uint32_t v) { b.push_back(char(v & 255)); }
    void u16(uint32_t v) { u8(v), u8(v >> 8); }
    void u32(uint32_t v) { u16(v & 0xffff), u16(v >> 16); }
    void f32(float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        u32(v);
    }
    void str(const std::string& s) {
        u8(uint32_t(s.size()));
        b += s;
    }
};
struct In {
    const std::string& b;
    size_t p = 0;
    bool ok = true;
    bool need(size_t n) {
        if (!ok || b.size() - p < n || p > b.size()) ok = false;
        return ok;
    }
    uint32_t u8() { return need(1) ? uint8_t(b[p++]) : 0; }
    uint32_t u16() {
        const uint32_t lo = u8();
        return lo | u8() << 8;
    }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | u16() << 16;
    }
    float f32() {
        const uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    std::string str(size_t max) {
        const size_t n = u8();
        if (n > max || !need(n)) {
            ok = false;
            return {};
        }
        std::string s = b.substr(p, n);
        p += n;
        return s;
    }
};

void putSnap(Out& o, const RunCheckpoint& c) {
    for (float f : c.car.f) o.f32(f);
    for (int32_t i : c.car.i) o.u32(uint32_t(i));
    o.u32(uint32_t(c.noProgress));
    o.f32(c.progressS);
}
RunCheckpoint getSnap(In& in) {
    RunCheckpoint c;
    for (float& f : c.car.f) f = in.f32();
    for (int32_t& i : c.car.i) i = int32_t(in.u32());
    c.noProgress = int32_t(in.u32());
    c.progressS = in.f32();
    return c;
}

// How far apart two snapshots are: the worst absolute float difference (revs
// counted in thousands) and whether any discrete state differs. Absolute, not
// relative: the distance along the road runs to thousands of metres, and a
// relative tolerance would let a car gain a little ground every second.
struct Gap {
    float worst = 0;
    bool discrete = false;
    float metres = 0;  // along-road and across-road distance between them
};
Gap gap(const RunCheckpoint& a, const RunCheckpoint& b) {
    Gap g;
    for (int k = 0; k < CarSnapshot::NF; k++)
        g.worst = std::max(g.worst, std::fabs(a.car.f[k] - b.car.f[k]) / (k == 15 ? 1000.0f : 1.0f));
    for (int k = 0; k < CarSnapshot::NI; k++) g.discrete |= a.car.i[k] != b.car.i[k];
    g.discrete |= a.noProgress != b.noProgress;
    g.worst = std::max(g.worst, std::fabs(a.progressS - b.progressS));
    g.metres = std::fabs(a.car.f[0] - b.car.f[0]) + std::fabs(a.car.f[1] - b.car.f[1]);
    return g;
}
}  // namespace

RunInput quantize(const CarInput& in) {
    RunInput q;
    q.steer = int16_t(std::lround(std::clamp(in.steer, -1.0f, 1.0f) * 32767));
    q.throttle = uint8_t(std::lround(std::clamp(in.throttle, 0.0f, 1.0f) * 255));
    q.brake = uint8_t(std::lround(std::clamp(in.brake, 0.0f, 1.0f) * 255));
    q.flags = uint8_t((in.handbrake ? F_HANDBRAKE : 0) | (in.shiftUp ? F_UP : 0) | (in.shiftDown ? F_DOWN : 0) | (in.analog ? F_ANALOG : 0) |
                      (in.assist ? F_ASSIST : 0));
    return q;
}

CarInput expand(const RunInput& q) {
    CarInput in;
    in.steer = q.steer / 32767.0f;
    in.throttle = q.throttle / 255.0f;
    in.brake = q.brake / 255.0f;
    in.handbrake = q.flags & F_HANDBRAKE;
    in.shiftUp = q.flags & F_UP;
    in.shiftDown = q.flags & F_DOWN;
    in.analog = q.flags & F_ANALOG;
    in.assist = q.flags & F_ASSIST;
    return in;
}

bool pushOutRule(Car& car, const Course& c, int& noProgress, float& progressS) {
    if (car.state == CarState::Driving && std::fabs(car.x) > car.seg(c).hw + 0.5f && car.s - progressS < 3) {
        if (++noProgress > 60 * 10) {
            car.recover("PUSHED OUT", 5);
            noProgress = 0;
            return true;
        }
    } else {
        noProgress = 0;
        progressS = car.s;
    }
    return false;
}

// ---------------------------------------------------------------- file format

std::string Replay::encode() const {
    Out o;
    o.b.append(MAGIC, 4);
    o.u16(VERSION);
    o.str(game);
    o.str(build);
    o.u8(uint32_t(stage));
    o.u8(uint32_t(spec));
    o.u8(manual);
    o.f32(damage.engine), o.f32(damage.suspension), o.f32(damage.tyres), o.f32(damage.body);
    o.u8(uint32_t(damage.puncture + 1));
    o.f32(claimed);
    o.u32(uint32_t(inputs.size()));
    o.u32(uint32_t(checkpoints.size()));
    for (const RunCheckpoint& c : checkpoints) putSnap(o, c);
    // Inputs, run-length coded: most frames repeat the one before.
    std::vector<std::pair<uint32_t, RunInput>> runs;
    for (const RunInput& q : inputs) {
        if (!runs.empty() && runs.back().second == q && runs.back().first < 65535) runs.back().first++;
        else runs.push_back({1, q});
    }
    o.u32(uint32_t(runs.size()));
    for (auto& [n, q] : runs) {
        o.u16(n);
        o.u16(uint16_t(q.steer));
        o.u8(q.throttle), o.u8(q.brake), o.u8(q.flags);
    }
    return o.b;
}

bool Replay::decode(const std::string& bytes, Replay& r, std::string& why) {
    In in{bytes};
    if (!in.need(4) || std::memcmp(bytes.data(), MAGIC, 4) != 0) return why = "not a replay", false;
    in.p = 4;
    if (in.u16() != VERSION) return why = "unknown replay version", false;
    r.game = in.str(16);
    r.build = in.str(40);
    r.stage = int(in.u8());
    r.spec = int(in.u8());
    const uint32_t man = in.u8();
    r.damage.engine = in.f32(), r.damage.suspension = in.f32(), r.damage.tyres = in.f32(), r.damage.body = in.f32();
    r.damage.puncture = int(in.u8()) - 1;
    r.claimed = in.f32();
    const uint32_t frames = in.u32(), cps = in.u32();
    if (!in.ok) return why = "truncated header", false;
    if (man > 1 || frames == 0 || frames > uint32_t(MAX_FRAMES) || cps != (frames - 1) / EVERY + 1) return why = "bad header", false;
    r.manual = man;
    // Every checkpoint is a fixed size: check the whole block is there before reading it.
    const size_t snapBytes = size_t(CarSnapshot::NF + CarSnapshot::NI + 2) * 4;
    if (!in.need(snapBytes * cps)) return why = "truncated checkpoints", false;
    r.checkpoints.clear();
    r.checkpoints.reserve(cps);
    for (uint32_t k = 0; k < cps; k++) r.checkpoints.push_back(getSnap(in));
    const uint32_t nruns = in.u32();
    if (!in.ok || nruns == 0 || nruns > frames || !in.need(size_t(nruns) * 7)) return why = "bad inputs", false;
    r.inputs.clear();
    r.inputs.reserve(frames);
    for (uint32_t k = 0; k < nruns; k++) {
        const uint32_t n = in.u16();
        RunInput q;
        q.steer = int16_t(in.u16());
        q.throttle = uint8_t(in.u8()), q.brake = uint8_t(in.u8()), q.flags = uint8_t(in.u8());
        if (n == 0 || r.inputs.size() + n > frames || q.flags > 31 || q.steer == -32768) return why = "bad inputs", false;
        r.inputs.insert(r.inputs.end(), n, q);
    }
    if (!in.ok || r.inputs.size() != frames) return why = "bad inputs", false;
    if (in.p != bytes.size()) return why = "trailing bytes", false;
    return true;
}

// ---------------------------------------------------------------- recording

void RunRecorder::begin(const std::string& game, const std::string& build, int stage, const Car& car, const Course& c, bool manual) {
    rep_ = Replay();
    rep_.game = game;
    rep_.build = build.substr(0, 40);
    rep_.stage = stage;
    rep_.spec = car.specId;
    rep_.manual = manual;
    rep_.damage = car.damage;
    active_ = true;
    done_ = false;
    (void)c;
}

void RunRecorder::frame(const RunInput& in, const Car& car, const Course& c, int noProgress, float progressS) {
    if (!active()) return;
    if (int(rep_.inputs.size()) >= Replay::MAX_FRAMES) {  // too long to verify: stop recording
        active_ = false;
        return;
    }
    if (rep_.inputs.size() % Replay::EVERY == 0) rep_.checkpoints.push_back({car.snapshot(c), noProgress, progressS});
    rep_.inputs.push_back(in);
}

// ---------------------------------------------------------------- verification

VerifyResult verifyReplay(const Replay& rep) {
    VerifyResult res;
    auto reject = [&](const std::string& why) {
        res.verdict = Verdict::Rejected;
        res.reason = why;
        return res;
    };
    if (rep.game != "rally") return reject("unknown game");
    if (rep.stage < 0 || rep.stage >= NUM_STAGES) return reject("unknown stage");
    if (rep.spec < 0 || rep.spec >= NUM_CARS) return reject("unknown car");
    const Damage& d = rep.damage;
    for (float v : {d.engine, d.suspension, d.tyres, d.body})
        if (!(v >= 0 && v <= 1)) return reject("bad damage");
    if (d.puncture < -1 || d.puncture > 1) return reject("bad damage");
    if (rep.inputs.empty() || rep.checkpoints.size() != (rep.inputs.size() - 1) / Replay::EVERY + 1) return reject("bad checkpoints");

    const Course course = buildCourse(rep.stage);
    // The start: a car just put on the line, in the stated condition. Only the
    // revs and the throttle can differ (the driver revs it through the countdown).
    Car fresh;
    fresh.reset(course, course.startSeg, rep.spec);
    fresh.damage = d;
    RunCheckpoint start{fresh.snapshot(course), 0, fresh.s};
    RunCheckpoint first = rep.checkpoints[0];
    const float rpm = first.car.f[15], thr = first.car.f[16];
    if (!(rpm >= 800 && rpm <= carSpec(rep.spec).rpmMax + 1) || !(thr >= 0 && thr <= 1)) return reject("bad start");
    first.car.f[15] = start.car.f[15], first.car.f[16] = start.car.f[16];
    if (gap(start, first).worst > 1e-5f || gap(start, first).discrete) return reject("not a standing start");

    // Each second from its own checkpoint: step it and compare with the next.
    Car car;
    float time = 0, worst = 0;
    bool discrete = false;
    const size_t frames = rep.inputs.size();
    for (size_t k = 0; k < rep.checkpoints.size(); k++) {
        const RunCheckpoint& cp = rep.checkpoints[k];
        if (!std::isfinite(cp.progressS) || cp.noProgress < 0 || cp.noProgress > 60 * 10) return reject("bad checkpoint " + std::to_string(k));
        if (!car.restore(course, cp.car)) return reject("bad checkpoint " + std::to_string(k));
        int noProgress = cp.noProgress;
        float progressS = cp.progressS;
        const size_t f0 = k * Replay::EVERY, f1 = std::min(frames, f0 + Replay::EVERY);
        for (size_t f = f0; f < f1; f++) {
            if (car.state == CarState::Out) return reject("retired");
            car.step(course, expand(rep.inputs[f]), DT, rep.manual);
            pushOutRule(car, course, noProgress, progressS);
            const bool over = car.segIndex(course) >= course.finishSeg;
            if (over && f + 1 != frames) return reject("inputs go on past the finish");
            if (!over && f + 1 == frames) return reject("did not reach the finish");
        }
        if (k + 1 < rep.checkpoints.size()) {
            const Gap g = gap({car.snapshot(course), noProgress, progressS}, rep.checkpoints[k + 1]);
            if (g.metres > 2.0f) return reject("the car jumps between checkpoints at " + std::to_string(k + 1) + " s");
            worst = std::max(worst, g.worst);
            discrete |= g.discrete;
        } else {
            // The clock: exactly as the game counts it, a frame at a time.
            for (size_t f = 0; f < frames; f++) time += DT;
            res.time = time + car.penalty;
        }
    }
    if (std::fabs(res.time - rep.claimed) > 0.001f) return reject("claimed time does not match the drive");
    // Same binary, same machine: every checkpoint matches exactly. A different
    // platform's maths can differ in the last bits; a person should look at
    // anything more than that before it goes on the board.
    if (worst > 1e-3f || discrete) {
        res.verdict = Verdict::Review;
        res.reason = "replay drifts from its checkpoints";
        return res;
    }
    res.verdict = Verdict::Accepted;
    res.reason = worst > 0 ? "matches within platform rounding" : "matches exactly";
    return res;
}

// ---------------------------------------------------------------- base64

std::string base64Encode(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8 | uint8_t(in[i + 2]);
        out += T[v >> 18], out += T[(v >> 12) & 63], out += T[(v >> 6) & 63], out += T[v & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t v = uint8_t(in[i]) << 16;
        out += T[v >> 18], out += T[(v >> 12) & 63], out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8;
        out += T[v >> 18], out += T[(v >> 12) & 63], out += T[(v >> 6) & 63], out += '=';
    }
    return out;
}

bool base64Decode(const std::string& in, std::string& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    if (in.size() % 4) return false;
    out.clear();
    out.reserve(in.size() / 4 * 3);
    for (size_t i = 0; i < in.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; k++) {
            if (in[i + k] == '=' && i + 4 == in.size() && k >= 2) {
                v[k] = 0;
                pad++;
            } else if (pad || (v[k] = val(in[i + k])) < 0) {
                return false;
            }
        }
        const uint32_t n = uint32_t(v[0]) << 18 | uint32_t(v[1]) << 12 | uint32_t(v[2]) << 6 | uint32_t(v[3]);
        out += char(n >> 16);
        if (pad < 2) out += char((n >> 8) & 255);
        if (pad < 1) out += char(n & 255);
    }
    return true;
}

}  // namespace rc
