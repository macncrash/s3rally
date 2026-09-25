#include "rally32.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "console/gfx.h"
#include "rc/carmodel.h"
#include "version.h"

namespace rc32 {

using g32::V3;
using g32::WVtx;

namespace {
constexpr float DT = 1.0f / 60.0f;
constexpr float SEG_M = rc::SEG / rc::U;
constexpr float PI = 3.14159265f;
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

uint16_t to15(uint16_t rgb4) { return g32::texelFrom12(rgb4) & 0x7fff; }
// A 15-bit colour as a vertex shade (128 = full).
void shadeOf(uint16_t c15, uint8_t& r, uint8_t& g, uint8_t& b, float k = 1) {
    r = uint8_t(clampf(((c15 >> 10) & 31) * 128.0f / 31 * k, 0, 255));
    g = uint8_t(clampf(((c15 >> 5) & 31) * 128.0f / 31 * k, 0, 255));
    b = uint8_t(clampf((c15 & 31) * 128.0f / 31 * k, 0, 255));
}
uint32_t hash2(int x, int y) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}
std::string fmtTime(float t) {
    char b[32];
    std::snprintf(b, sizeof b, "%d'%02d\"%02d", int(t / 60), int(std::fmod(t, 60.0f)), int(std::fmod(t * 100, 100.0f)));
    return b;
}
}  // namespace

// ================================================================ setup

void Rally32::init(gs::System& sys) {
    sys_ = &sys;
    // The 16-bit art is generated into the board's sprite ROM once, then read back as textures.
    rc::buildArt(sys.vdp, art_);
    radio_ = std::make_unique<rally::Radio>(sys.apu);
    sfx_ = std::make_unique<rally::Sfx>(sys.apu);
    // Font: the system font, 16 x 6 characters of 8 x 8, white with a dark shadow.
    {
        std::vector<uint16_t> px(128 * 48, 0);
        for (int c = 32; c < 128; c++) {
            const uint8_t* g = gs::glyph(char(c));
            const int ox = ((c - 32) % 16) * 8, oy = ((c - 32) / 16) * 8;
            for (int y = 0; y < 7; y++)
                for (int x = 0; x < 5; x++)
                    if (g[y * 5 + x]) {
                        px[size_t(oy + y + 1) * 128 + ox + x + 2] = g32::texel(0, 0, 2);
                        px[size_t(oy + y) * 128 + ox + x + 1] = g32::texel(31, 31, 31);
                    }
        }
        font_ = gpu_.texture(128, 48, px.data());
    }
    // Title logo.
    {
        const gs::Image& im = art_.logo.lv[0];
        std::vector<uint16_t> px(size_t(im.w) * im.h, 0);
        const uint8_t* rom = sys.vdp.rom() + im.off;
        for (size_t i = 0; i < px.size(); i++)
            if (rom[i]) px[i] = g32::texelFrom12(sys.vdp.color(rc::PAL_LOGO * 16 + rom[i]));
        logo_ = gpu_.texture(im.w, im.h, px.data());
    }
    firstStageTex_ = -1;
    stage_ = -1;
    loadStage(0);
    mode_ = Mode::Title;
    attract_ = true;
    car_.s += 150;
    car_.u = 20;
}

void Rally32::loadStage(int st) {
    stage_ = st;
    course_ = rc::buildCourse(st);
    const int v = course_.venue;
    rc::loadVenue(sys_->vdp, art_, v, 0);  // palettes for this venue
    if (firstStageTex_ >= 0) gpu_.release(firstStageTex_);
    const rc::Venue& V = rc::venue(v);

    // Road surfaces: 64 across the road, 64 along it (4 metres).
    auto road = [&](rc::Surf s) {
        const uint16_t* pal = V.road[rc::surfPalette(v, s)];
        std::vector<uint16_t> px(64 * 64);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                const float au = std::fabs(x - 31.5f) / 32;
                const uint32_t h = hash2(x, y + 7 * s);
                int idx = (y / 16) % 2 ? 6 : 7;
                const bool track = au > 0.24f && au < 0.52f;
                switch (s) {
                    case rc::TARMAC:
                        idx = h % 13 == 0 ? 15 : 6;
                        if ((au < 0.03f && (y / 16) % 2) || (au > 0.9f && au < 0.95f)) idx = 14;
                        break;
                    case rc::WATER:
                        idx = h % 7 == 0 ? 13 : (y / 4) % 2 ? 11 : 12;
                        break;
                    case rc::SNOW:
                        idx = track ? (h % 5 == 0 ? 8 : 9) : au > 0.86f ? 15 : h % 17 == 0 ? 14 : 6;
                        break;
                    case rc::ICE:
                        idx = track ? 9 : (hash2(x / 3, y / 20) % 4 == 0 ? 14 : 6);
                        break;
                    case rc::MUD:
                        idx = track ? 9 : h % 13 == 0 ? 8 : 6;
                        if (hash2(x / 12, y / 24) % 5 == 0) idx = h % 9 == 0 ? 13 : 11;
                        break;
                    case rc::ROCKY:
                        idx = track ? 9 : 6;
                        if (h % 5 == 0) idx = (h >> 8) % 3 == 0 ? 15 : 8;
                        break;
                    default:  // gravel: swept wheel tracks, loose stones in the middle and at the edges
                        idx = track ? 9 : au < 0.12f ? 10 : 7;
                        if (h % (track ? 23 : au > 0.8f ? 3 : 7) == 0) idx = (h >> 8) & 1 ? 15 : 8;
                        break;
                }
                px[size_t(y) * 64 + x] = g32::texelFrom12(pal[idx]);
            }
        return gpu_.texture(64, 64, px.data());
    };
    for (int s = 0; s < rc::SURF_COUNT; s++) {
        const int id = road(rc::Surf(s));
        if (s == 0) firstStageTex_ = id;
        roadTex_[s] = id;
    }
    auto plain = [&](std::initializer_list<uint16_t> cols, int every) {
        std::vector<uint16_t> px(64 * 64);
        const std::vector<uint16_t> c(cols);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                const uint32_t h = hash2(x * 3, y * 5);
                px[size_t(y) * 64 + x] = g32::texelFrom12(c[h % every == 0 ? 2 : ((x / 8 + y / 8) & 1)]);
            }
        return gpu_.texture(64, 64, px.data());
    };
    const uint16_t* g = V.road[0];
    groundTex_ = plain({g[1], g[2], g[3]}, 5);
    vergeTex_ = plain({g[4], g[5], g[8]}, 6);
    waterTex_ = plain({g[11], g[12], g[13]}, 9);
    snowTex_ = plain({V.road[0][14], V.road[0][15], V.road[0][1]}, 7);
    dropTex_ = plain({V.scene[8], V.scene[9], V.scene[10]}, 4);

    // Scenery: the 16-bit sprites at half size, in this venue's colours.
    for (int o = 0; o < rc::O_COUNT; o++) {
        const gs::Image& im = art_.obj[o].lv[1].w ? art_.obj[o].lv[1] : art_.obj[o].lv[0];
        const int pal = rc::OBJ[o].scene ? rc::PAL_SCENE : rc::PAL_COMMON;
        std::vector<uint16_t> px(size_t(im.w) * im.h, 0);
        const uint8_t* rom = sys_->vdp.rom() + im.off;
        for (size_t i = 0; i < px.size(); i++)
            if (rom[i]) px[i] = g32::texelFrom12(sys_->vdp.color(pal * 16 + rom[i]));
        objTex_[o] = gpu_.texture(im.w, im.h, px.data());
    }
    std::copy(rc::carSpec(carId_).livery, rc::carSpec(carId_).livery + 16, livery_);

    // The centre line in world space, from the course's own plan.
    pos_.assign(size_t(course_.N) + 1, V3{});
    float x = 0, z = 0;
    for (int i = 0; i <= course_.N; i++) {
        const int k = std::min(i, course_.N - 1);
        pos_[size_t(i)] = {x, course_.segs[size_t(k)].y1 / rc::U, z};
        const float h = course_.heading(k);
        x += std::sin(h) * SEG_M;
        z += std::cos(h) * SEG_M;
    }
    botV_ = rc::speedProfile(course_, 0.66f, 6.0f, 5.5f, 50.0f);
    car_.reset(course_, course_.startSeg, carId_);
    time_ = 0;
    camYaw_ = headingAt(car_.s);
    camY_ = car_.y + 2;
}

// ================================================================ driving

float Rally32::headingAt(float s) const {
    const float f = s / SEG_M;
    const int i = std::clamp(int(f), 0, course_.N - 2);
    const float t = f - i;
    return course_.heading(i) * (1 - t) + course_.heading(i + 1) * t;
}

V3 Rally32::roadPoint(float s, float x) const {
    const float f = s / SEG_M;
    const int i = std::clamp(int(f), 0, course_.N - 1);
    const float t = clampf(f - i, 0, 1);
    const V3 p = pos_[size_t(i)] + (pos_[size_t(i + 1)] - pos_[size_t(i)]) * t;
    const float h = headingAt(s);
    return p + V3{std::cos(h), 0, -std::sin(h)} * x;
}

rc::CarInput Rally32::readPad() {
    const gs::Pad& p = sys_->pad;
    rc::CarInput in;
    in.steer = p.axisX;
    in.analog = p.axisX != 0;
    if (!in.analog) in.steer = (p.down(gs::BTN_RIGHT) ? 1.0f : 0.0f) - (p.down(gs::BTN_LEFT) ? 1.0f : 0.0f);
    in.throttle = std::max(p.down(gs::BTN_C) || p.down(gs::BTN_UP) ? 1.0f : 0.0f, p.accel);
    in.brake = std::max(p.down(gs::BTN_B) || p.down(gs::BTN_DOWN) ? 1.0f : 0.0f, p.brake);
    in.handbrake = p.down(gs::BTN_TURBO);
    return in;
}

// The autopilot (as in (3) RALLY): pure pursuit on a line that cuts a little inside.
rc::CarInput Rally32::autopilot() {
    rc::CarInput in;
    in.analog = true;
    const rc::Car& c = car_;
    const int i = c.segIndex(course_);
    const float u = std::max(std::fabs(c.u), 1.0f);
    const float L = 8 + u * 0.6f;
    const int j = std::min(course_.N - 1, i + int(L / SEG_M));
    float bend = 0, slope = 0;
    for (int k = i; k < j; k++) {
        slope += course_.segs[size_t(k)].kappa * SEG_M;
        bend += slope * SEG_M;
    }
    const rc::Segment& ahead = course_.segs[size_t(j)];
    const float line = clampf(ahead.kappa * 60, -0.35f, 0.35f) * ahead.hw;
    const float travel = c.psi + (std::fabs(c.u) > 2 ? std::atan2(c.v, std::fabs(c.u)) : 0.0f);
    const float alpha = std::atan2(line + bend - c.x, L) - travel;
    const rc::CarSpec& cs = rc::carSpec(c.specId);
    const float wb = cs.a + cs.b, dr = c.drift();
    const float slide = dr > 0.12f ? dr - 0.12f : dr < -0.12f ? dr + 0.12f : 0.0f;
    const float rWant = 2 * u * std::sin(alpha) / L;
    const float delta = std::atan(2 * wb * std::sin(alpha) / L) + 0.9f * slide + 0.18f * (rWant - c.r);
    in.steer = clampf(delta / (cs.steerMax / (1 + u / 32)), -1, 1);
    float vt = 1e9f;
    for (int k = i; k <= std::min(course_.N - 1, i + int((4 + u * 0.35f) / SEG_M)); k++) vt = std::min(vt, botV_[size_t(k)]);
    vt = std::max(vt, 8.0f);
    if (i >= course_.finishSeg) vt = std::min(vt, std::sqrt(8 * std::max(0.0f, (course_.stopSeg - i) * SEG_M - 4)));
    in.throttle = c.u < vt ? clampf((vt - c.u) * 0.5f + 0.3f, 0, 1) : 0;
    in.brake = c.u > vt * 1.04f ? clampf((c.u - vt) * 0.25f, 0.2f, 1) : 0;
    return in;
}

// ================================================================ the frame

void Rally32::frame(gs::System& sys) {
    t_++;
    const gs::Pad& pad = sys.pad;
    const bool confirm = pad.pressed(gs::BTN_START) || pad.pressed(gs::BTN_C);
    if (pad.pressed(gs::BTN_Z)) radio_->next();
    switch (mode_) {
        case Mode::Title:
            car_.step(course_, autopilot(), DT, false);
            if (car_.segIndex(course_) > course_.finishSeg) {
                loadStage((stage_ + 4) % rc::NUM_STAGES);
                car_.s += 150;
                car_.u = 18;
            }
            if (t_ > 30 && pad.pressed(gs::BTN_MODE) && sys.hasHome()) sys.eject();
            else if (t_ > 30 && confirm) {
                mode_ = Mode::Pick;
                pick_ = stage_;
                t_ = 0;
            }
            break;
        case Mode::Pick:
            car_.step(course_, autopilot(), DT, false);
            if (pad.pressed(gs::BTN_LEFT) || pad.pressed(gs::BTN_RIGHT)) {
                pick_ = (pick_ + (pad.pressed(gs::BTN_LEFT) ? rc::NUM_STAGES - 1 : 1)) % rc::NUM_STAGES;
                loadStage(pick_);
                car_.s += 150;
                car_.u = 18;
            }
            if (pad.pressed(gs::BTN_UP) || pad.pressed(gs::BTN_DOWN)) {
                carId_ = (carId_ + 1) % rc::NUM_CARS;
                std::copy(rc::carSpec(carId_).livery, rc::carSpec(carId_).livery + 16, livery_);
            }
            if (pad.pressed(gs::BTN_MODE)) { mode_ = Mode::Title; t_ = 0; }
            else if (confirm && t_ > 6) {
                loadStage(pick_);
                attract_ = false;
                mode_ = Mode::Drive;
                t_ = 0;
            }
            break;
        case Mode::Drive: {
            if (pad.pressed(gs::BTN_MODE)) { mode_ = Mode::Pick; attract_ = true; t_ = 0; break; }
            const rc::CarInput in = (sys.headless && !sys.scripted) ? autopilot() : readPad();
            car_.step(course_, in, DT, false);
            time_ += DT;
            if (car_.segIndex(course_) >= course_.finishSeg) {
                time_ += car_.penalty;
                if (best_ == 0 || time_ < best_) best_ = time_;
                mode_ = Mode::Done;
                t_ = 0;
                sfx_->fanfare();
            }
            break;
        }
        case Mode::Done:
            car_.step(course_, autopilot(), DT, false);
            if (t_ > 60 && confirm) { mode_ = Mode::Pick; attract_ = true; t_ = 0; }
            break;
    }
    const bool driving = mode_ == Mode::Drive || mode_ == Mode::Done;
    const rc::CarSpec& cs = rc::carSpec(car_.specId);
    sfx_->engine(clampf(car_.rpm / cs.rpmMax, 0, 1) * (driving ? 1.0f : 0.6f), driving ? car_.throttle : 0, true);
    const rc::Segment& g = car_.seg(course_);
    sfx_->road(car_.airborne ? 0 : clampf(car_.speed() / 45, 0, 1) * (rc::SURF[g.surf].dust ? 1.0f : 0.3f), car_.airborne ? 0 : car_.skid,
               g.surf == rc::WATER ? 0.5f : 0);
    radio_->duck(false);
    radio_->tick();
    sfx_->tick();
    follow();
}

// The chase camera follows the car every frame (drawing only looks through it).
void Rally32::follow() {
    const float carH = headingAt(car_.s) + car_.psi;
    float travel = carH;
    if (car_.speed() > 3)
        travel = headingAt(car_.s) + std::atan2(car_.u * std::sin(car_.psi) + car_.v * std::cos(car_.psi), std::max(0.5f, car_.along()));
    float d = travel - camYaw_;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    camYaw_ += d * 0.1f;
    const float groundBehind = roadPoint(car_.s - 6.4f, car_.x).y;
    camY_ += (std::max(groundBehind + 2.5f, car_.y + 2.0f) - camY_) * 0.12f;
}

bool Rally32::video(const uint32_t*& px, int& w, int& h) {
    scene();
    hud();
    px = gpu_.draw();
    w = g32::W;
    h = g32::H;
    return true;
}

// ================================================================ 3D

void Rally32::scene() {
    const rc::Venue& V = rc::venue(course_.venue);
    const float carH = headingAt(car_.s) + car_.psi;
    V3 carPos = roadPoint(car_.s, car_.x);
    carPos.y = car_.y;
    const V3 back{std::sin(camYaw_), 0, std::cos(camYaw_)};
    cam_.pos = carPos - back * 6.4f;
    cam_.pos.y = camY_;
    cam_.yaw = camYaw_;
    cam_.pitch = -0.15f;
    cam_.focal = 250;
    cam_.fogNear = V.fogNear * 0.6f;
    cam_.fogFar = std::min(V.fogFar, 320.0f);
    cam_.update();
    const uint16_t fog = to15(V.fog);
    gpu_.setFog((fog >> 10) & 31, (fog >> 5) & 31, fog & 31);

    // Sky: a gradient down to the haze at the horizon.
    const uint16_t top = to15(V.skyTop), hor = to15(V.skyHorizon);
    const float horizon = cam_.cy + cam_.focal * std::tan(-cam_.pitch);
    for (int y = 0; y < g32::H; y++) {
        const float t = clampf(y / std::max(1.0f, horizon), 0, 1);
        auto mix = [&](int sh) { return int(((top >> sh) & 31) + (((hor >> sh) & 31) - ((top >> sh) & 31)) * t); };
        gpu_.lineColor[y] = y < horizon ? uint16_t(mix(10) << 10 | mix(5) << 5 | mix(0)) : fog;
    }

    // Distant mountains: a ring of peaks far beyond the fog.
    {
        g32::Camera far = cam_;
        far.fogNear = 600;
        far.fogFar = 3000;
        const uint16_t m1 = to15(V.far[3]), m2 = to15(V.far[4]);
        const int n = 48;
        for (int k = 0; k < n; k++) {
            const float a0 = k * 2 * PI / n, a1 = (k + 1) * 2 * PI / n;
            const float h0 = 60 + 90 * (0.5f + 0.5f * std::sin(k * 1.7f + course_.venue)), h1 = 60 + 90 * (0.5f + 0.5f * std::sin((k + 1) * 1.7f + course_.venue));
            const float R = 1200;
            const V3 c = {cam_.pos.x, carPos.y - 30, cam_.pos.z};
            WVtx q[4];
            q[0].p = c + V3{std::sin(a0) * R, 0, std::cos(a0) * R};
            q[1].p = c + V3{std::sin(a1) * R, 0, std::cos(a1) * R};
            q[2].p = c + V3{std::sin(a1) * R, h1, std::cos(a1) * R};
            q[3].p = c + V3{std::sin(a0) * R, h0, std::cos(a0) * R};
            for (int i = 0; i < 4; i++) shadeOf(k % 2 ? m1 : m2, q[i].r, q[i].g, q[i].b);
            g32::polygon(gpu_, far, q, 4, -1, g32::OPAQUE, 1e5f);
        }
    }

    // The road and the land beside it, from just behind the camera out into the fog.
    const int first = std::max(0, car_.segIndex(course_) - 10);
    const int last = std::min(course_.N - 2, first + 480);
    auto vtx = [&](V3 p, float u, float v, uint8_t s = 128) {
        WVtx w;
        w.p = p;
        w.u = u;
        w.v = v;
        w.r = w.g = w.b = s;
        return w;
    };
    for (int i = first; i < last;) {
        const int step = i - first < 150 ? 1 : i - first < 300 ? 2 : 4;
        const int j = std::min(last, i + step);
        const rc::Segment& a = course_.segs[size_t(i)];
        const float s0 = i * SEG_M, s1 = j * SEG_M;
        const float hw0 = a.hw, hw1 = course_.segs[size_t(j)].hw;
        const float v0 = s0 * 16, v1 = s1 * 16;
        // Road.
        WVtx q[4] = {vtx(roadPoint(s0, -hw0), 0, v0), vtx(roadPoint(s0, hw0), 64, v0), vtx(roadPoint(s1, hw1), 64, v1),
                     vtx(roadPoint(s1, -hw1), 0, v1)};
        g32::polygon(gpu_, cam_, q, 4, roadTex_[a.surf]);
        // Each side: verge, then the ground (or water, a drop, a snow wall).
        for (int sd : {-1, 1}) {
            const uint8_t side = sd < 0 ? a.left : a.right;
            const float e0 = sd * hw0, e1 = sd * hw1;
            WVtx vq[4] = {vtx(roadPoint(s0, e0), 0, v0), vtx(roadPoint(s0, e0 + sd * 1.5f), 24, v0), vtx(roadPoint(s1, e1 + sd * 1.5f), 24, v1),
                          vtx(roadPoint(s1, e1), 0, v1)};
            if (side == gs::GROUND_SNOWWALL) {
                // A ploughed bank: a wall of snow just off the edge.
                WVtx w[4] = {vtx(roadPoint(s0, e0 + sd * 0.6f), 0, v0, 118), vtx(roadPoint(s1, e1 + sd * 0.6f), 0, v1, 118),
                             vtx(roadPoint(s1, e1 + sd * 0.6f) + V3{0, 1.4f, 0}, 32, v1), vtx(roadPoint(s0, e0 + sd * 0.6f) + V3{0, 1.4f, 0}, 32, v0)};
                g32::polygon(gpu_, cam_, w, 4, snowTex_, g32::OPAQUE, 0.2f);
                WVtx top[4] = {vtx(roadPoint(s0, e0 + sd * 0.6f) + V3{0, 1.4f, 0}, 0, v0), vtx(roadPoint(s0, e0 + sd * 40) + V3{0, 1.4f, 0}, 64, v0),
                               vtx(roadPoint(s1, e1 + sd * 40) + V3{0, 1.4f, 0}, 64, v1), vtx(roadPoint(s1, e1 + sd * 0.6f) + V3{0, 1.4f, 0}, 0, v1)};
                g32::polygon(gpu_, cam_, top, 4, snowTex_, g32::OPAQUE, 0.6f);
                g32::polygon(gpu_, cam_, vq, 4, snowTex_, g32::OPAQUE, 0.3f);
                continue;
            }
            g32::polygon(gpu_, cam_, vq, 4, vergeTex_, g32::OPAQUE, 0.3f);
            const V3 drop = side == gs::GROUND_DROP ? V3{0, -35, 0} : side == gs::GROUND_WATER ? V3{0, -0.4f, 0} : V3{};
            if (side == gs::GROUND_DROP) {  // the hillside falling away to the valley
                WVtx f[4] = {vtx(roadPoint(s0, e0 + sd * 1.5f), 0, v0), vtx(roadPoint(s0, e0 + sd * 14) + V3{0, -30, 0}, 64, v0),
                             vtx(roadPoint(s1, e1 + sd * 14) + V3{0, -30, 0}, 64, v1), vtx(roadPoint(s1, e1 + sd * 1.5f), 0, v1)};
                g32::polygon(gpu_, cam_, f, 4, dropTex_, g32::OPAQUE, 0.5f);
            }
            const float inner = side == gs::GROUND_DROP ? 14 : 1.5f;
            WVtx gq[4] = {vtx(roadPoint(s0, e0 + sd * inner) + drop, 0, v0 / 2), vtx(roadPoint(s0, e0 + sd * 60) + drop, 128, v0 / 2),
                          vtx(roadPoint(s1, e1 + sd * 60) + drop, 128, v1 / 2), vtx(roadPoint(s1, e1 + sd * inner) + drop, 0, v1 / 2)};
            g32::polygon(gpu_, cam_, gq, 4, side == gs::GROUND_WATER ? waterTex_ : groundTex_, g32::OPAQUE, 0.8f);
        }
        i = j;
    }

    // Scenery: the 16-bit sprites standing up as billboards.
    const int objLast = std::min(course_.N - 1, car_.segIndex(course_) + 280);
    for (int i = std::max(0, car_.segIndex(course_) - 8); i < objLast; i++)
        for (const rc::Placed& o : course_.segs[size_t(i)].objs) {
            const int tx = objTex_[o.type];
            const float h = rc::OBJ[o.type].h, w = h * gpu_.texW(tx) / float(gpu_.texH(tx));
            const uint8_t side = o.off < 0 ? course_.segs[size_t(i)].left : course_.segs[size_t(i)].right;
            const float lift = side == gs::GROUND_SNOWWALL && std::fabs(o.off) > course_.segs[size_t(i)].hw + 0.6f ? 1.4f : 0;
            g32::billboard(gpu_, cam_, roadPoint((i + 0.5f) * SEG_M, o.off) + V3{0, lift, 0}, w, h, tx, o.flip);
        }

    // The car: its polygons, turned and lit.
    {
        const float yaw = carH, pitch = car_.pitch, roll = car_.roll;
        const float cyw = std::cos(yaw), syw = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
        auto xf = [&](float x, float y, float z) {
            y -= 0.7f;  // rolls turn about the middle of the car
            const float x1 = x * cr - y * sr, y1 = x * sr + y * cr;
            const float y2 = y1 * cp + z * sp, z2 = -y1 * sp + z * cp;
            return carPos + V3{x1 * cyw + z2 * syw, y2 + 0.7f, -x1 * syw + z2 * cyw};
        };
        const V3 light = g32::normalize({-0.35f, 0.85f, -0.4f});
        for (const rc::CarPoly& p : rc::carPolys()) {
            const int n = int(p.xyz.size() / 3);
            WVtx q[10];
            for (int k = 0; k < n && k < 10; k++) q[k].p = xf(p.xyz[size_t(k) * 3], p.xyz[size_t(k) * 3 + 1], p.xyz[size_t(k) * 3 + 2]);
            V3 nrm = g32::normalize(g32::cross(q[1].p - q[0].p, q[2].p - q[0].p));
            const bool facing = g32::dot(nrm, cam_.pos - q[0].p) > 0;
            if (!facing && !p.twoSided) continue;  // back faces
            if (!facing) nrm = nrm * -1;
            const uint16_t c = to15(livery_[rc::carColorIndex(p.mat, std::max(0.0f, g32::dot(nrm, light)))]);
            for (int k = 0; k < n; k++) shadeOf(c, q[k].r, q[k].g, q[k].b);
            // Wheels and caps have up to 10 sides: fan them out in triangles.
            for (int k = 1; k + 1 < n; k++) {
                WVtx t[3] = {q[0], q[k], q[k + 1]};
                g32::polygon(gpu_, cam_, t, 3, -1, g32::OPAQUE, -0.2f);
            }
        }
        // A shadow under it.
        WVtx sh[4];
        const V3 gp = roadPoint(car_.s, car_.x) + V3{0, 0.03f, 0};
        const V3 f{std::sin(carH) * 2.2f, 0, std::cos(carH) * 2.2f}, r{std::cos(carH) * 1.0f, 0, -std::sin(carH) * 1.0f};
        sh[0].p = gp - f - r, sh[1].p = gp - f + r, sh[2].p = gp + f + r, sh[3].p = gp + f - r;
        for (WVtx& w : sh) w.r = w.g = w.b = 20;
        g32::polygon(gpu_, cam_, sh, 4, -1, g32::HALF, 0.1f);
    }
}

// ================================================================ HUD

void Rally32::text(const std::string& s, float x, float y, float scale, uint16_t color, int align) {
    const float adv = 7 * scale, w = s.size() * adv;
    if (align == 0) x -= w / 2;
    else if (align > 0) x -= w;
    uint8_t r, g, b;
    shadeOf(color, r, g, b);
    for (size_t i = 0; i < s.size(); i++) {
        const int c = static_cast<unsigned char>(s[i]);
        if (c <= 32 || c >= 128) continue;
        const float u = ((c - 32) % 16) * 8.0f, v = ((c - 32) / 16) * 8.0f;
        g32::Vtx a, bb, cc, d;
        const float px = x + i * adv;
        a.x = px, a.y = y, a.u = u, a.v = v;
        bb.x = px + 8 * scale, bb.y = y, bb.u = u + 8, bb.v = v;
        cc.x = px + 8 * scale, cc.y = y + 8 * scale, cc.u = u + 8, cc.v = v + 8;
        d.x = px, d.y = y + 8 * scale, d.u = u, d.v = v + 8;
        for (g32::Vtx* p : {&a, &bb, &cc, &d}) p->r = r, p->g = g, p->b = b;
        gpu_.quad(a, bb, cc, d, 0, font_);
    }
}

void Rally32::hud() {
    const uint16_t white = 0x7fff, yellow = uint16_t(31 << 10 | 26 << 5 | 0), red = uint16_t(31 << 10 | 6 << 5 | 4);
    const rc::Venue& V = rc::venue(course_.venue);
    if (mode_ == Mode::Title) {
        const float w = 250, h = w * gpu_.texH(logo_) / gpu_.texW(logo_);
        gpu_.sprite(160 - w / 2, 34, w, h, logo_, 0, 0, float(gpu_.texW(logo_)), float(gpu_.texH(logo_)));
        text("S3-32 PREVIEW", 160, 150, 1.4f, white);
        if (t_ % 60 < 40) text("PRESS START", 160, 176, 2, yellow);
        text("REAL 3D ON THE 32-BIT MACHINE", 160, 206, 1, white);
        text(S3_VERSION_STRING, 316, 228, 1, white, 1);
        return;
    }
    if (mode_ == Mode::Pick) {
        text("CHOOSE A STAGE", 160, 20, 2, yellow);
        text(std::string(V.name) + " - " + course_.name, 160, 48, 1.4f, white);
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.2f KM   %s", course_.stageMetres / 1000, rc::carSpec(carId_).name);
        text(buf, 160, 66, 1, white);
        text("LEFT/RIGHT STAGE  UP/DOWN CAR  START GO", 160, 214, 1, white);
        if (best_ > 0) text("BEST " + fmtTime(best_), 160, 196, 1, yellow);
        return;
    }
    text(fmtTime(time_ + car_.penalty), 8, 8, 1.6f, white, -1);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f KM", std::max(0.0f, course_.finishSeg * SEG_M - car_.s) / 1000);
    text(buf, 312, 8, 1.2f, white, 1);
    text(std::to_string(int(car_.kmh())), 300, 204, 2.4f, white, 1);
    text("KM/H", 314, 214, 1, yellow, 1);
    if (!car_.why.empty() && (car_.state == rc::CarState::Rolling || car_.state == rc::CarState::Recovering)) text(car_.why, 160, 70, 2, red);
    if (mode_ == Mode::Done) {
        text("FINISH", 160, 60, 3, yellow);
        text(fmtTime(time_), 160, 96, 2, white);
        if (t_ > 60 && t_ % 60 < 40) text("PRESS START", 160, 180, 1.4f, yellow);
    }
}

// ================================================================ headless

float Rally32::simulate(int stage, int frames, std::vector<std::string>* shots, const std::string& dir) {
    loadStage(stage);
    attract_ = false;
    mode_ = Mode::Drive;
    t_ = 0;
    for (int f = 0; f < frames && mode_ == Mode::Drive; f++) {
        sys_->step();
        if (shots && (f == 60 * 8 || f == 60 * 30)) {
            sys_->render();
            const std::string p = dir + "/g32-stage" + std::to_string(stage) + "-" + std::to_string(f / 60) + ".png";
            if (sys_->saveScreenshot(p)) shots->push_back(p);
        }
    }
    return mode_ == Mode::Done ? time_ : 0;
}

}  // namespace rc32
