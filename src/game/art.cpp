#include "art.h"

#include <algorithm>
#include <cmath>

using gs::Bitmap;
using gs::Pt;
using gs::rgb4;

namespace rally {

namespace {

constexpr float PI = 3.14159265f;
constexpr float TAU = 6.28318530f;

struct Rng {
    uint32_t a;
    float operator()() {
        a = a * 1664525u + 1013904223u;
        return float(a >> 8) / 16777216.0f;
    }
};

// ------------------------------------------------------------ scenery
// Scene palette roles: 1 outline, 2-3 wood, 4-6 foliage, 7 white/snow, 8-10 rock,
// 11 accent, 12-13 sand, 14 water, 15 highlight.

Bitmap pine(bool snow, Rng& r) {
    Bitmap b(128, 256);
    b.rect(56, 200, 16, 56, 2);
    b.rect(64, 200, 8, 56, 3);
    const int layers = 7;
    for (int k = 0; k < layers; k++) {
        float top = k * 27.0f, bot = top + 60 + k * 3, hw = 16 + k * 7.5f;
        std::vector<Pt> jag;
        const int n = 9;
        for (int i = 0; i <= n; i++) jag.push_back({64 - hw + 2 * hw * i / n, bot - ((i % 2) ? 8.0f : 0.0f) - r() * 3});
        std::vector<Pt> all = {{64, top}};
        for (int i = n; i >= 0; i--) all.push_back(jag[i]);
        b.poly(all, 5);
        std::vector<Pt> left = {{64, top}, {64, bot - 5}};
        for (int i = n / 2; i >= 0; i--) left.push_back(jag[i]);
        b.poly(left, 4);
        b.poly({{66, top + 8}, {64 + hw * 0.6f, bot - 12}, {70, bot - 14}}, 6);
        if (snow) {
            b.poly({{64, top}, {64 + hw * 0.55f, top + (bot - top) * 0.55f}, {64 - hw * 0.5f, top + (bot - top) * 0.6f}}, 7);
            b.poly({{64, top}, {64 - hw * 0.5f, top + (bot - top) * 0.6f}, {64 - hw * 0.2f, top + (bot - top) * 0.5f}}, 12);
            for (int i = 0; i <= n; i += 2) b.ellipse(jag[i].first, jag[i].second - 3, 5, 3, 7);
        }
    }
    return b;
}

Bitmap birch(Rng& r) {
    Bitmap b(144, 256);
    b.poly({{66, 256}, {78, 256}, {76, 90}, {68, 90}}, 7);
    for (int y = 110; y < 250; y += 14 + int(r() * 10)) b.rect(66 + r() * 6, float(y), 5 + r() * 5, 3, 1);
    b.line(72, 150, 44, 110, 7, 4);
    b.line(72, 130, 104, 96, 7, 4);
    struct Blob { float x, y, rx, ry; };
    std::vector<Blob> blobs;
    for (int i = 0; i < 14; i++) blobs.push_back({30 + r() * 84, 20 + r() * 110, 18 + r() * 14, 16 + r() * 12});
    for (auto& o : blobs) b.ellipse(o.x, o.y + 4, o.rx, o.ry, 4);
    for (auto& o : blobs) b.ellipse(o.x + 3, o.y, o.rx * 0.8f, o.ry * 0.8f, 5);
    for (auto& o : blobs) b.ellipse(o.x + 6, o.y - o.ry * 0.3f, o.rx * 0.4f, o.ry * 0.35f, 6);
    return b;
}

Bitmap bush(Rng& r) {
    Bitmap b(144, 80);
    struct Blob { float x, y, rx, ry; };
    Blob blobs[] = {{32, 52, 30, 26}, {72, 40, 36, 36}, {112, 54, 28, 24}, {56, 60, 28, 20}, {92, 62, 28, 18}};
    for (auto& o : blobs) b.ellipse(o.x, o.y + 4, o.rx, o.ry, 4);
    for (auto& o : blobs) b.ellipse(o.x + 4, o.y, o.rx * 0.8f, o.ry * 0.8f, 5);
    for (auto& o : blobs) b.ellipse(o.x + 8, o.y - o.ry * 0.35f, o.rx * 0.35f, o.ry * 0.3f, 6);
    for (int i = 0; i < 6; i++) b.ellipse(20 + r() * 104, 30 + r() * 30, 2.5f, 2.5f, 11);
    return b;
}

Bitmap drybush(Rng& r) {
    Bitmap b(128, 72);
    for (int i = 0; i < 26; i++) {
        float a = PI + r() * PI, len = 20 + r() * 38;
        float x0 = 64 + (r() - 0.5f) * 20;
        b.line(x0, 70, x0 + std::cos(a) * len * 1.3f, 70 + std::sin(a) * len, r() < 0.5f ? 3 : 2, 2);
    }
    for (int i = 0; i < 10; i++) b.ellipse(20 + r() * 88, 20 + r() * 40, 3, 3, 15);
    return b;
}

Bitmap rock(int w, int h, Rng& r) {
    Bitmap b(w, h);
    std::vector<Pt> pts;
    const int n = 11;
    for (int i = 0; i < n; i++) {
        float a = PI + PI * i / (n - 1);
        float rr = 0.8f + r() * 0.2f;
        pts.push_back({w / 2 + std::cos(a) * w * 0.48f * rr, h - 2 + std::sin(a) * (h - 4) * rr});
    }
    b.poly(pts, 9);
    // Lit top-right facets, shadowed lower-left.
    b.poly({{w * 0.5f, h * 0.08f}, {w * 0.9f, h * 0.45f}, {w * 0.62f, h * 0.5f}, {w * 0.42f, h * 0.3f}}, 10);
    b.poly({{w * 0.04f, h * 0.98f}, {w * 0.1f, h * 0.55f}, {w * 0.35f, h * 0.62f}, {w * 0.45f, h * 0.98f}}, 8);
    for (int i = 0; i < 5; i++) {
        float x = w * (0.2f + r() * 0.6f), y = h * (0.3f + r() * 0.5f);
        b.line(x, y, x + w * 0.15f, y + h * 0.1f, 8, 2);
    }
    b.rect(0, float(h - 3), float(w), 3, 8);
    return b;
}

Bitmap cactus() {
    Bitmap b(112, 240);
    auto limb = [&](float x0, float y0, float x1, float y1, float r) {
        b.poly({{x0 - r, y0}, {x0 + r, y0}, {x1 + r, y1}, {x1 - r, y1}}, 5);
        b.ellipse(x1, y1, r, r, 5);
    };
    limb(56, 240, 56, 30, 17);
    limb(22, 120, 22, 70, 11);
    b.rect(22, 118, 34, 18, 5);
    b.ellipse(22, 126, 11, 9, 5);
    limb(92, 150, 92, 90, 11);
    b.rect(56, 146, 36, 18, 5);
    b.ellipse(92, 155, 11, 9, 5);
    // Ribs and shading.
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 112; x++) {
            if (!b.get(x, y)) continue;
            int col = 5;
            int rib = x % 7;
            if (rib == 0) col = 4;
            else if (rib == 3) col = 6;
            b.set(x, y, col);
        }
    b.ellipse(56, 28, 5, 4, 11);
    b.ellipse(22, 62, 4, 3, 11);
    return b;
}

Bitmap palm(Rng& r) {
    Bitmap b(192, 272);
    float x = 100, y = 272;
    for (int i = 0; i < 18; i++) {
        float nx = 100 - std::pow(i / 18.0f, 1.8f) * 34, ny = 272 - i * 12.5f;
        b.poly({{x - 9, y}, {x + 9, y}, {nx + 8, ny}, {nx - 8, ny}}, i % 2 ? 2 : 3);
        x = nx;
        y = ny;
    }
    float cx = x, cy = y;
    for (int f = 0; f < 9; f++) {
        float a = -PI + f * PI / 8 + (r() - 0.5f) * 0.2f;
        float len = 70 + r() * 20;
        std::vector<Pt> top, bot;
        for (int s = 0; s <= 8; s++) {
            float t = s / 8.0f;
            float px = cx + std::cos(a) * len * t;
            float py = cy + std::sin(a) * len * t * 0.55f + t * t * 45;
            float wdt = 11 * std::sin(t * PI);
            top.push_back({px, py - wdt});
            bot.push_back({px, py + wdt * 0.6f});
        }
        std::vector<Pt> poly = top;
        for (int s = 8; s >= 0; s--) poly.push_back(bot[s]);
        b.poly(poly, f % 2 ? 4 : 5);
        for (int s = 1; s < 8; s++) b.line(top[s].first, top[s].second, bot[s].first, bot[s].second + 4, 6);
    }
    b.ellipse(cx - 6, cy + 8, 7, 7, 2);
    b.ellipse(cx + 6, cy + 10, 7, 7, 3);
    return b;
}

Bitmap reeds(Rng& r) {
    Bitmap b(128, 72);
    for (int i = 0; i < 30; i++) {
        float x = 4 + r() * 120, h = 30 + r() * 40;
        b.line(x, 72, x + (r() - 0.5f) * 12, 72 - h, r() < 0.5f ? 4 : 5, 2);
        if (r() < 0.3f) b.rect(x - 2, 72 - h, 4, 10, 2);
    }
    return b;
}

Bitmap snowbank(Rng& r) {
    Bitmap b(256, 72);
    for (int i = 0; i < 9; i++) b.ellipse(20 + i * 27 + r() * 8, 58 - r() * 12, 30, 28, 12);
    for (int i = 0; i < 9; i++) b.ellipse(24 + i * 27 + r() * 8, 52 - r() * 12, 26, 22, 7);
    b.rect(0, 64, 256, 8, 12);
    return b;
}

Bitmap cliff(Rng& r) {
    Bitmap b(256, 320);
    std::vector<Pt> pts = {{0, 320}};
    for (int i = 0; i <= 16; i++) pts.push_back({i * 16.0f, 10 + r() * 60 + (i % 4 == 0 ? 20 : 0)});
    pts.push_back({256, 320});
    b.poly(pts, 9);
    for (int s = 0; s < 14; s++) {
        float y = 60 + s * 19 + r() * 6;
        b.line(0, y, 256, y + (r() - 0.5f) * 12, 8, 2 + r() * 2);
    }
    for (int i = 0; i < 12; i++) {
        float x = r() * 240, y = 40 + r() * 240;
        b.poly({{x, y}, {x + 18, y + 4}, {x + 12, y + 30}}, 10);
        b.poly({{x + 12, y + 30}, {x + 18, y + 4}, {x + 22, y + 30}}, 8);
    }
    for (int x = 0; x < 256; x++)
        for (int y = 0; y < 320; y++)
            if (b.get(x, y) && !b.get(x, y - 3)) b.set(x, y, 10);
    return b;
}

Bitmap logs() {
    Bitmap b(176, 72);
    for (int row = 0; row < 3; row++)
        for (int i = 0; i < 4 - row; i++) {
            float cx = 26 + i * 40 + row * 20, cy = 54 - row * 22;
            b.ellipse(cx, cy, 20, 18, 2);
            b.ellipse(cx, cy, 15, 13, 3);
            b.ellipse(cx, cy, 7, 6, 13);
            b.ellipse(cx, cy, 2, 2, 2);
        }
    return b;
}

// ------------------------------------------------------------ common objects
// Common palette roles: 1 black, 2 white, 3 red, 4 yellow, 5 blue, 6 green, 7 orange,
// 8-9 greys, 10 skin, 11 brown, 12 purple, 13 cyan, 14 silver, 15 tyre.

Bitmap chevron() {
    Bitmap b(160, 128);
    b.rect(28, 64, 10, 64, 8);
    b.rect(122, 64, 10, 64, 8);
    b.rect(28, 64, 4, 64, 9);
    b.rect(122, 64, 4, 64, 9);
    b.rect(4, 6, 152, 68, 1);
    b.rect(10, 12, 140, 56, 2);
    for (int i = 0; i < 3; i++) {
        float x = 20 + i * 42;
        b.poly({{x, 16}, {x + 18, 16}, {x + 40, 40}, {x + 18, 64}, {x, 64}, {x + 22, 40}}, 3);
    }
    return b;
}

Bitmap crowd(Rng& r) {
    Bitmap b(256, 128);
    const int shirts[] = {3, 5, 4, 6, 7, 12, 13, 2};
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 12; i++) {
            float x = 8 + i * 20 + r() * 8 + row * 10, base = 128 - row * 14;
            float h = 60 + r() * 26;
            int shirt = shirts[int(r() * 8)];
            b.rect(x, base - h * 0.45f, 16, h * 0.45f, 8);  // legs
            b.rect(x - 1, base - h * 0.85f, 18, h * 0.45f, shirt);
            b.ellipse(x + 8, base - h * 0.92f, 8, 9, 10);
            b.rect(x + 1, base - h * 1.02f, 14, 5, r() < 0.5f ? 11 : 1);
            if (r() < 0.45f) {  // waving arm, sometimes with a flag
                float ax = x + (r() < 0.5f ? -6 : 18);
                b.rect(ax, base - h * 1.15f, 5, h * 0.35f, 10);
                if (r() < 0.5f) b.rect(ax, base - h * 1.4f, 20, 12, shirts[int(r() * 8)]);
            }
        }
    return b;
}

Bitmap flag() {
    Bitmap b(80, 256);
    b.rect(8, 0, 7, 256, 14);
    b.rect(8, 0, 3, 256, 2);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 4; x++) {
            float wave = std::sin((x + y * 0.3f) * 1.2f) * 4;
            b.rect(15 + x * 15, 6 + y * 12 + wave, 15, 12, (x + y) % 2 ? 1 : 2);
        }
    return b;
}

Bitmap arch(const char* text) {
    Bitmap b(1024, 640);
    b.rect(0, 120, 40, 520, 14);
    b.rect(984, 120, 40, 520, 14);
    b.rect(10, 120, 12, 520, 2);
    b.rect(994, 120, 12, 520, 2);
    b.rect(0, 40, 1024, 110, 1);
    b.rect(6, 46, 1012, 98, 3);
    b.rect(6, 118, 1012, 12, 4);
    for (int x = 6; x < 1012; x += 40) b.rect(float(x), 130, 20, 8, 2);
    Bitmap t = gs::textBitmap(text, {10, 2, 1, 0, 1});
    b.blit(t, 512 - t.w / 2, 50);
    return b;
}

Bitmap tires() {
    Bitmap b(160, 96);
    for (int row = 0; row < 3; row++)
        for (int i = 0; i < 4; i++) {
            float cx = 24 + i * 38 + (row % 2) * 16, cy = 84 - row * 24;
            b.ellipse(cx, cy, 20, 14, 15);
            b.ellipse(cx, cy - 2, 12, 7, 1);
            b.rect(cx - 20, cy - 3, 40, 3, (i + row) % 2 ? 3 : 2);
        }
    return b;
}

Bitmap bale() {
    Bitmap b(144, 96);
    b.rect(4, 14, 136, 82, 4);
    b.rect(4, 14, 136, 12, 2);
    for (int y = 30; y < 96; y += 8) b.line(4, float(y), 140, float(y + 3), 7, 2);
    b.rect(40, 14, 6, 82, 11);
    b.rect(98, 14, 6, 82, 11);
    return b;
}

Bitmap boat() {
    Bitmap b(160, 144);
    b.poly({{10, 112}, {150, 112}, {130, 136}, {30, 136}}, 2);
    b.rect(10, 112, 140, 6, 3);
    b.rect(78, 10, 4, 104, 8);
    b.poly({{82, 12}, {82, 106}, {140, 106}}, 2);
    b.poly({{76, 20}, {76, 106}, {28, 106}}, 4);
    b.rect(0, 136, 160, 8, 13);
    return b;
}

// ------------------------------------------------------------ cars
// Car palette roles: 1 black, 2 dark grey, 3 body, 4 body shade, 5-6 stripes,
// 7-8 glass, 9-10 tail lights, 11 metal, 12 tread, 13 body highlight, 14 white, 15 decal.

Bitmap car(float turn) {
    const float H = 112;
    Bitmap b(192, 112);
    const float k = turn * 9;
    auto sx = [&](float x, float y) { return x + k * (1 - y / H) * 1.2f - k * 0.3f; };
    auto P = [&](std::initializer_list<Pt> pts) {
        std::vector<Pt> v;
        for (auto& p : pts) v.push_back({sx(p.first, p.second), p.second});
        return v;
    };
    if (turn > 0) {  // left flank visible while yawing right (kept inside the bitmap)
        const float kf = std::min(k, 3.3f);
        b.poly(P({{10, 58}, {10 - kf * 2.2f, 50}, {16 - kf * 2.2f, 36}, {26, 36}, {24, 58}}), 4);
        b.poly(P({{14 - kf * 2, 64}, {14, 64}, {14, 96}, {14 - kf * 2, 94}}), 4);
        b.rect(8 - kf * 2.4f, 64, 12 + kf * 0.6f, 34, 1);
        b.poly(P({{20 - kf * 2, 36}, {42, 36}, {58, 14}, {40 - kf * 1.2f, 16}}), 7);
    }
    b.rect(10, 70, 32, 38, 1);
    b.rect(150, 70, 32, 38, 1);
    for (int y = 74; y < 106; y += 6) {
        b.rect(12, float(y), 28, 2, 12);
        b.rect(152, float(y), 28, 2, 12);
    }
    b.poly(P({{8, 56}, {184, 56}, {180, 92}, {12, 92}}), 3);
    b.poly(P({{122, 56}, {184, 56}, {180, 92}, {142, 92}}), 4);
    b.poly(P({{8, 58}, {184, 58}, {184, 62}, {8, 62}}), 5);
    b.poly(P({{8, 63}, {184, 63}, {183, 66}, {9, 66}}), 6);
    b.poly(P({{20, 57}, {172, 57}, {158, 34}, {34, 34}}), 3);
    b.poly(P({{112, 57}, {172, 57}, {158, 34}, {122, 34}}), 4);
    b.poly(P({{30, 52}, {100, 52}, {96, 38}, {38, 38}}), 13);
    b.poly(P({{40, 50}, {152, 50}, {150, 46}, {42, 46}}), 5);
    // Rear glass, roof, roof vent
    b.poly(P({{42, 36}, {150, 36}, {134, 12}, {58, 12}}), 7);
    b.poly(P({{62, 32}, {80, 32}, {98, 14}, {84, 14}}), 8);
    b.poly(P({{58, 12}, {134, 12}, {128, 5}, {64, 5}}), 3);
    b.poly(P({{70, 8}, {122, 8}, {120, 5}, {72, 5}}), 6);
    b.poly(P({{86, 4}, {106, 4}, {104, 1}, {88, 1}}), 2);
    // Wing
    b.poly(P({{22, 29}, {170, 29}, {170, 36}, {22, 36}}), 1);
    b.poly(P({{24, 30}, {168, 30}, {168, 32}, {24, 32}}), 6);
    b.poly(P({{40, 36}, {46, 36}, {46, 42}, {40, 42}}), 1);
    b.poly(P({{146, 36}, {152, 36}, {152, 42}, {146, 42}}), 1);
    // Lights, plate, bumper, number decal
    b.poly(P({{16, 68}, {52, 68}, {52, 78}, {18, 78}}), 9);
    b.poly(P({{140, 68}, {176, 68}, {174, 78}, {140, 78}}), 9);
    b.poly(P({{20, 70}, {34, 70}, {34, 76}, {21, 76}}), 10);
    b.poly(P({{158, 70}, {172, 70}, {171, 76}, {158, 76}}), 10);
    b.poly(P({{78, 70}, {114, 70}, {114, 82}, {78, 82}}), 14);
    b.poly(P({{80, 72}, {112, 72}, {112, 80}, {80, 80}}), 1);
    b.poly(P({{82, 74}, {110, 74}, {110, 78}, {82, 78}}), 15);
    b.poly(P({{12, 86}, {180, 86}, {178, 96}, {14, 96}}), 2);
    b.poly(P({{56, 40}, {64, 40}, {64, 48}, {56, 48}}), 15);
    b.rect(18, 94, 16, 12, 6);
    b.rect(158, 94, 16, 12, 6);
    b.ellipse(sx(146, 94), 94, 5, 4, 11);
    b.ellipse(sx(146, 94), 94, 2.5f, 2, 1);
    return b;
}

// ------------------------------------------------------------ HUD pieces

Bitmap bigGlyph(char ch) {
    Bitmap t = gs::textBitmap(std::string(1, ch), {2, 1, 15, 0, 1});
    // Arcade gradient: pale top, full middle, deep bottom.
    for (int y = 0; y < t.h; y++)
        for (int x = 0; x < t.w; x++) {
            uint8_t& p = t.px[size_t(y) * t.w + x];
            if (p == 1) p = y < t.h * 0.35f ? 3 : (y < t.h * 0.65f ? 1 : 2);
        }
    return t;
}

Bitmap paceIcon(int kind) {
    Bitmap b(72, 56);
    if (kind <= 3) {
        // Curved arrow to the right; tighter for harder corners.
        float bend = 0.35f + kind * 0.35f;
        std::vector<Pt> spine;
        for (int i = 0; i <= 12; i++) {
            float t = i / 12.0f;
            float a = t * bend * PI;
            spine.push_back({18 + std::sin(a) * 26 * (kind == 3 ? 0.9f : 1.2f), 50 - (1 - std::cos(a)) * 0 - t * 34 + (kind == 3 ? t * t * 20 : 0)});
        }
        for (size_t i = 0; i + 1 < spine.size(); i++)
            b.line(spine[i].first, spine[i].second, spine[i + 1].first, spine[i + 1].second, 1, 9);
        Pt end = spine.back(), prev = spine[spine.size() - 3];
        float dx = end.first - prev.first, dy = end.second - prev.second, len = std::sqrt(dx * dx + dy * dy);
        dx /= len; dy /= len;
        b.poly({{end.first + dx * 12, end.second + dy * 12}, {end.first - dy * 12, end.second + dx * 12},
                {end.first + dy * 12, end.second - dx * 12}}, 1);
    } else if (kind == 4) {
        b.poly({{4, 50}, {36, 10}, {68, 50}, {56, 50}, {36, 26}, {16, 50}}, 1);
    } else {
        b.poly({{36, 4}, {56, 34}, {16, 34}}, 1);
        b.ellipse(36, 38, 20, 16, 1);
        b.ellipse(30, 38, 5, 6, 3);
    }
    for (int y = 0; y < b.h; y++)
        for (int x = 0; x < b.w; x++)
            if (b.get(x, y) == 1 && y > b.h * 0.6f) b.set(x, y, 2);
    b.outline(15, true);
    return b;
}

Bitmap logoBitmap() {
    Bitmap b(320, 104);
    Bitmap l1 = gs::textBitmap("S3", {3, 5, 15, 0, 1});
    Bitmap l2 = gs::textBitmap("RALLY", {7, 1, 15, 0, 1});
    b.blit(l1, 160 - l1.w / 2, 0);
    b.blit(l2, 160 - l2.w / 2, 30);
    for (int y = 30; y < 104; y++)
        for (int x = 0; x < 320; x++) {
            uint8_t& p = b.px[size_t(y) * 320 + x];
            if (p == 1) p = y < 46 ? 1 : y < 62 ? 2 : y < 76 ? 3 : 4;
        }
    for (int i = 0; i < 5; i++) {
        int y = 38 + i * 12;
        for (int x = 4 + i * 8; x < 60 - i * 4; x++)
            if (!b.get(x, y)) { b.set(x, y, 3); b.set(x, y + 1, 4); b.set(x, y + 2, 4); }
        for (int x = 260 + i * 4; x < 316 - i * 8; x++)
            if (!b.get(x, y)) { b.set(x, y, 3); b.set(x, y + 1, 4); b.set(x, y + 2, 4); }
    }
    return b;
}

// ------------------------------------------------------------ backdrops
// Far palette: 1-2 cloud, 3-4 mountain, 5-6 snow, 7 far hill, 8-9 sun, 10 haze, 11-12 extra.
// Near palette: 1-2 hills, 3-5 trees, 6-7 water, 8-9 dunes/snow, 10 rock, 11 white, 12 dark.

void clouds(Bitmap& b, Rng& r, int count, float yMin, float yMax) {
    const int W = b.w;
    for (int i = 0; i < count; i++) {
        float cx = r() * W, cy = yMin + r() * (yMax - yMin), s = 0.6f + r() * 0.9f;
        struct P { float x, y, rx, ry; };
        std::vector<P> puffs;
        for (int k = 0; k < 7; k++)
            puffs.push_back({cx + (r() - 0.5f) * 80 * s, cy + (r() - 0.5f) * 10 * s, (14 + r() * 18) * s, (7 + r() * 6) * s});
        for (int o : {-W, 0, W}) for (auto& p : puffs) b.ellipse(p.x + o, p.y + 3, p.rx, p.ry, 2);
        for (int o : {-W, 0, W}) for (auto& p : puffs) b.ellipse(p.x + o, p.y, p.rx * 0.95f, p.ry * 0.8f, 1);
    }
}

void mountains(Bitmap& b, Rng& r, float base, float amp, float snowLine, bool jagged) {
    const int W = b.w;
    float ph[5];
    for (float& p : ph) p = r() * TAU;
    auto ridge = [&](float x) {
        float v = base - amp * (0.55f * std::sin(TAU * x / W + ph[0]) + 0.3f * std::sin(TAU * 3 * x / W + ph[1]) +
                                0.15f * std::sin(TAU * 7 * x / W + ph[2]) + (jagged ? 0.1f : 0.04f) * std::sin(TAU * 17 * x / W + ph[3]) +
                                (jagged ? 0.06f : 0.0f) * std::sin(TAU * 41 * x / W + ph[4]));
        return v;
    };
    for (int x = 0; x < W; x++) {
        float top = ridge(float(x)), slope = ridge(x + 1.0f) - ridge(x - 1.0f);
        bool lit = slope < 0.2f;
        for (int y = std::max(0, int(top)); y < b.h; y++) {
            float sl = snowLine + 4 * std::sin(x * 0.9f) + 3 * std::sin(x * 0.37f);
            int c = lit ? 3 : 4;
            if (y < sl && y < top + 30) c = lit ? 5 : 6;
            b.set(x, y, c);
        }
    }
}

void mesas(Bitmap& b, Rng& r) {
    const int W = b.w;
    for (int i = 0; i < 9; i++) {
        float cx = r() * W, w = 30 + r() * 70, top = 170 + r() * 45, h2 = w * (0.1f + r() * 0.1f);
        for (int o : {-W, 0, W}) {
            b.poly({{cx + o - w, float(b.h)}, {cx + o - w * 0.7f, top + h2}, {cx + o - w * 0.55f, top},
                    {cx + o + w * 0.55f, top}, {cx + o + w * 0.7f, top + h2}, {cx + o + w, float(b.h)}}, 3);
            b.poly({{cx + o + w * 0.2f, top}, {cx + o + w * 0.55f, top}, {cx + o + w * 0.7f, top + h2}, {cx + o + w, float(b.h)},
                    {cx + o + w * 0.3f, float(b.h)}}, 4);
            for (int s = 0; s < 3; s++) {
                float y = top + 8 + s * 10;
                b.line(cx + o - w * 0.55f, y, cx + o + w * 0.2f, y, 11, 1);
            }
        }
    }
}

void treeline(Bitmap& b, Rng& r, float base, float minH, float maxH, int c1, int c2, int fill) {
    const int W = b.w;
    for (float x = -8; x < W + 8; x += 3 + r() * 5) {
        float h = minH + r() * (maxH - minH), hw = 3 + r() * 3;
        int c = r() < 0.5f ? c1 : c2;
        for (int o : {-W, 0, W}) b.poly({{x + o, base - h}, {x + o + hw, base}, {x + o - hw, base}}, c);
    }
    b.rect(0, base, float(W), float(b.h) - base, fill);
}

void hills(Bitmap& b, Rng& r, float base, float amp, int c1, int c2) {
    const int W = b.w;
    float p0 = r() * TAU, p1 = r() * TAU;
    for (int x = 0; x < W; x++) {
        float top = base - amp * (0.6f * std::sin(TAU * 2 * x / W + p0) + 0.4f * std::sin(TAU * 5 * x / W + p1));
        float slope = std::cos(TAU * 2 * x / W + p0);
        for (int y = int(top); y < b.h; y++) b.set(x, y, slope > 0 ? c1 : c2);
    }
}

void backdropFar(Bitmap& b, int stage, Rng& r) {
    switch (stage) {
        case 0:
            clouds(b, r, 4, 30, 90);
            b.rect(0, 226, float(b.w), 30, 10);
            mesas(b, r);
            break;
        case 1:
            clouds(b, r, 7, 28, 100);
            mountains(b, r, 214, 42, 196, false);
            hills(b, r, 236, 7, 7, 7);
            break;
        case 2:
            clouds(b, r, 5, 24, 80);
            mountains(b, r, 222, 70, 205, true);
            break;
        default: {
            // Sunset: a big sun sinking behind purple ridges.
            for (int o : {0, b.w}) {
                b.ellipse(360.0f - o, 204, 44, 44, 9);
                b.ellipse(360.0f - o, 204, 34, 34, 8);
            }
            clouds(b, r, 6, 40, 150);
            mountains(b, r, 228, 26, 0, false);
            break;
        }
    }
}

void backdropNear(Bitmap& b, int stage, Rng& r) {
    switch (stage) {
        case 0: {
            hills(b, r, 238, 7, 8, 9);
            for (int i = 0; i < 90; i++) {
                float x = r() * b.w, y = 236 + r() * 12, rx = 2 + r() * 3;
                int c = r() < 0.5f ? 3 : 4;
                for (int o : {-b.w, 0, b.w}) b.ellipse(x + o, y, rx, 1.5f, c);  // wrap so the plane tiles
            }
            break;
        }
        case 1:
            treeline(b, r, 244, 10, 30, 3, 4, 3);
            break;
        case 2:
            hills(b, r, 236, 10, 8, 9);
            treeline(b, r, 246, 8, 24, 3, 4, 12);
            break;
        default: {
            // Lake: far shore trees then water with the sun's reflection.
            treeline(b, r, 234, 5, 14, 3, 4, 3);
            b.rect(0, 238, float(b.w), float(b.h) - 238, 6);
            for (int y = 239; y < b.h; y += 2)
                for (int k = 0; k < 3; k++) {
                    float w = 8 + (y - 238) * 3 + r() * 10;
                    float cx = 360 + (r() - 0.5f) * 30;
                    for (int o : {0, b.w}) b.rect(cx - o - w / 2, float(y), w, 1, 7);
                }
            break;
        }
    }
}

}  // namespace

// ------------------------------------------------------------ cars / palettes

const CarSpec& carSpec(int i) {
    static const CarSpec specs[NUM_CARS] = {
        {"GS-C  CELICA 4WD", "BALANCED  GRIP", 1.0f, 1.0f, 1.08f,
         {0, rgb4(1, 1, 1), rgb4(4, 4, 4), rgb4(15, 15, 15), rgb4(11, 12, 13), rgb4(2, 4, 13), rgb4(14, 2, 2), rgb4(1, 2, 4),
          rgb4(6, 8, 11), rgb4(10, 0, 0), rgb4(15, 6, 4), rgb4(9, 9, 9), rgb4(6, 6, 6), rgb4(15, 15, 15), rgb4(15, 15, 15), rgb4(15, 12, 0)}},
        {"GS-D  DELTA INTEGRALE", "POWER  DRIFT", 1.12f, 1.02f, 0.94f,
         {0, rgb4(1, 1, 1), rgb4(4, 4, 4), rgb4(14, 2, 2), rgb4(10, 1, 1), rgb4(15, 15, 15), rgb4(15, 12, 0), rgb4(1, 2, 4),
          rgb4(6, 8, 11), rgb4(10, 0, 0), rgb4(15, 6, 4), rgb4(9, 9, 9), rgb4(6, 6, 6), rgb4(15, 7, 6), rgb4(15, 15, 15), rgb4(2, 4, 13)}},
    };
    return specs[std::clamp(i, 0, NUM_CARS - 1)];
}

void setCarPalette(gs::VDP& vdp, int pal, int car) {
    const CarSpec& c = carSpec(car);
    for (int i = 0; i < 16; i++) vdp.setColor(pal * 16 + i, c.livery[i]);
}

void setRivalPalettes(gs::VDP& vdp) {
    // body, shade, stripe1, stripe2, highlight, decal
    const uint16_t liv[NUM_RIVAL_PALS][6] = {
        {rgb4(15, 13, 0), rgb4(11, 9, 0), rgb4(0, 8, 3), rgb4(1, 1, 1), rgb4(15, 15, 8), rgb4(14, 2, 2)},
        {rgb4(2, 4, 12), rgb4(1, 2, 8), rgb4(15, 12, 0), rgb4(15, 15, 15), rgb4(5, 8, 15), rgb4(15, 12, 0)},
        {rgb4(3, 3, 3), rgb4(1, 1, 1), rgb4(15, 7, 0), rgb4(15, 15, 15), rgb4(7, 7, 7), rgb4(15, 7, 0)},
    };
    for (int p = 0; p < NUM_RIVAL_PALS; p++) {
        int base = (PAL_RIVAL + p) * 16;
        setCarPalette(vdp, PAL_RIVAL + p, 0);
        vdp.setColor(base + 3, liv[p][0]);
        vdp.setColor(base + 4, liv[p][1]);
        vdp.setColor(base + 5, liv[p][2]);
        vdp.setColor(base + 6, liv[p][3]);
        vdp.setColor(base + 13, liv[p][4]);
        vdp.setColor(base + 15, liv[p][5]);
    }
}

static void loadFixedPalettes(gs::VDP& vdp) {
    auto set = [&](int pal, std::initializer_list<uint16_t> cs) {
        int i = 0;
        for (uint16_t c : cs) vdp.setColor(pal * 16 + i++, c);
    };
    set(PAL_HUD, {0, rgb4(15, 15, 15), rgb4(11, 11, 12), rgb4(15, 15, 15), rgb4(15, 3, 2), rgb4(2, 13, 3), rgb4(15, 8, 0),
                  rgb4(3, 6, 15), rgb4(4, 4, 5), rgb4(3, 13, 14), rgb4(8, 8, 9), rgb4(0, 0, 0), rgb4(0, 0, 0), rgb4(0, 0, 0),
                  rgb4(0, 0, 0), rgb4(0, 0, 1)});
    set(PAL_YELLOW, {0, rgb4(15, 13, 0), rgb4(15, 7, 0), rgb4(15, 15, 10), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, rgb4(0, 0, 1)});
    set(PAL_RED, {0, rgb4(15, 3, 2), rgb4(10, 1, 1), rgb4(15, 11, 10), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, rgb4(0, 0, 1)});
    set(PAL_COMMON, {0, rgb4(1, 1, 1), rgb4(15, 15, 15), rgb4(14, 2, 2), rgb4(15, 13, 2), rgb4(2, 4, 13), rgb4(2, 10, 3),
                     rgb4(15, 7, 0), rgb4(4, 4, 5), rgb4(9, 9, 10), rgb4(14, 11, 8), rgb4(7, 4, 2), rgb4(9, 3, 11),
                     rgb4(3, 12, 13), rgb4(12, 12, 13), rgb4(3, 3, 3)});
    set(PAL_LOGO, {0, rgb4(15, 15, 10), rgb4(15, 13, 0), rgb4(15, 7, 0), rgb4(14, 2, 1), rgb4(15, 15, 15), 0, 0, 0, 0, 0, 0, 0,
                   0, 0, rgb4(0, 0, 1)});
    setRivalPalettes(vdp);
}

void buildArt(gs::VDP& vdp, Art& a) {
    Rng r{1234};
    loadFixedPalettes(vdp);

    // HUD font tiles.
    gs::TileAlloc tiles(vdp);
    for (int c = 32; c < 128; c++) {
        uint8_t px[64] = {};
        const uint8_t* g = gs::glyph(char(c));
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (g[y * 5 + x] && px[(y + 1) * 8 + x + 2] == 0) px[(y + 1) * 8 + x + 2] = 15;
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (g[y * 5 + x]) px[y * 8 + x + 1] = 1;
        int t = tiles.alloc(1);
        vdp.loadTile(t, px);
        a.fontTile[c - 32] = t;
    }
    a.firstFreeTile = tiles.used();

    // Sprite ROM.
    a.obj[O_PINE] = gs::uploadMipped(vdp, pine(false, r));
    a.obj[O_PINE_SNOW] = gs::uploadMipped(vdp, pine(true, r));
    a.obj[O_BIRCH] = gs::uploadMipped(vdp, birch(r));
    a.obj[O_BUSH] = gs::uploadMipped(vdp, bush(r));
    a.obj[O_DRYBUSH] = gs::uploadMipped(vdp, drybush(r));
    a.obj[O_ROCK] = gs::uploadMipped(vdp, rock(144, 96, r));
    a.obj[O_BOULDER] = gs::uploadMipped(vdp, rock(256, 208, r));
    a.obj[O_CACTUS] = gs::uploadMipped(vdp, cactus());
    a.obj[O_PALM] = gs::uploadMipped(vdp, palm(r));
    a.obj[O_REEDS] = gs::uploadMipped(vdp, reeds(r));
    a.obj[O_SNOWBANK] = gs::uploadMipped(vdp, snowbank(r));
    a.obj[O_CLIFF] = gs::uploadMipped(vdp, cliff(r));
    a.obj[O_LOGS] = gs::uploadMipped(vdp, logs());
    a.obj[O_CHEVRON] = gs::uploadMipped(vdp, chevron());
    a.obj[O_CROWD] = gs::uploadMipped(vdp, crowd(r));
    a.obj[O_FLAG] = gs::uploadMipped(vdp, flag());
    a.obj[O_ARCH_START] = gs::uploadMipped(vdp, arch("S3 RALLY"));
    a.obj[O_ARCH_CP] = gs::uploadMipped(vdp, arch("CHECKPOINT"));
    a.obj[O_TIRES] = gs::uploadMipped(vdp, tires());
    a.obj[O_BALE] = gs::uploadMipped(vdp, bale());
    a.obj[O_BOAT] = gs::uploadMipped(vdp, boat());
    for (int i = 0; i < 5; i++) a.car[i] = gs::uploadMipped(vdp, car(i * 0.5f));

    Bitmap sh(128, 32);
    sh.ellipse(64, 16, 63, 15, 1);
    a.shadow = gs::uploadMipped(vdp, sh);
    Bitmap puff(64, 64);
    puff.ellipse(34, 34, 28, 24, 2);
    puff.ellipse(29, 29, 24, 20, 1);
    for (int y = 0; y < 64; y++)  // dithered edge so puffs read as soft
        for (int x = 0; x < 64; x++)
            if (puff.get(x, y) && ((x + y) & 1) && (std::hypot(x - 32.0f, y - 32.0f) > 22)) puff.set(x, y, 0);
    a.puff = gs::uploadMipped(vdp, puff);
    Bitmap flake(4, 4);
    flake.rect(1, 0, 2, 4, 3);
    flake.rect(0, 1, 4, 2, 3);
    a.flake = gs::uploadMipped(vdp, flake);
    Bitmap splash(96, 64);
    for (int i = 0; i < 40; i++) {
        float ang = PI + r() * PI, d = 10 + r() * 36;
        splash.ellipse(48 + std::cos(ang) * d * 1.2f, 60 + std::sin(ang) * d, 2 + r() * 3, 3 + r() * 3, r() < 0.5f ? 5 : 6);
    }
    a.splash = gs::uploadMipped(vdp, splash);
    Bitmap spray(64, 48);
    for (int i = 0; i < 30; i++) spray.ellipse(8 + r() * 48, 8 + r() * 36, 1.5f + r() * 2, 1.5f + r() * 2, r() < 0.6f ? 2 : 8);
    a.spray = gs::uploadMipped(vdp, spray);

    for (int c = 32; c < 128; c++) a.glyph[c - 32] = gs::uploadMipped(vdp, bigGlyph(char(c)));
    for (int i = 0; i < 6; i++) a.pace[i] = gs::uploadMipped(vdp, paceIcon(i));
    a.logo = gs::uploadMipped(vdp, logoBitmap());
    const int rpmCol[4] = {5, 3, 4, 8};
    for (int i = 0; i < 4; i++) {
        Bitmap seg(6, 12);
        seg.rect(0, 0, 6, 12, rpmCol[i]);
        seg.rect(0, 0, 6, 2, i == 3 ? 10 : 1);
        a.rpm[i] = gs::uploadMipped(vdp, seg);
    }
    Bitmap panel(8, 8);
    panel.rect(0, 0, 8, 8, 15);
    a.panel = gs::uploadMipped(vdp, panel);
    Bitmap wide(32, 8);
    wide.rect(0, 0, 32, 8, 15);
    a.panelWide = gs::uploadMipped(vdp, wide);
    // Turbo: an exhaust flame burst (yellow palette: 3 pale core, 1 yellow, 2 orange) and a speed streak.
    Bitmap flame(48, 48);
    for (int i = 0; i < 12; i++) {
        float ang = i * TAU / 12, len = (i % 2) ? 14.0f : 22.0f;
        flame.poly({{24 + std::cos(ang - 0.25f) * 6, 24 + std::sin(ang - 0.25f) * 6}, {24 + std::cos(ang) * len, 24 + std::sin(ang) * len},
                    {24 + std::cos(ang + 0.25f) * 6, 24 + std::sin(ang + 0.25f) * 6}}, 2);
    }
    flame.ellipse(24, 24, 12, 12, 1);
    flame.ellipse(24, 24, 6, 6, 3);
    a.flame = gs::uploadMipped(vdp, flame);
    Bitmap streak(2, 32);
    streak.rect(0, 0, 2, 32, 1);
    a.streak = gs::uploadMipped(vdp, streak);
    a.map = vdp.allocImage(72, 72);
}

int loadStage(gs::VDP& vdp, const Art& a, int stage) {
    const StageDef& s = stageDef(stage);
    for (int i = 0; i < 16; i++) {
        vdp.setColor(PAL_ROAD * 16 + i, s.roadMain[i]);
        vdp.setColor(PAL_TARMAC * 16 + i, s.roadTarmac[i]);
        vdp.setColor(PAL_ALT * 16 + i, s.roadAlt[i]);
        vdp.setColor(PAL_SCENE * 16 + i, s.scene[i]);
        vdp.setColor(PAL_FAR * 16 + i, s.far[i]);
        vdp.setColor(PAL_NEAR * 16 + i, s.near[i]);
    }
    // Effects palette: dust tinted to the stage's ground.
    const uint16_t dust[NUM_STAGES][2] = {
        {rgb4(15, 12, 8), rgb4(12, 9, 6)}, {rgb4(12, 10, 7), rgb4(9, 7, 5)}, {rgb4(15, 15, 15), rgb4(12, 13, 15)}, {rgb4(13, 11, 8), rgb4(10, 8, 6)}};
    const uint16_t fx[16] = {0, dust[stage][0], dust[stage][1], rgb4(15, 15, 15), rgb4(12, 13, 15), rgb4(10, 13, 15),
                             rgb4(5, 8, 13), rgb4(15, 13, 5), rgb4(6, 6, 6), 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 16; i++) vdp.setColor(PAL_FX * 16 + i, fx[i]);
    vdp.setFogColor(s.fog);

    // Backdrop planes (tiles rebuilt per stage).
    gs::TileAlloc tiles(vdp, a.firstFreeTile);
    Rng r{uint32_t(77 + stage * 31)};
    Bitmap far(512, 256), near(1024, 256);
    backdropFar(far, stage, r);
    backdropNear(near, stage, r);
    vdp.B.resize(64, 32);
    vdp.A.resize(128, 32);
    gs::bitmapToPlane(tiles, vdp.B, 0, 0, far, PAL_FAR);
    gs::bitmapToPlane(tiles, vdp.A, 0, 0, near, PAL_NEAR);
    return tiles.used();
}

}  // namespace rally
