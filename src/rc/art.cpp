#include "art.h"

#include <algorithm>
#include <cmath>

using gs::Bitmap;
using gs::Pt;
using gs::rgb4;

namespace rc {

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
// 11 accent, 12-13 sand/earth, 14 water, 15 highlight.

Bitmap pine(bool snow, Rng& r) {
    Bitmap b(128, 256);
    b.rect(58, 200, 12, 56, 2);
    b.rect(64, 200, 6, 56, 3);
    const int layers = 8;
    for (int k = 0; k < layers; k++) {
        float top = k * 24.0f, bot = top + 56 + k * 3, hw = 12 + k * 6.8f;
        std::vector<Pt> jag;
        const int n = 9;
        for (int i = 0; i <= n; i++) jag.push_back({64 - hw + 2 * hw * i / n, bot - ((i % 2) ? 8.0f : 0.0f) - r() * 3});
        std::vector<Pt> all = {{64, top}};
        for (int i = n; i >= 0; i--) all.push_back(jag[size_t(i)]);
        b.poly(all, 5);
        std::vector<Pt> left = {{64, top}, {64, bot - 5}};
        for (int i = n / 2; i >= 0; i--) left.push_back(jag[size_t(i)]);
        b.poly(left, 4);
        b.poly({{66, top + 8}, {64 + hw * 0.6f, bot - 12}, {70, bot - 14}}, 6);
        if (snow) {
            b.poly({{64, top}, {64 + hw * 0.55f, top + (bot - top) * 0.55f}, {64 - hw * 0.5f, top + (bot - top) * 0.6f}}, 7);
            b.poly({{64, top}, {64 - hw * 0.5f, top + (bot - top) * 0.6f}, {64 - hw * 0.2f, top + (bot - top) * 0.5f}}, 12);
            for (int i = 0; i <= n; i += 2) b.ellipse(jag[size_t(i)].first, jag[size_t(i)].second - 3, 5, 3, 7);
        }
    }
    return b;
}

Bitmap birch(Rng& r) {
    Bitmap b(144, 256);
    b.poly({{66, 256}, {76, 256}, {74, 90}, {68, 90}}, 7);
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
    b.poly({{w * 0.5f, h * 0.08f}, {w * 0.9f, h * 0.45f}, {w * 0.62f, h * 0.5f}, {w * 0.42f, h * 0.3f}}, 10);
    b.poly({{w * 0.04f, h * 0.98f}, {w * 0.1f, h * 0.55f}, {w * 0.35f, h * 0.62f}, {w * 0.45f, h * 0.98f}}, 8);
    for (int i = 0; i < 5; i++) {
        float x = w * (0.2f + r() * 0.6f), y = h * (0.3f + r() * 0.5f);
        b.line(x, y, x + w * 0.15f, y + h * 0.1f, 8, 2);
    }
    b.rect(0, float(h - 3), float(w), 3, 8);
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

Bitmap olive(Rng& r) {
    Bitmap b(192, 192);
    // Gnarled, twisted trunk.
    for (int i = 0; i < 3; i++) {
        float x0 = 90 + i * 6, y0 = 192;
        for (int s = 0; s < 8; s++) {
            float x1 = x0 + (r() - 0.5f) * 14, y1 = y0 - 12;
            b.line(x0, y0, x1, y1, i == 1 ? 3 : 2, 7);
            x0 = x1;
            y0 = y1;
        }
    }
    struct Blob { float x, y, rx, ry; };
    std::vector<Blob> blobs;
    for (int i = 0; i < 18; i++) blobs.push_back({30 + r() * 132, 20 + r() * 80, 16 + r() * 14, 12 + r() * 10});
    for (auto& o : blobs) b.ellipse(o.x, o.y + 3, o.rx, o.ry, 4);
    for (auto& o : blobs) b.ellipse(o.x + 3, o.y, o.rx * 0.8f, o.ry * 0.75f, 5);
    for (int i = 0; i < 90; i++) b.set(int(30 + r() * 132), int(15 + r() * 90), 15);  // silvery undersides
    return b;
}

Bitmap gum(Rng& r) {
    Bitmap b(176, 320);
    // Pale, peeling trunk forking into branches.
    b.poly({{80, 320}, {96, 320}, {94, 150}, {84, 150}}, 15);
    b.poly({{88, 320}, {96, 320}, {94, 150}, {90, 150}}, 13);
    for (int y = 170; y < 310; y += 16 + int(r() * 12)) b.rect(82 + r() * 8, float(y), 4 + r() * 4, 6, 12);
    b.line(88, 160, 40, 80, 15, 6);
    b.line(90, 150, 140, 70, 15, 6);
    b.line(88, 130, 92, 40, 15, 5);
    // Sparse, drooping clumps.
    for (int i = 0; i < 11; i++) {
        float cx = 24 + r() * 128, cy = 20 + r() * 90;
        for (int k = 0; k < 8; k++) b.ellipse(cx + (r() - 0.5f) * 26, cy + r() * 18, 7 + r() * 5, 4 + r() * 4, r() < 0.5f ? 4 : 5);
    }
    return b;
}

Bitmap cypress(Rng& r) {
    Bitmap b(64, 256);
    std::vector<Pt> pts;
    for (int i = 0; i <= 20; i++) {
        const float t = i / 20.0f;
        const float w = 26 * std::sin(std::min(1.0f, t * 1.2f) * PI * 0.75f) + (r() - 0.5f) * 3;
        pts.push_back({32 + w, 6 + t * 240});
    }
    for (int i = 20; i >= 0; i--) {
        const float t = i / 20.0f;
        const float w = 26 * std::sin(std::min(1.0f, t * 1.2f) * PI * 0.75f) + (r() - 0.5f) * 3;
        pts.push_back({32 - w, 6 + t * 240});
    }
    b.poly(pts, 4);
    for (int y = 10; y < 240; y += 6) b.line(32, float(y), 32 + 16 * std::sin(y * 0.02f), float(y + 4), 5, 2);
    b.rect(29, 244, 6, 12, 2);
    return b;
}

Bitmap stonewall(Rng& r) {
    Bitmap b(256, 64);
    for (int row = 0; row < 4; row++)
        for (float x = -8 + row * 7 % 20; x < 256; x += 18 + r() * 10) {
            const float y = 64 - (row + 1) * 15 - r() * 2, w = 16 + r() * 10, h = 13 + r() * 3;
            b.ellipse(x + w / 2, y + h / 2, w / 2, h / 2, 9);
            b.ellipse(x + w / 2 + 2, y + h / 2 - 2, w / 2.6f, h / 3, 10);
            b.ellipse(x + w / 2 - 2, y + h / 2 + 3, w / 3, h / 4, 8);
        }
    return b;
}

Bitmap anthill(Rng& r) {
    Bitmap b(96, 128);
    std::vector<Pt> pts = {{8, 128}};
    for (int i = 0; i <= 10; i++) {
        const float t = i / 10.0f;
        pts.push_back({8 + t * 80, 128 - (std::sin(t * PI) * 110 + (r() - 0.5f) * 10)});
    }
    pts.push_back({88, 128});
    b.poly(pts, 12);
    for (int i = 0; i < 40; i++) b.ellipse(20 + r() * 56, 30 + r() * 90, 3, 5, r() < 0.5f ? 13 : 11);
    return b;
}

Bitmap hut(int venueIndex) {
    Bitmap b(256, 192);
    const bool redCabin = venueIndex == 0 || venueIndex == 1;  // Nordic red timber cabin, else a stone farmhouse
    b.rect(30, 80, 196, 112, redCabin ? 11 : 9);
    if (redCabin)
        for (int y = 84; y < 192; y += 8) b.line(30, float(y), 226, float(y), 1, 1);
    else
        for (int i = 0; i < 40; i++) b.rect(34 + float((i * 37) % 180), 84 + float((i * 23) % 100), 12, 7, 10);
    b.poly({{16, 84}, {128, 20}, {240, 84}}, redCabin ? 1 : 11);
    b.poly({{128, 20}, {240, 84}, {200, 84}}, redCabin ? 8 : 3);
    b.rect(60, 110, 34, 30, 7);
    b.rect(62, 112, 30, 26, 14);
    b.rect(150, 120, 36, 72, 2);
    b.rect(160, 100, 8, 8, 7);
    return b;
}

Bitmap roo(bool sign) {
    Bitmap b(96, 96);
    auto body = [&](float ox, float oy, float k, int c) {
        b.ellipse(ox + 46 * k, oy + 56 * k, 20 * k, 26 * k, c);                       // body
        b.ellipse(ox + 60 * k, oy + 26 * k, 8 * k, 9 * k, c);                         // head
        b.poly({{ox + 60 * k, oy + 18 * k}, {ox + 63 * k, oy + 4 * k}, {ox + 66 * k, oy + 18 * k}}, c);  // ear
        b.line(ox + 30 * k, oy + 72 * k, ox + 4 * k, oy + 92 * k, c, 6 * k);          // tail
        b.poly({{ox + 40 * k, oy + 74 * k}, {ox + 70 * k, oy + 92 * k}, {ox + 36 * k, oy + 92 * k}}, c);  // feet
        b.line(ox + 58 * k, oy + 46 * k, ox + 68 * k, oy + 56 * k, c, 3 * k);         // arms
    };
    if (sign) {  // yellow diamond road sign on a post (common palette)
        b.rect(45, 60, 6, 36, 9);
        b.poly({{48, 0}, {94, 36}, {48, 72}, {2, 36}}, 1);
        b.poly({{48, 4}, {90, 36}, {48, 68}, {6, 36}}, 4);
        body(24, 12, 0.5f, 1);
    } else {  // a grey kangaroo standing (scene palette)
        body(0, 0, 1, 12);
        b.ellipse(46, 60, 12, 16, 13);
    }
    return b;
}

// ------------------------------------------------------------ people and signs (common palette)
// Common palette roles: 1 black, 2 white, 3 red, 4 yellow, 5 blue, 6 green, 7 orange,
// 8-9 greys, 10 skin, 11 brown, 12 purple, 13 cyan, 14 silver, 15 tyre.

Bitmap spectators(Rng& r) {
    Bitmap b(256, 128);
    const int jackets[] = {3, 5, 4, 6, 7, 12, 13, 2, 1};
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 11; i++) {
            float x = 6 + i * 22 + r() * 8 + row * 11, base = 128 - row * 12;
            float h = 64 + r() * 22;
            int j = jackets[int(r() * 9)];
            b.rect(x, base - h * 0.45f, 15, h * 0.45f, r() < 0.5f ? 8 : 5);  // jeans
            b.rect(x - 2, base - h * 0.88f, 19, h * 0.47f, j);                 // jacket
            b.ellipse(x + 7, base - h * 0.94f, 7, 8, 10);
            if (r() < 0.5f) b.rect(x, base - h * 1.04f, 14, 5, r() < 0.5f ? 1 : 3);  // cap
            const float k = r();
            if (k < 0.2f) {  // camera held up
                b.rect(x + 2, base - h * 1.05f, 12, 8, 1);
            } else if (k < 0.45f) {  // arms up, maybe a flag
                const float ax = x + (r() < 0.5f ? -6 : 18);
                b.rect(ax, base - h * 1.2f, 5, h * 0.4f, 10);
                if (r() < 0.6f) b.rect(ax, base - h * 1.45f, 22, 13, jackets[int(r() * 9)]);
            }
        }
    return b;
}

Bitmap marshal() {
    Bitmap b(64, 144);
    b.rect(20, 80, 10, 64, 5);
    b.rect(34, 80, 10, 64, 5);
    b.rect(16, 36, 32, 50, 7);   // orange vest
    b.rect(16, 52, 32, 6, 4);    // reflective band
    b.ellipse(32, 24, 10, 12, 10);
    b.rect(22, 10, 20, 8, 7);    // hat
    b.rect(46, 26, 6, 40, 7);    // raised arm
    b.rect(44, 0, 18, 14, 4);    // yellow flag
    return b;
}

Bitmap board(const char* text, int bg, int fg, bool checker) {
    Bitmap b(192, 256);
    b.rect(24, 100, 10, 156, 8);
    b.rect(158, 100, 10, 156, 8);
    b.rect(0, 0, 192, 116, 1);
    b.rect(6, 6, 180, 104, bg);
    if (checker)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 9; x++)
                if ((x + y) % 2) b.rect(6 + x * 20.0f, 6 + y * 26.0f, 20, 26, 1);
    Bitmap t = gs::textBitmap(text, {4, fg, 1, 0, 1});
    if (t.w > 176) t = t.resample(176, t.h * 176 / t.w);
    b.rect(96.0f - t.w / 2 - 4, 58.0f - t.h / 2 - 4, float(t.w + 8), float(t.h + 8), checker ? 2 : bg);
    b.blit(t, 96 - t.w / 2, 58 - t.h / 2);
    return b;
}

Bitmap stopSign() {
    Bitmap b(160, 256);
    b.rect(74, 120, 12, 136, 8);
    std::vector<Pt> oct;
    for (int i = 0; i < 8; i++) {
        const float a = PI / 8 + i * PI / 4;
        oct.push_back({80 + std::cos(a) * 76, 76 + std::sin(a) * 76});
    }
    b.poly(oct, 2);
    for (auto& p : oct) p = {80 + (p.first - 80) * 0.9f, 76 + (p.second - 76) * 0.9f};
    b.poly(oct, 3);
    Bitmap t = gs::textBitmap("STOP", {5, 2, 0, 0, 1});
    b.blit(t, 80 - t.w / 2, 76 - t.h / 2);
    return b;
}

Bitmap cautionSign() {
    Bitmap b(128, 176);
    b.rect(60, 90, 8, 86, 8);
    b.poly({{64, 0}, {126, 108}, {2, 108}}, 3);
    b.poly({{64, 16}, {110, 98}, {18, 98}}, 2);
    b.rect(60, 40, 8, 36, 1);
    b.rect(60, 82, 8, 8, 1);
    return b;
}

Bitmap arrowBoard(bool right) {
    Bitmap b(192, 96);
    b.rect(20, 60, 8, 36, 8);
    b.rect(164, 60, 8, 36, 8);
    b.rect(0, 0, 192, 66, 1);
    b.rect(4, 4, 184, 58, 2);
    for (int i = 0; i < 3; i++) {
        const float x = 14 + i * 58;
        if (right) b.poly({{x, 8}, {x + 22, 8}, {x + 48, 33}, {x + 22, 58}, {x, 58}, {x + 26, 33}}, 3);
        else b.poly({{x + 48, 8}, {x + 26, 8}, {x, 33}, {x + 26, 58}, {x + 48, 58}, {x + 22, 33}}, 3);
    }
    return b;
}

Bitmap kmPost() {
    Bitmap b(32, 96);
    b.rect(6, 0, 20, 96, 2);
    b.rect(6, 10, 20, 12, 3);
    b.rect(6, 0, 20, 4, 1);
    return b;
}

Bitmap tape() {
    Bitmap b(512, 64);
    for (int x = 0; x < 512; x += 128) b.rect(float(x) + 4, 8, 6, 56, 11);
    for (int x = 0; x < 512; x += 16) {
        b.rect(float(x), 16, 8, 5, 3);
        b.rect(float(x) + 8, 16, 8, 5, 2);
    }
    return b;
}

Bitmap fence() {
    Bitmap b(256, 64);
    for (int x = 0; x < 256; x += 40) b.rect(float(x) + 2, 4, 8, 60, 2);
    b.rect(0, 14, 256, 7, 3);
    b.rect(0, 34, 256, 7, 3);
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

// ------------------------------------------------------------ HUD pieces

Bitmap bigGlyph(char ch) {
    Bitmap t = gs::textBitmap(std::string(1, ch), {2, 1, 15, 0, 1});
    for (int y = 0; y < t.h; y++)
        for (int x = 0; x < t.w; x++) {
            uint8_t& p = t.px[size_t(y) * t.w + x];
            if (p == 1) p = y < t.h * 0.35f ? 3 : (y < t.h * 0.65f ? 1 : 2);
        }
    return t;
}

// Corner arrows: tighter the lower the number; a hairpin folds right back.
Bitmap cornerIcon(int sev) {
    Bitmap b(64, 64);
    if (sev == 8) {  // straight: an up arrow
        b.rect(26, 22, 12, 36, 1);
        b.poly({{32, 4}, {50, 26}, {14, 26}}, 1);
    } else {
        const float turn = sev == 7 ? 3.0f : (7 - sev) * 0.32f + 0.25f;  // radians of turn drawn
        std::vector<Pt> spine;
        const float R = sev == 7 ? 12 : 16 + (sev - 1) * 6;
        float x = 22, y = 60, h = 0;
        const int steps = 24;
        const float len = sev == 7 ? 58.0f : 56.0f;
        for (int i = 0; i <= steps; i++) {
            spine.push_back({x, y});
            const float t = float(i) / steps;
            const float k = (t > 0.35f) ? turn / (len * 0.65f) : 0;
            h += k * len / steps;
            x += std::sin(h) * len / steps;
            y -= std::cos(h) * len / steps;
        }
        (void)R;
        for (size_t i = 0; i + 1 < spine.size(); i++) b.line(spine[i].first, spine[i].second, spine[i + 1].first, spine[i + 1].second, 1, 8);
        Pt end = spine.back(), prev = spine[spine.size() - 3];
        float dx = end.first - prev.first, dy = end.second - prev.second, l = std::sqrt(dx * dx + dy * dy);
        dx /= l;
        dy /= l;
        b.poly({{end.first + dx * 10, end.second + dy * 10}, {end.first - dy * 10, end.second + dx * 10},
                {end.first + dy * 10, end.second - dx * 10}}, 1);
    }
    for (int y = 0; y < b.h; y++)
        for (int x = 0; x < b.w; x++)
            if (b.get(x, y) == 1 && y > b.h * 0.6f) b.set(x, y, 2);
    b.outline(15, true);
    return b;
}

Bitmap modIcon(int kind) {
    Bitmap b(40, 40);
    switch (kind) {
        case MI_CREST: b.poly({{2, 34}, {20, 12}, {38, 34}, {30, 34}, {20, 22}, {10, 34}}, 1); break;
        case MI_JUMP:
            b.poly({{2, 36}, {18, 26}, {18, 36}}, 1);
            b.poly({{22, 24}, {38, 36}, {22, 36}}, 1);
            b.line(10, 22, 20, 8, 1, 3);
            b.line(20, 8, 32, 18, 1, 3);
            break;
        case MI_WATER:
            for (int i = 0; i < 3; i++)
                for (int x = 2; x < 38; x++) b.set(x, 12 + i * 10 + int(std::sin(x * 0.5f) * 3), 1), b.set(x, 13 + i * 10 + int(std::sin(x * 0.5f) * 3), 1);
            break;
        case MI_CAUTION:
            b.poly({{20, 2}, {38, 36}, {2, 36}}, 4);
            b.rect(18, 12, 4, 14, 1);
            b.rect(18, 29, 4, 4, 1);
            break;
        case MI_DONTCUT:
            b.ellipse(20, 26, 14, 10, 1);
            b.ellipse(24, 22, 6, 4, 2);
            b.line(4, 4, 36, 36, 4, 4);
            break;
        case MI_TIGHTENS:
            b.line(6, 36, 6, 20, 1, 4);
            b.line(6, 20, 20, 8, 1, 4);
            b.line(20, 8, 34, 10, 1, 4);
            b.line(34, 10, 30, 22, 1, 4);
            break;
        case MI_BUMPS:
            for (int x = 2; x < 38; x++) b.set(x, 30 - int(std::fabs(std::sin(x * 0.4f)) * 12), 1), b.set(x, 31 - int(std::fabs(std::sin(x * 0.4f)) * 12), 1);
            break;
        default:  // finish flag
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 5; x++) b.rect(6 + x * 6.0f, 4 + y * 6.0f, 6, 6, (x + y) % 2 ? 1 : 3);
            b.rect(4, 4, 3, 34, 1);
            break;
    }
    b.outline(15, true);
    return b;
}

Bitmap damageIcon(int kind) {
    Bitmap b(24, 24);
    switch (kind) {
        case DI_ENGINE:
            b.rect(4, 8, 16, 12, 1);
            b.rect(8, 4, 8, 4, 1);
            b.rect(0, 11, 4, 6, 1);
            b.rect(20, 11, 4, 6, 1);
            break;
        case DI_SUSPENSION:
            for (int i = 0; i < 5; i++) b.line(6, 4 + i * 4.0f, 18, 6 + i * 4.0f, 1, 2);
            b.rect(10, 0, 4, 24, 1);
            break;
        case DI_TYRES:
            b.ellipse(12, 12, 11, 11, 1);
            b.ellipse(12, 12, 5, 5, 0);
            break;
        default:
            b.poly({{2, 16}, {6, 8}, {18, 8}, {22, 16}, {22, 20}, {2, 20}}, 1);
            break;
    }
    return b;
}

Bitmap logoBitmap() {
    Bitmap b(320, 132);
    Bitmap l1 = gs::textBitmap("(3)", {4, 5, 15, 0, 1});
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

// ------------------------------------------------------------ cockpit (HUD palette)

Bitmap dashboard() {
    Bitmap b(320, 84);
    // Dash top edge curving up in the middle for the binnacle.
    std::vector<Pt> top = {{0, 84}, {0, 38}};
    for (int x = 0; x <= 320; x += 8) {
        const float t = (x - 160) / 160.0f;
        top.push_back({float(x), 38 - 22 * std::exp(-t * t * 9)});
    }
    top.push_back({320, 84});
    b.poly(top, 12);
    // Grain and a lighter lip.
    for (int x = 0; x < 320; x++) {
        const float t = (x - 160) / 160.0f;
        const int y = int(38 - 22 * std::exp(-t * t * 9));
        b.set(x, y, 14);
        b.set(x, y + 1, 13);
    }
    // Instrument binnacle holes are left dark; switches on the centre console.
    for (int i = 0; i < 6; i++) b.rect(210 + i * 14.0f, 60, 8, 6, i == 2 ? 4 : 10);
    b.rect(210, 70, 86, 3, 8);
    return b;
}

Bitmap steeringWheel(float angle) {
    Bitmap b(176, 176);
    const float cx = 88, cy = 88;
    // Rim.
    for (int y = 0; y < 176; y++)
        for (int x = 0; x < 176; x++) {
            const float d = std::hypot(x - cx, y - cy);
            if (d < 84 && d > 70) b.set(x, y, d > 80 || d < 73 ? 11 : 12);
        }
    // Three spokes and the hub, turned by `angle`.
    auto spoke = [&](float a, float w) {
        const float ca = std::cos(a + angle), sa = std::sin(a + angle);
        b.line(cx, cy, cx + ca * 72, cy + sa * 72, 13, w);
    };
    spoke(0, 12);
    spoke(PI, 12);
    spoke(PI / 2, 16);
    b.ellipse(cx, cy, 20, 20, 11);
    b.ellipse(cx, cy, 14, 14, 13);
    // Top-dead-centre marker (a yellow stripe on real rally wheels).
    const float ma = -PI / 2 + angle;
    for (int t = 70; t <= 84; t++)
        for (int k = -4; k <= 4; k++) b.set(int(cx + std::cos(ma) * t - std::sin(ma) * k * 0.6f), int(cy + std::sin(ma) * t + std::cos(ma) * k * 0.6f), 6);
    return b;
}

Bitmap tachoNeedle(float frac) {
    Bitmap b(64, 64);
    // Dial face and scale.
    b.ellipse(32, 32, 30, 30, 11);
    b.ellipse(32, 32, 28, 28, 15);
    for (int i = 0; i <= 8; i++) {
        const float a = PI * 0.75f + i * PI * 1.5f / 8;
        b.line(32 + std::cos(a) * 22, 32 + std::sin(a) * 22, 32 + std::cos(a) * 27, 32 + std::sin(a) * 27, i >= 7 ? 4 : 1, 2);
    }
    const float a = PI * 0.75f + frac * PI * 1.5f;
    b.line(32, 32, 32 + std::cos(a) * 25, 32 + std::sin(a) * 25, 6, 3);
    b.ellipse(32, 32, 4, 4, 2);
    return b;
}

Bitmap pillar() {
    // Left A-pillar and door top; the right one is the same flipped.
    Bitmap b(88, 224);
    b.poly({{0, 0}, {40, 0}, {12, 150}, {0, 158}}, 12);
    b.poly({{40, 0}, {45, 0}, {16, 152}, {12, 150}}, 13);
    b.poly({{0, 158}, {12, 150}, {88, 200}, {88, 224}, {0, 224}}, 12);
    b.poly({{12, 150}, {16, 152}, {88, 196}, {88, 200}}, 13);
    return b;
}

Bitmap roofBar() {
    Bitmap b(320, 24);
    b.rect(0, 0, 320, 16, 12);
    b.rect(0, 16, 320, 3, 13);
    // Roll cage bar and the rear-view mirror mount.
    b.rect(0, 19, 320, 5, 8);
    b.rect(150, 0, 20, 20, 12);
    return b;
}

Bitmap mirror() {
    Bitmap b(96, 28);
    b.rect(0, 0, 96, 28, 11);
    b.rect(3, 3, 90, 22, 8);
    b.rect(3, 3, 90, 8, 10);
    return b;
}

Bitmap coDriver() {
    // The co-driver in the right seat: helmet, shoulder, and the pace notes book.
    Bitmap b(120, 150);
    b.ellipse(70, 50, 44, 48, 1);   // helmet
    b.ellipse(70, 50, 40, 44, 2);
    b.rect(28, 42, 34, 18, 5);      // visor
    b.rect(38, 30, 70, 7, 3);       // helmet stripe
    b.poly({{10, 150}, {26, 96}, {110, 96}, {120, 150}}, 7);  // race suit shoulder
    b.rect(0, 110, 50, 40, 1);      // notes book
    b.rect(3, 113, 44, 34, 2);
    for (int i = 0; i < 5; i++) b.rect(6, 118 + i * 6.0f, 36, 2, 8);
    return b;
}

Bitmap gloves() {
    Bitmap b(200, 60);
    b.ellipse(24, 30, 22, 26, 11);
    b.ellipse(176, 30, 22, 26, 11);
    b.ellipse(22, 26, 16, 18, 8);
    b.ellipse(178, 26, 16, 18, 8);
    return b;
}

Bitmap windscreenDirt(int level, Rng& r) {
    Bitmap b(320, 150);
    const int splats = 20 + level * 45;
    for (int i = 0; i < splats; i++) {
        const float x = r() * 320, y = 20 + r() * 130, s = 1 + r() * (2 + level * 1.5f);
        b.ellipse(x, y, s * 1.5f, s, r() < 0.6f ? 1 : 2);
    }
    return b;
}

Bitmap wiper(float angle) {
    Bitmap b(320, 150);
    const float px = 110, py = 150;
    const float a = -PI + angle;
    b.line(px, py, px + std::cos(a) * 150, py + std::sin(a) * 150, 11, 4);
    b.line(px + 220 * 0.0f + 110, py, px + 110 + std::cos(a) * 150, py + std::sin(a) * 150, 11, 4);
    return b;
}

// ------------------------------------------------------------ backdrops
// Far palette: 1-2 cloud, 3-4 mountain, 5-6 snow, 7 far hill, 8-9 sun, 10 haze, 11-12 extra (sea).
// Near palette: 1-2 hills, 3-5 trees, 6-7 water, 8-9 ground/snow, 10 rock, 11 white, 12 dark.

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
        return base - amp * (0.55f * std::sin(TAU * x / W + ph[0]) + 0.3f * std::sin(TAU * 3 * x / W + ph[1]) +
                             0.15f * std::sin(TAU * 7 * x / W + ph[2]) + (jagged ? 0.1f : 0.04f) * std::sin(TAU * 17 * x / W + ph[3]) +
                             (jagged ? 0.06f : 0.0f) * std::sin(TAU * 41 * x / W + ph[4]));
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
        for (int y = std::max(0, int(top)); y < b.h; y++) b.set(x, y, slope > 0 ? c1 : c2);
    }
}

void blobTrees(Bitmap& b, Rng& r, float base, int count, float size, int c1, int c2) {
    const int W = b.w;
    for (int i = 0; i < count; i++) {
        const float x = r() * W, s = size * (0.6f + r() * 0.6f);
        for (int o : {-W, 0, W}) {
            b.rect(x + o - 1, base - s * 0.8f, 2, s * 0.8f, 12);
            b.ellipse(x + o, base - s * 0.9f, s * 0.7f, s * 0.45f, r() < 0.5f ? c1 : c2);
        }
    }
}

void backdropFar(Bitmap& b, int v, int tod, Rng& r) {
    switch (v) {
        case 0:  // Finland: soft blue forested ridges, big summer clouds
            clouds(b, r, 7, 24, 110);
            mountains(b, r, 222, 20, 0, false);
            hills(b, r, 238, 6, 7, 7);
            break;
        case 1:  // Norway: snowy peaks
            clouds(b, r, tod == 2 ? 0 : 5, 24, 90);
            mountains(b, r, 214, 62, 206, true);
            break;
        case 2:  // Sardinia: the sea on the horizon, dry hills
            clouds(b, r, 3, 30, 80);
            b.rect(0, 232, float(b.w), 24, 11);
            for (int y = 234; y < 256; y += 3) b.rect(0, float(y), float(b.w), 1, 12);
            hills(b, r, 230, 12, 3, 4);
            break;
        case 3:  // Australia: flat-topped red ranges far away
            if (tod == 2)
                for (int o : {0, b.w}) {
                    b.ellipse(300.0f - o, 214, 40, 40, 9);
                    b.ellipse(300.0f - o, 214, 30, 30, 8);
                }
            clouds(b, r, 2, 40, 90);
            mountains(b, r, 236, 12, 0, false);
            break;
        default:  // Cyprus: big mountains in the haze
            clouds(b, r, 3, 30, 90);
            mountains(b, r, 214, 48, 0, false);
            break;
    }
}

void backdropNear(Bitmap& b, int v, Rng& r) {
    switch (v) {
        case 0:
            treeline(b, r, 244, 12, 30, 3, 4, 3);
            break;
        case 1:
            hills(b, r, 236, 8, 8, 9);
            treeline(b, r, 246, 8, 22, 3, 4, 8);
            break;
        case 2:
            hills(b, r, 240, 8, 1, 2);
            blobTrees(b, r, 244, 40, 9, 4, 5);
            break;
        case 3:
            b.rect(0, 244, float(b.w), 12, 1);
            blobTrees(b, r, 246, 26, 12, 4, 5);
            break;
        default:
            hills(b, r, 236, 12, 1, 2);
            treeline(b, r, 246, 6, 18, 3, 4, 2);
            break;
    }
}

}  // namespace

// ------------------------------------------------------------ palettes

void setCarPalette(gs::VDP& vdp, int pal, int car) {
    const CarSpec& c = carSpec(car);
    for (int i = 0; i < 16; i++) vdp.setColor(pal * 16 + i, c.livery[i]);
}

void setLivery(gs::VDP& vdp, int pal, int livery) {
    // body, shade, stripe 1, stripe 2, highlight, decal
    static const uint16_t L[NUM_LIVERIES][6] = {
        {rgb4(15, 13, 0), rgb4(11, 9, 0), rgb4(0, 8, 3), rgb4(1, 1, 1), rgb4(15, 15, 8), rgb4(14, 2, 2)},
        {rgb4(2, 4, 12), rgb4(1, 2, 8), rgb4(15, 12, 0), rgb4(15, 15, 15), rgb4(5, 8, 15), rgb4(15, 12, 0)},
        {rgb4(3, 3, 3), rgb4(1, 1, 1), rgb4(15, 7, 0), rgb4(15, 15, 15), rgb4(7, 7, 7), rgb4(15, 7, 0)},
        {rgb4(15, 15, 15), rgb4(11, 12, 13), rgb4(3, 6, 14), rgb4(14, 2, 2), rgb4(15, 15, 15), rgb4(3, 6, 14)},
        {rgb4(0, 8, 9), rgb4(0, 5, 6), rgb4(15, 15, 15), rgb4(15, 9, 0), rgb4(4, 12, 13), rgb4(15, 15, 15)},
        {rgb4(10, 0, 3), rgb4(6, 0, 2), rgb4(15, 13, 0), rgb4(15, 15, 15), rgb4(14, 3, 6), rgb4(15, 13, 0)},
        {rgb4(12, 12, 12), rgb4(8, 8, 9), rgb4(0, 10, 4), rgb4(1, 1, 1), rgb4(15, 15, 15), rgb4(0, 10, 4)},
        {rgb4(15, 8, 0), rgb4(11, 5, 0), rgb4(1, 1, 1), rgb4(15, 15, 15), rgb4(15, 12, 5), rgb4(1, 1, 1)},
    };
    const uint16_t* l = L[std::clamp(livery, 0, NUM_LIVERIES - 1)];
    setCarPalette(vdp, pal, 0);
    const int base = pal * 16;
    vdp.setColor(base + 3, l[0]);
    vdp.setColor(base + 4, l[1]);
    vdp.setColor(base + 5, l[2]);
    vdp.setColor(base + 6, l[3]);
    vdp.setColor(base + 13, l[4]);
    vdp.setColor(base + 15, l[5]);
}

static void loadFixedPalettes(gs::VDP& vdp) {
    auto set = [&](int pal, std::initializer_list<uint16_t> cs) {
        int i = 0;
        for (uint16_t c : cs) vdp.setColor(pal * 16 + i++, c);
    };
    set(PAL_HUD, {0, rgb4(15, 15, 15), rgb4(11, 11, 12), rgb4(15, 15, 15), rgb4(15, 3, 2), rgb4(2, 13, 3), rgb4(15, 8, 0),
                  rgb4(3, 6, 15), rgb4(4, 4, 5), rgb4(3, 13, 14), rgb4(8, 8, 9), rgb4(0, 0, 0), rgb4(2, 2, 2), rgb4(3, 3, 3),
                  rgb4(5, 5, 6), rgb4(0, 0, 1)});
    set(PAL_YELLOW, {0, rgb4(15, 13, 0), rgb4(15, 7, 0), rgb4(15, 15, 10), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, rgb4(0, 0, 1)});
    set(PAL_RED, {0, rgb4(15, 3, 2), rgb4(10, 1, 1), rgb4(15, 11, 10), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, rgb4(0, 0, 1)});
    set(PAL_COMMON, {0, rgb4(1, 1, 1), rgb4(15, 15, 15), rgb4(14, 2, 2), rgb4(15, 13, 2), rgb4(2, 4, 13), rgb4(2, 10, 3),
                     rgb4(15, 7, 0), rgb4(4, 4, 5), rgb4(9, 9, 10), rgb4(14, 11, 8), rgb4(7, 4, 2), rgb4(9, 3, 11),
                     rgb4(3, 12, 13), rgb4(12, 12, 13), rgb4(3, 3, 3)});
    set(PAL_LOGO, {0, rgb4(15, 15, 10), rgb4(15, 13, 0), rgb4(15, 7, 0), rgb4(14, 2, 1), rgb4(15, 15, 15), 0, 0, 0, 0, 0, 0, 0,
                   0, 0, rgb4(0, 0, 1)});
}

void buildArt(gs::VDP& vdp, Art& a) {
    Rng r{4321};
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

    // Scenery.
    a.obj[O_PINE] = gs::uploadMipped(vdp, pine(false, r));
    a.obj[O_PINE_SNOW] = gs::uploadMipped(vdp, pine(true, r));
    a.obj[O_BIRCH] = gs::uploadMipped(vdp, birch(r));
    a.obj[O_BUSH] = gs::uploadMipped(vdp, bush(r));
    a.obj[O_ROCK] = gs::uploadMipped(vdp, rock(144, 96, r));
    a.obj[O_BOULDER] = gs::uploadMipped(vdp, rock(256, 208, r));
    a.obj[O_SNOWBANK] = gs::uploadMipped(vdp, snowbank(r));
    a.obj[O_CLIFF] = gs::uploadMipped(vdp, cliff(r));
    a.obj[O_LOGS] = gs::uploadMipped(vdp, logs());
    a.obj[O_BALE] = gs::uploadMipped(vdp, bale());
    a.obj[O_OLIVE] = gs::uploadMipped(vdp, olive(r));
    a.obj[O_GUM] = gs::uploadMipped(vdp, gum(r));
    a.obj[O_STONEWALL] = gs::uploadMipped(vdp, stonewall(r));
    a.obj[O_SPECTATORS] = gs::uploadMipped(vdp, spectators(r));
    a.obj[O_MARSHAL] = gs::uploadMipped(vdp, marshal());
    a.obj[O_BOARD_START] = gs::uploadMipped(vdp, board("START", 3, 2, false));
    a.obj[O_BOARD_SPLIT] = gs::uploadMipped(vdp, board("SPLIT", 4, 1, false));
    a.obj[O_BOARD_FINISH] = gs::uploadMipped(vdp, board("FINISH", 2, 1, true));
    a.obj[O_BOARD_STOP] = gs::uploadMipped(vdp, stopSign());
    a.obj[O_ROO_SIGN] = gs::uploadMipped(vdp, roo(true));
    a.obj[O_CAUTION_SIGN] = gs::uploadMipped(vdp, cautionSign());
    a.obj[O_ARROW_L] = gs::uploadMipped(vdp, arrowBoard(false));
    a.obj[O_ARROW_R] = gs::uploadMipped(vdp, arrowBoard(true));
    a.obj[O_KM_POST] = gs::uploadMipped(vdp, kmPost());
    a.obj[O_TAPE] = gs::uploadMipped(vdp, tape());
    a.obj[O_FENCE] = gs::uploadMipped(vdp, fence());
    a.obj[O_ANTHILL] = gs::uploadMipped(vdp, anthill(r));
    a.obj[O_CYPRESS] = gs::uploadMipped(vdp, cypress(r));
    a.obj[O_HUT] = gs::uploadMipped(vdp, hut(2));
    a.obj[O_ROO] = gs::uploadMipped(vdp, roo(false));

    // The car from every angle.
    const float elev = 0.24f;
    for (int y = 0; y < YAW_FRAMES; y++)
        for (int p = 0; p < PITCH_FRAMES; p++)
            a.car[y][p] = gs::uploadMipped(vdp, renderCar(y * TAU / YAW_FRAMES, (p - 1) * 0.16f, 0, elev));
    for (int k = 0; k < ROLL_FRAMES; k++) {
        a.carRoll[0][k] = gs::uploadMipped(vdp, renderCar(0, 0, k * TAU / ROLL_FRAMES, elev));
        a.carRoll[1][k] = gs::uploadMipped(vdp, renderCar(PI / 2, 0, k * TAU / ROLL_FRAMES, elev));
    }

    // Effects.
    Bitmap sh(128, 32);
    sh.ellipse(64, 16, 63, 15, 1);
    a.shadow = gs::uploadMipped(vdp, sh);
    Bitmap puff(64, 64);
    puff.ellipse(34, 34, 28, 24, 2);
    puff.ellipse(29, 29, 24, 20, 1);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            if (puff.get(x, y) && ((x + y) & 1) && (std::hypot(x - 32.0f, y - 32.0f) > 22)) puff.set(x, y, 0);
    a.puff = gs::uploadMipped(vdp, puff);
    // Dust hanging in the air: a checkerboard-dithered cloud, the 16-bit way of drawing something see-through.
    Bitmap haze(96, 64);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 96; x++) {
            const float d = std::hypot((x - 48) / 48.0f, (y - 34) / 30.0f);
            if (d < 1 && ((x + y) & 1) && (d < 0.7f || ((x / 2 + y / 2) & 1))) haze.set(x, y, d < 0.45f ? 1 : 2);
        }
    {  // no mipmaps: shrinking would fill in the dither
        const gs::Image img = gs::uploadImage(vdp, haze);
        a.haze.w = haze.w;
        a.haze.h = haze.h;
        a.haze.lv[0] = a.haze.lv[1] = a.haze.lv[2] = img;
    }
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
    Bitmap clod(48, 40);
    for (int i = 0; i < 14; i++) clod.ellipse(6 + r() * 36, 6 + r() * 28, 2 + r() * 3, 2 + r() * 2, r() < 0.5f ? 8 : 2);
    a.clod = gs::uploadMipped(vdp, clod);
    Bitmap spark(4, 4);
    spark.rect(0, 0, 4, 4, 1);
    a.spark = gs::uploadMipped(vdp, spark);

    // HUD.
    for (int c = 32; c < 128; c++) a.glyph[c - 32] = gs::uploadMipped(vdp, bigGlyph(char(c)));
    for (int i = 0; i < NUM_ICONS; i++) a.icon[i] = gs::uploadMipped(vdp, cornerIcon(i + 1));
    for (int i = 0; i < MI_COUNT; i++) a.mod[i] = gs::uploadMipped(vdp, modIcon(i));
    for (int i = 0; i < DI_COUNT; i++) a.damage[i] = gs::uploadMipped(vdp, damageIcon(i));
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

    // Cockpit.
    a.dash = gs::uploadMipped(vdp, dashboard());
    for (int i = 0; i < WHEEL_FRAMES; i++) a.wheel[i] = gs::uploadMipped(vdp, steeringWheel((i - (WHEEL_FRAMES - 1) / 2) * (PI / 12)));
    for (int i = 0; i < NEEDLE_FRAMES; i++) a.needle[i] = gs::uploadMipped(vdp, tachoNeedle(float(i) / (NEEDLE_FRAMES - 1)));
    a.pillar = gs::uploadMipped(vdp, pillar());
    a.roofBar = gs::uploadMipped(vdp, roofBar());
    a.mirror = gs::uploadMipped(vdp, mirror());
    a.codriver = gs::uploadMipped(vdp, coDriver());
    a.gloves = gs::uploadMipped(vdp, gloves());
    for (int i = 0; i < 3; i++) a.dirt[i] = gs::uploadMipped(vdp, windscreenDirt(i, r));
    for (int i = 0; i < 3; i++) a.wiper[i] = gs::uploadMipped(vdp, wiper(0.35f + i * 0.9f));
    a.map = vdp.allocImage(72, 72);
}

int loadVenue(gs::VDP& vdp, const Art& a, int v, int tod) {
    const Venue& V = venue(v);
    // Time of day darkens the scenery towards dusk blue or night.
    auto dim = [&](uint16_t c) -> uint16_t {
        if (tod == 0 || c == 0) return c;
        int r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
        if (tod == 1) { r = r * 3 / 4; g = g * 2 / 3; b = std::min(15, b * 3 / 4 + 1); }
        else { r = r / 4; g = g / 4; b = std::min(15, b / 3 + 1); }
        return rgb4(r, g, b);
    };
    for (int i = 0; i < 16; i++) {
        for (int k = 0; k < 4; k++) vdp.setColor(ROAD_PALS[k] * 16 + i, V.road[k][i]);  // headlights keep the road lit
        vdp.setColor(PAL_SCENE * 16 + i, tod == 2 ? V.scene[i] : dim(V.scene[i]));
        vdp.setColor(PAL_FAR * 16 + i, dim(V.far[i]));
        vdp.setColor(PAL_NEAR * 16 + i, dim(V.near[i]));
    }
    const uint16_t fx[16] = {0, V.dust[0], V.dust[1], rgb4(15, 15, 15), rgb4(12, 13, 15), rgb4(10, 13, 15),
                             rgb4(5, 8, 13), rgb4(15, 13, 5), rgb4(5, 4, 3), 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 16; i++) vdp.setColor(PAL_FX * 16 + i, fx[i]);

    gs::TileAlloc tiles(vdp, a.firstFreeTile);
    Rng r{uint32_t(91 + v * 37)};
    Bitmap far(512, 256), near(1024, 256);
    backdropFar(far, v, tod, r);
    backdropNear(near, v, r);
    vdp.B.resize(64, 32);
    vdp.A.resize(128, 32);
    gs::bitmapToPlane(tiles, vdp.B, 0, 0, far, PAL_FAR);
    gs::bitmapToPlane(tiles, vdp.A, 0, 0, near, PAL_NEAR);
    return tiles.used();
}

}  // namespace rc
