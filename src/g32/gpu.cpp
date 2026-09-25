#include "gpu.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace g32 {

namespace {
// 4x4 ordered dither, in 1/16ths of a 5-bit step.
const int BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
inline float edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }
}  // namespace

GPU::GPU() : ram_(TEX_RAM / 2), fb_(size_t(W) * H), out_(size_t(W) * H) {}

int GPU::texture(int w, int h, const uint16_t* texels) {
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) throw std::runtime_error("S3-32: bad texture size");
    const size_t n = size_t(w) * h;
    if (used_ / 2 + n > ram_.size()) throw std::runtime_error("S3-32: texture RAM full");
    std::copy(texels, texels + n, ram_.begin() + long(used_ / 2));
    const bool pow2 = (w & (w - 1)) == 0 && (h & (h - 1)) == 0;
    tex_.push_back({w, h, used_ / 2, pow2});
    used_ += n * 2;
    return int(tex_.size()) - 1;
}

void GPU::tri(const Vtx& a, const Vtx& b, const Vtx& c, float depth, int tex, Blend blend) {
    if (prims_.size() >= 16384) return;  // over budget: the hardware would drop it
    prims_.push_back({{a, b, c}, depth, tex, blend, order_++});
}

void GPU::quad(const Vtx& a, const Vtx& b, const Vtx& c, const Vtx& d, float depth, int tex, Blend blend) {
    tri(a, b, c, depth, tex, blend);
    tri(a, c, d, depth, tex, blend);
}

void GPU::sprite(float x, float y, float w, float h, int tex, float u0, float v0, float u1, float v1, float depth, uint8_t shade,
                 Blend blend, bool flip) {
    if (flip) std::swap(u0, u1);
    Vtx a, b, c, d;
    a.x = x, a.y = y, a.u = u0, a.v = v0;
    b.x = x + w, b.y = y, b.u = u1, b.v = v0;
    c.x = x + w, c.y = y + h, c.u = u1, c.v = v1;
    d.x = x, d.y = y + h, d.u = u0, d.v = v1;
    for (Vtx* p : {&a, &b, &c, &d}) p->r = p->g = p->b = shade;
    quad(a, b, c, d, depth, tex, blend);
}

void GPU::raster(const Prim& p) {
    const Vtx &A = p.v[0], &B = p.v[1], &C = p.v[2];
    float area = edge(A.x, A.y, B.x, B.y, C.x, C.y);
    if (std::fabs(area) < 1e-6f) return;
    const int x0 = std::max(0, int(std::floor(std::min({A.x, B.x, C.x}))));
    const int x1 = std::min(W - 1, int(std::ceil(std::max({A.x, B.x, C.x}))));
    const int y0 = std::max(0, int(std::floor(std::min({A.y, B.y, C.y}))));
    const int y1 = std::min(H - 1, int(std::ceil(std::max({A.y, B.y, C.y}))));
    if (x0 > x1 || y0 > y1) return;
    const float inv = 1.0f / area;
    const Tex* t = p.tex >= 0 && p.tex < int(tex_.size()) ? &tex_[size_t(p.tex)] : nullptr;
    const uint16_t* tr = t ? &ram_[t->off] : nullptr;
    const bool shaded = !(A.r == B.r && B.r == C.r && A.g == B.g && B.g == C.g && A.b == B.b && B.b == C.b && A.r == 128 && A.g == 128 && A.b == 128) ||
                        A.fog > 0 || B.fog > 0 || C.fog > 0;
    // Edge functions stepped across the bounding box (sampled at pixel centres).
    for (int y = y0; y <= y1; y++) {
        const float py = y + 0.5f;
        uint16_t* row = &fb_[size_t(y) * W];
        for (int x = x0; x <= x1; x++) {
            const float px = x + 0.5f;
            float w0 = edge(B.x, B.y, C.x, C.y, px, py) * inv;
            float w1 = edge(C.x, C.y, A.x, A.y, px, py) * inv;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            // Colour: the texel (or white), times the shade.
            float r = (A.r * w0 + B.r * w1 + C.r * w2) / 128.0f;
            float g = (A.g * w0 + B.g * w1 + C.g * w2) / 128.0f;
            float b = (A.b * w0 + B.b * w1 + C.b * w2) / 128.0f;
            float cr = 31, cg = 31, cb = 31;
            if (tr) {
                int tu = int(std::floor(A.u * w0 + B.u * w1 + C.u * w2));
                int tv = int(std::floor(A.v * w0 + B.v * w1 + C.v * w2));
                if (t->pow2) {
                    tu &= t->w - 1;
                    tv &= t->h - 1;
                } else {
                    tu = std::clamp(tu, 0, t->w - 1);
                    tv = std::clamp(tv, 0, t->h - 1);
                }
                const uint16_t tx = tr[size_t(tv) * t->w + tu];
                if (!(tx & 0x8000)) continue;  // transparent texel
                cr = float((tx >> 10) & 31), cg = float((tx >> 5) & 31), cb = float(tx & 31);
            }
            cr *= r, cg *= g, cb *= b;
            const float f = A.fog * w0 + B.fog * w1 + C.fog * w2;
            if (f > 0) {
                cr += (fogR_ - cr) * f;
                cg += (fogG_ - cg) * f;
                cb += (fogB_ - cb) * f;
            }
            // Down to 15 bits, dithered where the colour was worked out.
            const float d = shaded ? (BAYER[y & 3][x & 3] - 7.5f) / 16.0f : 0;
            int R = std::clamp(int(cr + d + 0.5f), 0, 31), G = std::clamp(int(cg + d + 0.5f), 0, 31), Bc = std::clamp(int(cb + d + 0.5f), 0, 31);
            uint16_t& dst = row[x];
            if (p.blend == HALF) {
                R = (R + ((dst >> 10) & 31)) / 2, G = (G + ((dst >> 5) & 31)) / 2, Bc = (Bc + (dst & 31)) / 2;
            } else if (p.blend == ADD) {
                R = std::min(31, R + ((dst >> 10) & 31)), G = std::min(31, G + ((dst >> 5) & 31)), Bc = std::min(31, Bc + (dst & 31));
            }
            dst = uint16_t(R << 10 | G << 5 | Bc);
        }
    }
}

const uint32_t* GPU::draw() {
    for (int y = 0; y < H; y++) std::fill(fb_.begin() + y * W, fb_.begin() + (y + 1) * W, uint16_t(lineColor[y] & 0x7fff));
    // The ordering table: far to near, submission order within a depth.
    std::stable_sort(prims_.begin(), prims_.end(), [](const Prim& a, const Prim& b) { return a.depth > b.depth; });
    for (const Prim& p : prims_) raster(p);
    lastPrims_ = int(prims_.size());
    prims_.clear();
    order_ = 0;
    for (size_t i = 0; i < fb_.size(); i++) {
        const uint16_t c = fb_[i];
        const uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
        out_[i] = 0xff000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
    }
    return out_.data();
}

}  // namespace g32

namespace g32 {

void GPU::release(int from) {
    if (from < 0 || from >= int(tex_.size())) return;
    used_ = tex_[size_t(from)].off * 2;
    tex_.resize(size_t(from));
}

}  // namespace g32
