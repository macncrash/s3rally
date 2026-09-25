// S3 RALLY CHAMPIONSHIP - drawing: the road from the camera, the scenery
// and cars as scaled sprites, the cockpit, the HUD and the menus.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game.h"

namespace rc {

std::string fmtTime(float t);

namespace {
constexpr float HALF = gs::SCREEN_W / 2.0f;
constexpr float F = 190;           // focal length, pixels
constexpr float HORIZON0 = 92;
constexpr int DRAW = 360;          // segments of road drawn (288 m)
constexpr int SPRITE_DRAW = 220;   // segments of scenery
constexpr float SEG_M = SEG / U;
constexpr float PI = 3.14159265f;
constexpr float TAU = 6.28318530f;
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

uint16_t lerpColor(uint16_t a, uint16_t b, float t) {
    t = clampf(t, 0, 1);
    auto ch = [&](int sh) { return int(std::lround(((a >> sh) & 15) + (((b >> sh) & 15) - ((a >> sh) & 15)) * t)); };
    return gs::rgb4(ch(8), ch(4), ch(0));
}

std::string ordinal(int n) {
    const char* suf = (n % 100 >= 11 && n % 100 <= 13) ? "TH" : n % 10 == 1 ? "ST" : n % 10 == 2 ? "ND" : n % 10 == 3 ? "RD" : "TH";
    return std::to_string(n) + suf;
}

std::string signedTime(float d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%c%.2f", d < 0 ? '-' : '+', std::fabs(d));
    return buf;
}

struct CamSpec {
    float h, d, x;  // height, distance behind the car, offset to the side (metres)
};
CamSpec camSpec(View v) {
    switch (v) {
        case View::Far: return {2.3f, 6.6f, 0};
        case View::Cockpit: return {1.12f, -0.25f, -0.36f};
        default: return {1.45f, 3.9f, 0};
    }
}
}  // namespace

// ================================================================ helpers

void RallyChamp::spr(const gs::Mipped& m, float cx, float bottom, float h, int pal, bool flip, int fog, int clipY, bool shadow) {
    if (h < 1 || fog >= 16) return;
    const float w = h * m.w / m.h;
    gs::Sprite s;
    s.x = int16_t(std::lround(cx - w / 2));
    s.y = int16_t(std::lround(bottom - h));
    s.w = int16_t(std::max(1L, std::lround(w)));
    s.h = int16_t(std::max(1L, std::lround(h)));
    s.img = m.pick(h);
    s.pal = uint8_t(pal);
    s.fog = uint8_t(fog);
    s.hflip = flip;
    s.shadow = shadow;
    s.clipY = int16_t(clipY);
    vdp_->sprite(s);
}

void RallyChamp::text(const std::string& s, float x, float y, float scale, int pal, int align) {
    const float adv = 11 * scale;
    const float w = s.size() * adv;
    if (align == 0) x -= w / 2;
    else if (align > 0) x -= w;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c < 32 || c >= 128) continue;
        const gs::Mipped& g = art_.glyph[c - 32];
        spr(g, x + i * adv + g.w * scale / 2, y + g.h * scale, g.h * scale, pal, false, 0);
    }
}

void RallyChamp::hud(int col, int row, const std::string& s, int pal) {
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c <= 32 || c >= 128) continue;
        vdp_->HUD.set(col + int(i), row, gs::entry(art_.fontTile[c - 32], pal));
    }
}

int RallyChamp::fogFor(float metres) const {
    const Venue& V = venue(venue_);
    float nearM = V.fogNear, farM = V.fogFar;
    if (loadedTod_ == 2) { nearM = 14; farM = 110; }  // headlights at night
    else if (loadedTod_ == 1) { nearM *= 0.8f; farM *= 0.8f; }
    return int(clampf((metres - nearM) / (farM - nearM) * 16, 0, 16));
}

void RallyChamp::drawCar(float cx, float groundY, float ppm, float yawRel, float pitch, float roll, int pal, int fog, int clip, bool rolling,
                         int rollView) {
    const float k = ppm / CAR_PPM;
    const float h = CAR_BH * k;
    const float bottom = groundY + (CAR_BH - CAR_GROUND) * k;
    const gs::Mipped* m;
    if (rolling) {
        int f = int(std::lround(roll / TAU * ROLL_FRAMES));
        f = ((f % ROLL_FRAMES) + ROLL_FRAMES) % ROLL_FRAMES;
        m = &art_.carRoll[rollView][f];
    } else {
        int y = int(std::lround(yawRel / TAU * YAW_FRAMES));
        y = ((y % YAW_FRAMES) + YAW_FRAMES) % YAW_FRAMES;
        const int p = pitch < -0.08f ? 0 : pitch > 0.08f ? 2 : 1;
        m = &art_.car[y][p];
    }
    spr(*m, cx, bottom, h, pal, false, fog, clip);
}

// ================================================================ the frame

void RallyChamp::render() {
    gs::VDP& v = *vdp_;
    v.clearSprites();
    v.HUD.clear();
    float camS = 0, camX = 0, camY = 0, camPsi = 0;
    drawRoad(camS, camX, camY, camPsi);
    const bool menu = mode_ != Mode::Start && mode_ != Mode::Stage && mode_ != Mode::Finish && mode_ != Mode::Title;
    // Sprites: earlier ones are drawn on top, so the HUD and cockpit go first.
    drawHud();
    drawMenus();
    if (!menu && view_ == View::Cockpit && mode_ != Mode::Title) drawCockpit();
    drawWorld(camS, camX, camY, camPsi);
}

void RallyChamp::drawRoad(float& camS, float& camX, float& camY, float& camPsi) {
    gs::VDP& v = *vdp_;
    const Venue& V = venue(venue_);
    const bool menu = mode_ != Mode::Start && mode_ != Mode::Stage && mode_ != Mode::Finish && mode_ != Mode::Title;
    const View view = (mode_ == Mode::Start || mode_ == Mode::Stage || mode_ == Mode::Finish) ? view_ : View::Chase;
    const CamSpec cs = camSpec(view);
    const Car& c = car_;

    // Heading: the chase camera follows the direction of travel, so a slide shows
    // the car sideways; the cockpit looks where the car points.
    float want = c.psi;
    if (view != View::Cockpit && c.speed() > 3 && c.state == CarState::Driving) {
        const float ds = c.along(), dx = c.u * std::sin(c.psi) + c.v * std::cos(c.psi);
        want = std::atan2(dx, std::max(ds, 0.5f));
    }
    if (view == View::Cockpit) camPsi_ = want;
    else camPsi_ += (clampf(want, -0.7f, 0.7f) - camPsi_) * 0.12f;
    camPsi = camPsi_;
    camS = c.s - cs.d * std::cos(camPsi);
    camX = c.x - cs.d * std::sin(camPsi) + cs.x * std::cos(camPsi);
    const float groundCam = c.ground(course_, camS);
    if (view == View::Cockpit) {
        camY_ = c.y + cs.h - c.compress;
    } else {
        const float target = std::max(groundCam + cs.h, c.y + cs.h * 0.8f);
        camY_ += (target - camY_) * (target > camY_ ? 0.06f : 0.14f);  // slow to rise: the car leaps on screen
        camY_ = std::max(camY_, groundCam + 0.5f);
    }
    camY = camY_;
    const float pitchWant = view == View::Cockpit ? c.pitch : c.pitch * 0.25f;
    camPitch_ += (pitchWant - camPitch_) * 0.3f;
    const float shake = shake_ > 0 ? (float(std::rand() % 100) / 100.0f - 0.5f) * shake_ : 0;
    const float shakeY = shake_ > 0 ? (float(std::rand() % 100) / 100.0f - 0.5f) * shake_ * 0.6f : 0;
    const float horizon = HORIZON0 + camPitch_ * F + shakeY + (view == View::Cockpit ? -c.compress * 40 : 0);

    // Project the road, segment by segment from the camera.
    for (auto& r : v.road) r.on = false;
    const auto& segs = course_.segs;
    const int N = course_.N;
    const float camZ = camS * U;
    const int base = std::clamp(int(camZ / SEG), 0, N - 1);
    const float basePct = (camZ - base * SEG) / SEG;
    float x = 0, dx = -segs[size_t(base)].curve * basePct - camPsi * SEG;
    int maxy = gs::SCREEN_H;
    const float camXu = camX * U, camYu = camY * U;
    const float nearZ = 0.4f * U;
    for (int n = 0; n < DRAW && base + n < N; n++) {
        Segment& seg = const_cast<Segment&>(segs[size_t(base + n)]);
        const float z1 = seg.z1 - camZ, z2 = seg.z2 - camZ;
        auto proj = [&](Proj& p, float wx, float wy, float z) {
            p.z = z;
            p.s = F / z;
            p.x = HALF + p.s * (wx - camXu) + shake;
            p.y = horizon - p.s * (wy - camYu);
            p.w = p.s * seg.hw * U;
        };
        proj(seg.p1, x, seg.y1, std::max(z1, nearZ));
        proj(seg.p2, x + dx, seg.y2, std::max(z2, nearZ));
        x += dx;
        dx += seg.curve;
        seg.clip = float(maxy);
        seg.vis = -1;
        if (z2 <= nearZ) continue;
        seg.vis = int(frameNo_ & 0x7fffffff);
        const Proj &p1 = seg.p1, &p2 = seg.p2;
        if (p2.y >= maxy || p2.y >= p1.y) continue;
        const int y0 = std::max(0, int(std::ceil(p2.y)));
        const int y1 = std::min(maxy, int(std::ceil(p1.y)));
        const float inv = 1.0f / (p1.y - p2.y);
        const uint8_t pal = uint8_t(ROAD_PALS[surfPalette(venue_, seg.surf)]);
        const uint8_t style = uint8_t(surfStyle(venue_, seg.surf));
        for (int y = y0; y < y1; y++) {
            const float t = (p1.y - y - 0.5f) * inv;
            gs::RoadLine& r = v.road[y];
            r.on = true;
            r.cx = p1.x + (p2.x - p1.x) * t;
            r.hw = p1.w + (p2.w - p1.w) * t;
            r.v = seg.z1 + SEG * t;
            r.pal = pal;
            r.band = seg.band;
            r.style = style;
            r.left = seg.left;
            r.right = seg.right;
            lineZ_[y] = (p1.z + (p2.z - p1.z) * t) / U;
        }
        if (y0 < maxy) maxy = y0;
    }
    for (int y = std::max(1, maxy + 1); y < gs::SCREEN_H; y++)
        if (!v.road[y].on) {
            v.road[y] = v.road[y - 1];
            lineZ_[y] = lineZ_[y - 1];
        }
    horizon_ = maxy;

    // Dust hanging in the road ahead thickens the air.
    float dusty = 0;
    for (const Cloud& cl : clouds_) {
        const float d = cl.s - c.s;
        if (d > -2 && d < 40) dusty += (1 - float(cl.life) / cl.max) * 0.35f;
    }
    dusty = clampf(dusty, 0, 7);

    // Sky, fog and the parallax backdrop.
    const int dimFog = menu ? 9 : 0;
    const bool night = loadedTod_ == 2;
    const uint16_t skyTop = night ? gs::rgb4(0, 0, 2) : loadedTod_ == 1 ? gs::rgb4(4, 3, 8) : V.skyTop;
    const uint16_t skyHor = night ? gs::rgb4(2, 3, 6) : loadedTod_ == 1 ? gs::rgb4(15, 8, 4) : V.skyHorizon;
    const float headingW = course_.heading(std::clamp(int(camZ / SEG), 0, N - 1)) + camPsi;
    const int bvs = BACKDROP_BASE - maxy;
    for (int y = 0; y < gs::SCREEN_H; y++) {
        if (y < maxy) {
            v.lineBackdrop[y] = lerpColor(skyTop, skyHor, float(y) / std::max(1.0f, horizon + 24));
            const int near = std::max(0, 14 - (maxy - y));
            v.lineFog[y] = uint8_t(std::max({V.backdropFog + int(dusty), std::min(near, 12), dimFog}));
        } else {
            v.lineFog[y] = uint8_t(std::min(16, std::max(fogFor(lineZ_[y]) + int(dusty * (1 - std::min(1.0f, lineZ_[y] / 60))) * 2, dimFog)));
            v.lineBackdrop[y] = V.fog;  // seen past a drop: the haze over the valley
        }
        float wobble = 0;
        if (V.weather == 2 && std::abs(y - maxy) < 10) wobble = std::sin(y * 1.3f + frameNo_ * 0.21f) * 1.2f;  // heat haze
        v.B.vscroll[y] = int16_t(bvs);
        // Below the horizon the near plane shows its lowest row: the valley floor beyond a drop.
        v.A.vscroll[y] = int16_t(y > maxy + 5 ? 255 - y : bvs);
        v.B.hscroll[y] = int16_t(std::lround(-headingW * F * 0.9f + (y + bvs < CLOUD_ROWS ? frameNo_ * 0.05f : 0) + shake + wobble));
        v.A.hscroll[y] = int16_t(std::lround(-headingW * F * 1.4f + shake + wobble));
    }
    v.A.enabled = v.B.enabled = true;
    v.setFogColor(menu ? gs::rgb4(0, 0, 1) : night ? gs::rgb4(0, 0, 1) : dusty > 1 ? V.dust[0] : V.fog);
}

void RallyChamp::drawWorld(float camS, float camX, float camY, float camPsi) {
    const auto& segs = course_.segs;
    const int N = course_.N;
    const int vis = int(frameNo_ & 0x7fffffff);
    const bool menu = mode_ != Mode::Start && mode_ != Mode::Stage && mode_ != Mode::Finish && mode_ != Mode::Title;
    const int dimFog = menu ? 9 : 0;
    const float camZ = camS * U;
    const int base = std::clamp(int(camZ / SEG), 0, N - 1);
    const View view = (mode_ == Mode::Start || mode_ == Mode::Stage || mode_ == Mode::Finish) ? view_ : View::Chase;
    const CamSpec cs = camSpec(view);
    const float camHeading = course_.heading(base) + camPsi;

    items_.clear();
    for (int n = 1; n < SPRITE_DRAW && base + n < N; n++) {
        const Segment& seg = segs[size_t(base + n)];
        if (seg.vis != vis) continue;
        for (const Placed& o : seg.objs) items_.push_back({seg.p1.z / U, 0, &seg, &o, 0});
    }
    for (size_t k = 0; k < others_.size(); k++) {
        const float d = others_[k].s - camS;
        if (d > 0.5f && d < SPRITE_DRAW * SEG_M && others_[k].s < (course_.stopSeg + 30) * SEG_M) items_.push_back({d, 1, nullptr, nullptr, int(k)});
    }
    for (size_t k = 0; k < clouds_.size(); k++) {
        const float d = clouds_[k].s - camS;
        if (d > 6 && d < 150) items_.push_back({d, 3, nullptr, nullptr, int(k)});  // (not right in the lens)
    }
    // The ghost of the best run (time attack).
    const std::vector<float>& gh = stage_ >= 0 ? ghost_[stage_] : ghostRec_;
    int ghostAt = -1;
    if (game_ == Game::TimeAttack && mode_ == Mode::Stage && !gh.empty()) {
        ghostAt = std::min(int(gh.size() / 3) - 1, int(stageTime_ * 10));
        const float d = gh[size_t(ghostAt) * 3] - camS;
        if (d > 1 && d < SPRITE_DRAW * SEG_M) items_.push_back({d, 4, nullptr, nullptr, ghostAt});
    }
    const bool showCar = view != View::Cockpit && mode_ != Mode::CarSelect;
    if (showCar) items_.push_back({std::max(0.5f, cs.d), 2, nullptr, nullptr, 0});
    std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.z < b.z; });

    struct Shadow { float x, y, w; int clip; };
    std::vector<Shadow> shadows;
    // Where a point on the road is on screen: the segment's projection, interpolated.
    auto place = [&](float s, float xOff, float& sx, float& sy, float& ppm, int& clip) -> bool {
        const int i = std::clamp(int(s * U / SEG), 0, N - 1);
        const Segment& seg = segs[size_t(i)];
        if (seg.vis != vis) return false;
        const float t = clampf((s * U - seg.z1) / SEG, 0, 1);
        const Proj &p1 = seg.p1, &p2 = seg.p2;
        const float k = p1.s + (p2.s - p1.s) * t;
        sx = p1.x + (p2.x - p1.x) * t + xOff * U * k;
        sy = p1.y + (p2.y - p1.y) * t;
        ppm = k * U;
        clip = int(seg.clip);
        return true;
    };

    for (const Item& it : items_) {
        switch (it.kind) {
            case 0: {  // scenery
                const Segment& seg = *it.seg;
                const Proj& p = seg.p1;
                if (p.y > seg.clip + 2) break;
                const int fog = std::max(fogFor(it.z), dimFog);
                if (fog >= 16) break;
                const ObjInfo& info = OBJ[it.obj->type];
                const gs::Mipped& m = art_.obj[it.obj->type];
                const float ppm = p.s * U;
                const float h = info.h * ppm;
                if (h < 2) break;
                const float w = h * m.w / m.h;
                const float xs = p.x + it.obj->off * ppm;
                if (xs + w < -40 || xs - w > gs::SCREEN_W + 40) break;
                spr(m, xs, p.y, h, info.scene ? PAL_SCENE : PAL_COMMON, it.obj->flip, fog, int(seg.clip));
                if (info.shadow && fog < 12) shadows.push_back({xs + w * 0.15f, p.y + 1, w * 1.1f, int(seg.clip)});
                break;
            }
            case 1: {  // another crew's car
                const Other& o = others_[size_t(it.index)];
                float sx, sy, ppm;
                int clip;
                if (!place(o.s, o.x, sx, sy, ppm, clip)) break;
                if (sy > clip + 3) break;
                const int fog = std::max(fogFor(it.z), dimFog);
                const int i = std::clamp(int(o.s * U / SEG), 0, N - 1);
                const float yawRel = o.psi + course_.heading(i) - camHeading - std::atan2((sx - HALF) / F, 1.0f);
                drawCar(sx, sy, ppm, yawRel, 0, 0, o.pal, fog, clip, false, 0);
                shadows.push_back({sx, sy + 1, ppm * 2.3f, clip});
                if (o.slot >= 0 && !o.name.empty() && fog < 12 && ppm > 12 && !menu)
                    text(o.name, sx, sy - ppm * 1.9f - 10, clampf(ppm / 60, 0.5f, 0.9f), PAL_YELLOW);
                break;
            }
            case 3: {  // dust cloud
                const Cloud& cl = clouds_[size_t(it.index)];
                float sx, sy, ppm;
                int clip;
                if (!place(cl.s, cl.x, sx, sy, ppm, clip)) break;
                const float fade = float(cl.life) / cl.max;
                const int fog = std::max(dimFog, std::min(15, 3 + int(fade * 10) + fogFor(it.z) / 3));  // dithered: see-through dust
                spr(art_.haze, sx, sy - cl.h * ppm, std::min(110.0f, cl.size * ppm * (1 + fade)), PAL_FX, (it.index & 1) != 0, fog, clip);
                break;
            }
            case 4: {  // ghost
                const float gs_ = gh[size_t(it.index) * 3], gx = gh[size_t(it.index) * 3 + 1], gp = gh[size_t(it.index) * 3 + 2];
                float sx, sy, ppm;
                int clip;
                if (!place(gs_, gx, sx, sy, ppm, clip)) break;
                const int i = std::clamp(int(gs_ * U / SEG), 0, N - 1);
                drawCar(sx, sy, ppm, gp + course_.heading(i) - camHeading, 0, 0, PAL_PLAYER, 10, clip, false, 0);
                break;
            }
            default: {  // our car, with the wheel spray around it
                const Car& c = car_;
                const float ppm = F / std::max(0.8f, cs.d);
                const float groundY = HORIZON0 + camPitch_ * F + F * (camY - c.ground(course_, c.s)) / std::max(0.8f, cs.d);
                const float carY = HORIZON0 + camPitch_ * F + F * (camY - c.y) / std::max(0.8f, cs.d);
                const float cx = HALF;  // the camera sits right behind the car
                const bool rolling = c.state == CarState::Rolling || (c.state == CarState::Recovering && std::fabs(std::remainder(c.roll, TAU)) > 0.2f);
                const float slideYaw = c.psi - camPsi;
                const int rollView = std::fabs(c.drift()) > 0.6f || std::fabs(slideYaw) > 0.8f ? 1 : 0;
                // Particles are in front of the car's lower half and behind the rest: draw the ones below first.
                for (const Particle& p : parts_) {
                    if (p.kind == 3) { spr(art_.spark, p.x, p.y, 3, PAL_YELLOW, false, 0); continue; }
                    const gs::Mipped& m = p.kind == 1 ? art_.spray : p.kind == 2 ? art_.splash : p.kind == 5 ? art_.clod : p.kind == 0 ? art_.haze : art_.puff;
                    const int fog = std::min(15, dimFog + p.life * 9 / std::max(1, p.max));
                    const int pal = PAL_FX;
                    spr(m, p.x, p.y + p.size / 2, p.size, pal, p.vx < 0, p.kind == 4 ? std::max(0, fog - 4) : fog);
                }
                drawCar(cx, carY, ppm, slideYaw, c.pitch - std::atan(c.groundSlope(course_, c.s)) * 0.5f, c.roll, PAL_PLAYER,
                        dimFog, 224, rolling, rollView);
                shadows.push_back({cx, groundY + 2, ppm * 2.4f, 224});
                break;
            }
        }
    }
    for (const Shadow& s : shadows) spr(art_.shadow, s.x, s.y + s.w * 0.07f, s.w * 0.25f, 0, false, 0, s.clip, true);
}

// ================================================================ cockpit

void RallyChamp::drawCockpit() {
    const Car& c = car_;
    const CarSpec& cs = carSpec(c.specId);
    // Wheel and hands on top of everything in the car.
    const float turn = c.steerIn * 2.1f;
    const int wf = std::clamp(int(std::lround(turn / (PI / 12))) + (WHEEL_FRAMES - 1) / 2, 0, WHEEL_FRAMES - 1);
    const float bob = c.compress * 30;
    spr(art_.wheel[wf], 108, 252 + bob, 132, PAL_HUD, false, 0);
    const int nf = std::clamp(int(c.rpm / cs.rpmMax * (NEEDLE_FRAMES - 1)), 0, NEEDLE_FRAMES - 1);
    spr(art_.needle[nf], 184, 214 + bob, 40, PAL_HUD, false, 0);
    // Digital speed and gear on the dash.
    hud(26, 25, std::to_string(int(c.kmh())), PAL_YELLOW);
    hud(30, 25, "KMH", PAL_HUD);
    hud(26, 26, c.gear < 0 ? "R" : std::to_string(c.gear), PAL_YELLOW);
    if (c.rpm > cs.rpmMax * 0.93f && frameNo_ % 8 < 5) hud(28, 26, "SHIFT", PAL_RED);
    spr(art_.dash, 160, 228 + bob, 70, PAL_HUD, false, 0);
    spr(art_.codriver, 286, 228 + bob * 0.6f, 96, PAL_COMMON, false, 0);
    // Pillars, roof, mirror.
    spr(art_.pillar, 30, 224, 224, PAL_HUD, false, 0);
    spr(art_.pillar, 290, 224, 224, PAL_HUD, true, 0);
    spr(art_.roofBar, 160, 24, 24, PAL_HUD, false, 0);
    spr(art_.mirror, 160, 34, 22, PAL_HUD, false, 0);
    // Mud and spray on the glass, and the wipers when they go.
    if (wiperT_ > 0) {
        const int ph = wiperT_ > 30 ? 0 : wiperT_ > 20 ? 1 : wiperT_ > 10 ? 2 : 1;
        spr(art_.wiper[ph], 160, 170, 150, PAL_HUD, false, 0);
    }
    if (dirt_ > 0.05f) {
        const int lv = dirt_ < 0.35f ? 0 : dirt_ < 0.7f ? 1 : 2;
        spr(art_.dirt[lv], 160, 170, 150, PAL_FX, false, 2);
    }
}

// ================================================================ HUD

void RallyChamp::drawRadio(int row) {
    if (radio_->cardFrames() <= 0) return;
    const std::string a = radio_->stationLine(), b = radio_->songLine();
    hud(20 - int(a.size()) / 2, row, a, PAL_YELLOW);
    if (!b.empty()) hud(20 - int(b.size()) / 2, row + 1, b, PAL_HUD);
}

void RallyChamp::drawNotes(int y) {
    // The next calls, soonest on the left and largest.
    const int i = car_.segIndex(course_);
    int shown = 0;
    float x = 118;
    // A dark strip behind the calls so they read over any scenery.
    for (int k = 0; k < 4; k++) spr(art_.panelWide, 104 + k * 32.0f, y + 46, 48, 0, false, 0, 224, true);
    for (const Note& n : course_.notes) {
        if (n.at < i - 2) continue;
        if (shown >= 3) break;
        const float sc = shown == 0 ? 1.0f : 0.62f;
        const int pal = (n.mods & M_CAUTION) ? PAL_RED : PAL_YELLOW;
        const int fog = shown == 0 ? 0 : 3;
        if (n.sev > 0) {
            spr(art_.icon[n.sev - 1], x, y + 40 * sc, 40 * sc, pal, n.dir < 0, fog);
            if (n.sev < 7) text(std::to_string(n.sev), x + 12 * sc, y + 22 * sc, 1.3f * sc, PAL_HUD, -1);
        } else if (n.mods & M_JUMP) {
            spr(art_.mod[MI_JUMP], x, y + 34 * sc, 34 * sc, pal, false, fog);
        } else if (n.mods & M_CREST) {
            spr(art_.mod[MI_CREST], x, y + 34 * sc, 34 * sc, pal, false, fog);
        } else if (n.mods & M_WATER) {
            spr(art_.mod[MI_WATER], x, y + 34 * sc, 34 * sc, pal, false, fog);
        } else if (n.at == course_.finishSeg) {
            spr(art_.mod[MI_FINISH], x, y + 34 * sc, 34 * sc, PAL_HUD, false, fog);
        } else {
            spr(art_.mod[MI_CAUTION], x, y + 30 * sc, 30 * sc, PAL_HUD, false, fog);
        }
        // Modifier badges stacked beside the next corner (crest, jump, don't cut, tightens, caution).
        if (shown == 0 && n.sev > 0) {
            const int kinds[5] = {M_CREST, M_JUMP, M_DONTCUT, M_TIGHTENS, M_CAUTION};
            const int icons[5] = {MI_CREST, MI_JUMP, MI_DONTCUT, MI_TIGHTENS, MI_CAUTION};
            float my = y + 16;
            for (int k = 0; k < 5; k++)
                if (n.mods & kinds[k]) {
                    spr(art_.mod[icons[k]], x + 30, my, 14, k == 4 ? PAL_RED : PAL_HUD, false, 0);
                    my += 14;
                }
        }
        if (shown == 0) {
            std::string t = n.text;
            if (t.size() > 22) t.resize(22);
            hud(20 - int(t.size()) / 2, y / 8 + 6, t, pal);
        }
        x += shown == 0 ? 58 : 34;
        shown++;
    }
}

void RallyChamp::drawHud() {
    const Venue& V = venue(venue_);
    const bool racing = mode_ == Mode::Start || mode_ == Mode::Stage || mode_ == Mode::Finish;
    if (V.weather == 1 && mode_ != Mode::Menu && view_ != View::Cockpit)
        for (const Flake& f : flakes_) spr(art_.flake, f.x, f.y, 2 + f.s, PAL_FX, false, 0);
    if (!msg_.empty() && (racing || mode_ == Mode::Title))
        for (size_t i = 0; i < msg_.size(); i++) text(msg_[i], HALF, 62 + i * 22.0f, i == 0 ? 1.6f : 1.1f, msgPal_);
    if (toastT_ > 0 && !toast_.empty()) {
        const std::string t = toast_.substr(0, 38);
        hud(20 - int(t.size()) / 2, 26, t, PAL_YELLOW);
    }
    if (!racing) {
        if (mode_ == Mode::Title) drawRadio(22);
        return;
    }
    drawRadio(12);
    const Car& c = car_;

    // Stage clock and splits.
    hud(1, 1, "STAGE", PAL_YELLOW);
    const float shown = mode_ == Mode::Start ? 0 : (finished_ ? myTime_[stage_] : stageTime_ + c.penalty);
    text(fmtTime(shown), 10, 14, 1.2f, PAL_HUD, -1);
    if (c.penalty > 0) hud(1, 5, "PENALTY +" + std::to_string(int(c.penalty)) + "S", PAL_RED);
    if (splitShow_ > 0) {
        hud(1, 6, "SPLIT " + std::to_string(splitsDone_), PAL_YELLOW);
        hud(9, 6, signedTime(lastSplitDelta_), lastSplitDelta_ <= 0 ? PAL_YELLOW : PAL_RED);
    }
    // Distance to go and the road ahead on a strip: us, the car ahead, the car behind.
    const float togo = std::max(0.0f, (course_.finishSeg * SEG_M - c.s)) / 1000;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f KM", togo);
    hud(39 - int(std::strlen(buf)), 1, buf, PAL_HUD);
    {
        const float x0 = 236, x1 = 316, y = 22;
        const float a = course_.startSeg * SEG_M, b = course_.finishSeg * SEG_M;
        auto px = [&](float s) { return x0 + clampf((s - a) / (b - a), 0, 1) * (x1 - x0); };
        spr(art_.panelWide, (x0 + x1) / 2, y + 1, 3, PAL_HUD, false, 0, 224, true);
        for (const Split& sp : course_.splits) spr(art_.panel, px(sp.dist + a), y + 2, 5, PAL_HUD, false, 0);
        for (const Other& o : others_)
            if (o.running) spr(art_.panel, px(o.s), y + 1, 4, o.startAt < 0 ? PAL_YELLOW : PAL_RED, false, 0);
        spr(art_.panel, px(c.s), y + 3, 7, PAL_HUD, false, 0);
    }
    // Gap to the car ahead once it's close.
    for (const Other& o : others_)
        if (o.startAt < 0 && o.running && o.s > c.s && o.s - c.s < 250 && mode_ == Mode::Stage)
            hud(26, 5, "CAR AHEAD " + std::to_string(int(o.s - c.s)) + "M", PAL_HUD);

    if (view_ != View::Cockpit || mode_ == Mode::Finish) drawNotes(24);
    else drawNotes(28);

    // The start clock.
    if (mode_ == Mode::Start) {
        const int go = startGo_;
        const int secs = (go - t_ + 59) / 60;
        const Venue& Vn = venue(venue_);
        hud(20 - int(std::strlen(Vn.stages[stageNo_])) / 2, 10, Vn.stages[stageNo_], PAL_YELLOW);
        if (secs <= 5 && secs >= 1) text(std::to_string(secs), HALF, 92, 3.2f - ((t_ % 60) / 60.0f), secs <= 3 ? PAL_RED : PAL_YELLOW);
        else if (secs > 5) hud(15, 13, "START IN " + std::to_string(secs), PAL_HUD);
    }
    if (paused_) {
        text("PAUSE", HALF, 80, 2, PAL_HUD);
        hud(12, 15, "START  RESUME", PAL_HUD);
        hud(12, 17, "ESC    QUIT STAGE", PAL_HUD);
    }

    // Damage.
    const Damage& d = c.damage;
    const float parts[4] = {d.engine, d.suspension, std::max(d.tyres, d.puncture ? 1.0f : 0.0f), d.body};
    for (int k = 0; k < 4; k++)
        if (parts[k] > 0.12f) spr(art_.damage[k], 14 + k * 20.0f, 214, 16, parts[k] > 0.55f ? PAL_RED : PAL_YELLOW, false, 0);

    if (view_ == View::Cockpit && mode_ != Mode::Finish) return;
    // Speed, gear and revs.
    const CarSpec& cs = carSpec(c.specId);
    text(std::to_string(int(c.kmh())), 280, 190, 1.8f, PAL_HUD, 1);
    hud(35, 27, "KM/H", PAL_YELLOW);
    text(c.gear < 0 ? "R" : std::to_string(c.gear), 314, 190, 1.5f, PAL_YELLOW, 1);
    hud(37, 22, manual_ ? "MT" : "AT", PAL_HUD);
    const int lit = int(c.rpm / cs.rpmMax * 14);
    for (int i = 0; i < 14; i++) {
        const int kind = i >= lit ? 3 : i < 9 ? 0 : i < 12 ? 1 : 2;
        spr(art_.rpm[kind], 192 + i * 7.0f, 186, 8 + i * 0.7f, PAL_HUD, false, 0);
    }
}

// ================================================================ map

void RallyChamp::drawMap(float cx, float cy) {
    uint8_t* px = vdp_->rom() + art_.map.off;
    const int W = art_.map.w;
    std::fill(px, px + W * art_.map.h, uint8_t(0));
    auto dot = [&](float fx, float fy, int r, int col) {
        const int x0 = 4 + int(fx * (W - 9)), y0 = 4 + int(fy * (W - 9));
        for (int y = y0 - r; y <= y0 + r; y++)
            for (int x = x0 - r; x <= x0 + r; x++)
                if (x >= 0 && y >= 0 && x < W && y < W) px[y * W + x] = uint8_t(col);
    };
    for (int i = course_.startSeg; i < course_.finishSeg; i += 3) dot(course_.map[size_t(i)].first, course_.map[size_t(i)].second, 1, 8);
    for (int i = course_.startSeg; i < course_.finishSeg; i += 3) dot(course_.map[size_t(i)].first, course_.map[size_t(i)].second, 0, 2);
    dot(course_.map[size_t(course_.startSeg)].first, course_.map[size_t(course_.startSeg)].second, 2, 5);
    dot(course_.map[size_t(course_.finishSeg)].first, course_.map[size_t(course_.finishSeg)].second, 2, 4);
    gs::Mipped m;
    m.w = m.h = W;
    m.lv[0] = m.lv[1] = m.lv[2] = art_.map;
    spr(m, cx, cy, float(W) * 1.4f, PAL_HUD, false, 0);
}

// ================================================================ menus

void RallyChamp::drawMenus() {
    const Venue& V = venue(venue_);
    auto verLine = [&] { hud(39 - int(std::strlen(S3_VERSION_STRING)), 27, S3_VERSION_STRING, PAL_HUD); };
    switch (mode_) {
        case Mode::Title:
            spr(art_.logo, HALF, 132, 118, PAL_LOGO, false, 0);
            if (frameNo_ % 60 < 40) text("PRESS START", HALF, 160, 1.4f, PAL_YELLOW);
            verLine();
            if (radio_->cardFrames() <= 0) {
                hud(10, 25, "(C) 2026 MACNCRASH", PAL_HUD);
                hud(12, 26, "S3-16 SYSTEM", PAL_HUD);
            }
            break;
        case Mode::Menu: {
            spr(art_.logo, HALF, 60, 50, PAL_LOGO, false, 0);
            static const char* LEVEL[3] = {"AMATEUR", "PRO", "LEGEND"};
            const std::string champ = std::string("CHAMPIONSHIP  < ") + LEVEL[difficulty_] + " >";
            const std::string single = std::string("SINGLE RALLY  < ") + LEVEL[difficulty_] + " >";
            const std::string items[7] = {champ, single, "TIME ATTACK", "ONLINE", "PROFILE", "CONTROLS",
                                          radio_->station() < 0 ? "RADIO  OFF" : "RADIO  ON"};
            for (int i = 0; i < 7; i++) hud(20 - int(items[i].size()) / 2, 9 + i * 2, items[i], i == menuSel_ ? PAL_YELLOW : PAL_HUD);
            hud(4, 9 + menuSel_ * 2, ">", PAL_YELLOW);
            const std::string who = "DRIVER: " + profile_.name;
            const char* help[7] = {"5 RALLIES, 15 STAGES. LEFT/RIGHT: LEVEL", "ONE RALLY: THREE STAGES AND SERVICE",
                                   "ONE STAGE. BEAT YOUR GHOST.", rally::Versus::available() ? "UP TO 4 PLAYERS, LAN OR INTERNET" : "ONLINE NEEDS THE DESKTOP VERSION",
                                   who.c_str(), "SEE AND REMAP BUTTONS", "CHANGE STATION. TAB IN GAME."};
            hud(20 - int(std::strlen(help[menuSel_])) / 2, 25, help[menuSel_], PAL_HUD);
            verLine();
            break;
        }
        case Mode::Pick: {
            if (game_ == Game::Rally) {
                text("CHOOSE A RALLY", HALF, 12, 1.4f, PAL_YELLOW);
                for (int i = 0; i < NUM_VENUES; i++) {
                    const Venue& vv = venue(i);
                    hud(2, 6 + i * 3, (i == pickSel_ ? "> " : "  ") + std::string(vv.rally), i == pickSel_ ? PAL_YELLOW : PAL_HUD);
                    hud(4, 7 + i * 3, vv.character, PAL_HUD);
                }
            } else {
                text("TIME ATTACK", HALF, 8, 1.4f, PAL_YELLOW);
                for (int i = 0; i < NUM_STAGES; i++) {
                    const Venue& vv = venue(i / 3);
                    std::string line = std::string(vv.name).substr(0, 9);
                    line.resize(11, ' ');
                    line += vv.stages[i % 3];
                    line.resize(26, ' ');
                    line += best_[i] > 0 ? fmtTime(best_[i]) : "-";
                    hud(3, 5 + i, (i == pickSel_ ? "> " : "  ") + line, i == pickSel_ ? PAL_YELLOW : PAL_HUD);
                }
            }
            hud(8, 26, "ENTER SELECT   ESC BACK", PAL_HUD);
            break;
        }
        case Mode::CarSelect: {
            const CarSpec& c = carSpec(carId_);
            text("CHOOSE YOUR CAR", HALF, 12, 1.4f, PAL_YELLOW);
            const float yaw = frameNo_ * 0.02f;
            drawCar(HALF, 118, 40, yaw, 0, 0, PAL_PLAYER, 0, 224, false, 0);
            hud(20 - int(std::strlen(c.name)) / 2, 18, c.name, PAL_YELLOW);
            hud(20 - int(std::strlen(c.blurb)) / 2, 19, c.blurb, PAL_HUD);
            auto bar = [&](int row, const char* label, float val) {
                hud(8, row, label, PAL_HUD);
                std::string b;
                const int n = std::clamp(int(std::lround(val * 12)), 0, 12);
                for (int i = 0; i < 12; i++) b += i < n ? '#' : '-';
                hud(19, row, b, PAL_YELLOW);
            };
            bar(21, "POWER", (c.torque / c.mass - 0.28f) / 0.12f);
            bar(22, "GRIP", (c.grip - 0.9f) / 0.12f);
            bar(23, "4WD", c.frontDrive / 0.45f);
            hud(6, 25, std::string(manual_ ? "MANUAL (Q/W)" : "AUTOMATIC") + (assist_ ? " + TRACTION HELP" : " - NO HELP"), PAL_HUD);
            hud(12, 26, "UP/DOWN: SET-UP", PAL_HUD);
            text("<", 40, 100, 2, PAL_YELLOW);
            text(">", 280, 100, 2, PAL_YELLOW);
            break;
        }
        case Mode::RallyIntro: {
            text(V.rally, HALF, 24, 1.3f, PAL_YELLOW);
            text(V.name, HALF, 50, 2.4f, PAL_HUD);
            hud(20 - int(std::strlen(V.character)) / 2, 11, V.character, PAL_YELLOW);
            if (game_ == Game::Championship) hud(12, 13, "ROUND " + std::to_string(venue_ + 1) + " OF 5", PAL_HUD);
            for (int k = 0; k < STAGES_PER_VENUE; k++)
                hud(9, 16 + k * 2, "SS" + std::to_string(k + 1) + "  " + std::string(V.stages[k]), PAL_HUD);
            if (t_ > 30 && frameNo_ % 60 < 40) hud(14, 25, "PRESS START", PAL_YELLOW);
            break;
        }
        case Mode::StageIntro: {
            char len[32];
            std::snprintf(len, sizeof len, "%.2f KM", course_.stageMetres / 1000);
            text("SS" + std::to_string(stageNo_ + 1), 70, 22, 2, PAL_YELLOW);
            text(course_.name, 70, 50, 1.3f, PAL_HUD, -1);
            hud(9, 10, V.rally, PAL_HUD);
            hud(9, 12, len, PAL_YELLOW);
            const char* tod = loadedTod_ == 2 ? "NIGHT" : loadedTod_ == 1 ? "DUSK" : "DAY";
            hud(9, 14, std::string("SURFACE ") + SURF[course_.segs[size_t(course_.startSeg + 50)].surf].name, PAL_HUD);
            hud(9, 15, std::string("TIME    ") + tod, PAL_HUD);
            if (game_ == Game::Championship || game_ == Game::Rally) {
                int me = 0;
                for (size_t k = 0; k < startOrder_.size(); k++)
                    if (startOrder_[k] < 0) me = int(k);
                hud(9, 17, "START POSITION " + std::to_string(me + 1), PAL_HUD);
                for (const Other& o : others_) {
                    if (o.startAt == -10) hud(9, 18, "AHEAD  " + o.name, PAL_HUD);
                    if (o.startAt == 10) hud(9, 19, "BEHIND " + o.name, PAL_HUD);
                }
                const Damage& d = car_.damage;
                if (d.total() > 0.02f) hud(9, 21, "CAR DAMAGE " + std::to_string(int(d.total() * 100)) + "%", PAL_RED);
            } else if (game_ == Game::TimeAttack && best_[stage_] > 0) {
                hud(9, 17, "BEST " + fmtTime(best_[stage_]), PAL_YELLOW);
                hud(9, 18, "RACE YOUR GHOST", PAL_HUD);
            }
            drawMap(250, 150);
            if (t_ > 30 && frameNo_ % 60 < 40) hud(14, 25, "PRESS START", PAL_YELLOW);
            break;
        }
        case Mode::Result:
            drawResult();
            break;
        case Mode::Service:
            drawService();
            break;
        case Mode::Standings:
        case Mode::Podium:
            drawStandings();
            break;
        case Mode::Lobby:
            drawLobby();
            break;
        case Mode::Profile:
            drawProfile();
            break;
        case Mode::Controls:
            drawControls();
            break;
        default:
            break;
    }
}

void RallyChamp::drawResult() {
    const Venue& V = venue(venue_);
    text("SS" + std::to_string(stageNo_ + 1) + " " + course_.name, HALF, 8, 1.1f, PAL_YELLOW);
    if (game_ == Game::TimeAttack) {
        text(fmtTime(myTime_[stage_]), HALF, 60, 2.2f, PAL_HUD);
        if (newRecord_ && frameNo_ % 40 < 28) text("NEW RECORD!", HALF, 100, 1.4f, PAL_RED);
        else if (best_[stage_] > 0) hud(12, 15, "BEST " + fmtTime(best_[stage_]), PAL_HUD);
    } else if (game_ == Game::Online) {
        struct Row { std::string name; float t; bool me, fin; };
        std::vector<Row> rows = {{profile_.name, myTime_[stage_], true, true}};
        for (int s = 0; s < rally::MAX_PLAYERS; s++) {
            if (s == versus_.mySlot || !versus_.players[s].active) continue;
            const rally::NetPlayer& p = versus_.players[s];
            rows.push_back({p.name, p.state.time, false, p.state.finished});
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.fin != b.fin ? a.fin : a.t < b.t; });
        for (size_t k = 0; k < rows.size(); k++) {
            std::string n = rows[k].name;
            n.resize(13, ' ');
            hud(6, 8 + int(k) * 2, ordinal(int(k) + 1) + "  " + n + (rows[k].fin ? fmtTime(rows[k].t) : "ON STAGE"), rows[k].me ? PAL_YELLOW : PAL_HUD);
        }
    } else {
        // Stage times: everyone, fastest first.
        struct Row { std::string name; float t; bool me; };
        std::vector<Row> rows = {{profile_.name.empty() ? "YOU" : profile_.name, myTime_[stage_] + myPenalty_[stage_], true}};
        for (const Crew& c : crews_)
            if (!c.out && c.time[stage_] > 0) rows.push_back({c.name, c.time[stage_], false});
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.t < b.t; });
        const float best = rows.empty() ? 0 : rows[0].t;
        int mine = 0;
        for (size_t k = 0; k < rows.size(); k++)
            if (rows[k].me) mine = int(k);
        hud(4, 4, "STAGE TIMES", PAL_YELLOW);
        const int first = std::clamp(mine - 4, 0, std::max(0, int(rows.size()) - 9));
        for (int k = first; k < first + 9 && k < int(rows.size()); k++) {
            std::string n = rows[size_t(k)].name;
            n.resize(13, ' ');
            const std::string gap = k == 0 ? "" : " " + signedTime(rows[size_t(k)].t - best);
            hud(3, 6 + (k - first) * 2, (k + 1 < 10 ? " " : "") + std::to_string(k + 1) + ". " + n + fmtTime(rows[size_t(k)].t) + gap,
                rows[size_t(k)].me ? PAL_YELLOW : PAL_HUD);
        }
        if (myPenalty_[stage_] > 0) hud(3, 25, "INCLUDES PENALTIES +" + std::to_string(int(myPenalty_[stage_])) + "S", PAL_RED);
        (void)V;
    }
    if (t_ > 40 && frameNo_ % 60 < 40) hud(14, 26, "PRESS START", PAL_YELLOW);
}

void RallyChamp::drawService() {
    text("SERVICE", HALF, 10, 1.6f, PAL_YELLOW);
    hud(8, 5, "15 MINUTES. LATE: +10S A MINUTE", PAL_HUD);
    const Damage& d = car_.damage;
    const float val[4] = {d.engine, d.suspension, std::max(d.tyres, d.puncture ? 1.0f : 0.0f), d.body};
    const char* names[4] = {"ENGINE", "SUSPENSION", "TYRES", "BODY"};
    int used = 0;
    for (int k = 0; k < 4; k++) {
        const int row = 8 + k * 3;
        std::string bar;
        const int n = int(std::lround(val[k] * 10));
        for (int i = 0; i < 10; i++) bar += i < n ? '#' : '-';
        hud(3, row, (serviceSel_ == k ? "> " : "  ") + std::string(names[k]), serviceSel_ == k ? PAL_YELLOW : PAL_HUD);
        hud(17, row, bar, val[k] > 0.55f ? PAL_RED : val[k] > 0.12f ? PAL_YELLOW : PAL_HUD);
        hud(29, row, repair_[k] ? "FIX " + std::to_string(REPAIR_MINUTES[k]) + "M" : "-", repair_[k] ? PAL_YELLOW : PAL_HUD);
        if (repair_[k]) used += REPAIR_MINUTES[k];
    }
    hud(3, 21, (serviceSel_ == 4 ? "> " : "  ") + std::string("DONE - TO THE NEXT STAGE"), serviceSel_ == 4 ? PAL_YELLOW : PAL_HUD);
    hud(3, 23, "MINUTES USED " + std::to_string(used) + " OF 15", used > 15 ? PAL_RED : PAL_HUD);
    hud(3, 24, "RALLY TIME " + fmtTime(myTotal_), PAL_HUD);
}

void RallyChamp::drawStandings() {
    if (mode_ == Mode::Podium) {
        text("CHAMPIONSHIP", HALF, 10, 1.6f, PAL_YELLOW);
        struct Row { std::string name; int pts; bool me; };
        std::vector<Row> rows = {{profile_.name.empty() ? "YOU" : profile_.name, myPoints_, true}};
        for (const Crew& c : crews_) rows.push_back({c.name, c.points, false});
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.pts > b.pts; });
        for (size_t k = 0; k < rows.size(); k++) {
            if (k >= 9 && !rows[k].me) continue;  // the top nine, and always our own line
            std::string n = rows[k].name;
            n.resize(14, ' ');
            const int row = k < 9 ? 6 + int(k) * 2 : 24;
            hud(6, row, ordinal(int(k) + 1) + "  " + n + std::to_string(rows[k].pts) + " PTS", rows[k].me ? PAL_YELLOW : PAL_HUD);
        }
        if (!rows.empty() && rows[0].me) text("WORLD CHAMPION!", HALF, 190, 1.4f, frameNo_ % 30 < 20 ? PAL_YELLOW : PAL_RED);
        return;
    }
    const Venue& V = venue(venue_);
    text(V.rally, HALF, 8, 1.1f, PAL_YELLOW);
    hud(12, 4, "FINAL CLASSIFICATION", PAL_HUD);
    struct Row { std::string name; float t; bool me; };
    std::vector<Row> rows = {{profile_.name.empty() ? "YOU" : profile_.name, myTotal_, true}};
    for (const Crew& c : crews_)
        if (!c.out) rows.push_back({c.name, c.total, false});
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.t < b.t; });
    const float best = rows.empty() ? 0 : rows[0].t;
    for (size_t k = 0; k < rows.size(); k++) {
        if (k >= 9 && !rows[k].me) continue;
        std::string n = rows[k].name;
        n.resize(13, ' ');
        const std::string gap = k == 0 ? fmtTime(rows[k].t) : signedTime(rows[k].t - best);
        hud(3, k < 9 ? 6 + int(k) * 2 : 24, (k + 1 < 10 ? " " : "") + std::to_string(k + 1) + ". " + n + gap, rows[k].me ? PAL_YELLOW : PAL_HUD);
    }
    if (game_ == Game::Championship) hud(3, 26, "CHAMPIONSHIP POINTS " + std::to_string(myPoints_), PAL_YELLOW);
}

void RallyChamp::drawProfile() {
    text("DRIVER PROFILE", HALF, 12, 1.5f, PAL_YELLOW);
    switch (profStep_) {
        case 0: {
            hud(11, 7, profFromMenu_ ? "CHANGE YOUR NAME" : "ENTER YOUR NAME", PAL_HUD);
            std::string shown = nameEdit_;
            if (nameEdit_.size() < rally::PROFILE_NAME_MAX && frameNo_ % 40 < 26) shown += sys_->ctl.connected ? pendingChar_ : '_';
            text(shown.empty() ? " " : shown, HALF, 84, 2, PAL_YELLOW);
            hud(9, 17, "UP TO 12 LETTERS OR NUMBERS", PAL_HUD);
            if (sys_->ctl.connected) hud(0, 22, "PAD: UP/DOWN LETTER  RIGHT ADD  LEFT DEL", PAL_HUD);
            hud(4, 24, "TYPE YOUR NAME, THEN PRESS ENTER", PAL_HUD);
            break;
        }
        case 1: {
            const char* lines[] = {"YOU GET A UNIQUE DRIVER ID. IT STAYS", "THE SAME IF YOU CHANGE YOUR NAME.", "",
                                   "THE ID COMES FROM URANDOM.AI. ONLY A", "RANDOM ID IS REQUESTED: YOUR NAME IS",
                                   "NEVER SENT. BOTH ARE SAVED HERE AND", "SHOWN ONLY TO PLAYERS YOU RACE."};
            for (int i = 0; i < 7; i++) hud(2, 7 + i, lines[i], PAL_HUD);
            const char* opts[] = {"AGREE - GET MY ID", "USE AN OFFLINE ID", "BACK"};
            for (int i = 0; i < 3; i++) hud(10, 17 + i * 2, (i == profSel_ ? "> " : "  ") + std::string(opts[i]), i == profSel_ ? PAL_YELLOW : PAL_HUD);
            break;
        }
        case 2:
            hud(10, 12, profSel_ == 0 ? "CONTACTING URANDOM.AI" : "MAKING YOUR ID", frameNo_ % 40 < 26 ? PAL_YELLOW : PAL_HUD);
            break;
        default:
            text("WELCOME", HALF, 50, 1.5f, PAL_HUD);
            text(profile_.name, HALF, 78, 2, PAL_YELLOW);
            hud(10, 15, "DRIVER ID " + profile_.shortId(), PAL_HUD);
            if (t_ > 20 && frameNo_ % 60 < 40) hud(14, 24, "PRESS START", PAL_YELLOW);
            break;
    }
}

}  // namespace rc
