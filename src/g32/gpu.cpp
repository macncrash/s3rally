#include "gpu.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif

namespace g32 {

namespace {
// 4x4 ordered dither, in 1/16ths of a 5-bit step.
const int BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
inline float edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }
constexpr float K5 = 255.0f / 31.0f;  // a 5-bit channel in 8-bit units
inline uint32_t argb15(uint16_t c) {
    const uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    return 0xff000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
}
}  // namespace

GPU::GPU(Model model)
    : model_(model),
      fbW_(model == Model::S3_64 ? W * 2 : W),
      fbH_(model == Model::S3_64 ? H * 2 : H),
      scale_(model == Model::S3_64 ? 2 : 1),
      ram_((model == Model::S3_64 ? TEX_RAM_64 : TEX_RAM) / 2),
      out_(size_t(fbW_) * fbH_) {
    if (model_ == Model::S3_32) fb_.resize(size_t(W) * H);
    else zb_.resize(size_t(fbW_) * fbH_);
}

int GPU::texture(int w, int h, const uint16_t* texels) {
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) throw std::runtime_error("S3: bad texture size");
    const bool pow2 = (w & (w - 1)) == 0 && (h & (h - 1)) == 0;
    // The 64 keeps a mipmap chain for repeating textures: each level half the size, down to 1 texel.
    int levels = 1;
    size_t n = size_t(w) * h;
    if (model_ == Model::S3_64 && pow2)
        for (int lw = w, lh = h; lw > 1 || lh > 1; levels++) {
            lw = std::max(1, lw / 2), lh = std::max(1, lh / 2);
            n += size_t(lw) * lh;
        }
    if (used_ / 2 + n > ram_.size()) throw std::runtime_error("S3: texture RAM full");
    uint16_t* dst = &ram_[used_ / 2];
    std::copy(texels, texels + size_t(w) * h, dst);
    const uint16_t* src = dst;
    for (int l = 1, sw = w, sh = h; l < levels; l++) {
        const int dw = std::max(1, sw / 2), dh = std::max(1, sh / 2);
        uint16_t* d = const_cast<uint16_t*>(src) + size_t(sw) * sh;
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++) {
                int r = 0, g = 0, b = 0, seen = 0;
                for (int k = 0; k < 4; k++) {
                    const uint16_t t = src[size_t(std::min(sh - 1, y * 2 + k / 2)) * sw + std::min(sw - 1, x * 2 + k % 2)];
                    if (!(t & 0x8000)) continue;
                    r += (t >> 10) & 31, g += (t >> 5) & 31, b += t & 31, seen++;
                }
                d[size_t(y) * dw + x] = seen >= 2 ? texel((r + seen / 2) / seen, (g + seen / 2) / seen, (b + seen / 2) / seen) : 0;
            }
        src = d, sw = dw, sh = dh;
    }
    tex_.push_back({w, h, used_ / 2, pow2, levels});
    used_ += n * 2;
    return int(tex_.size()) - 1;
}

void GPU::release(int from) {
    if (from < 0 || from >= int(tex_.size())) return;
    used_ = tex_[size_t(from)].off * 2;
    tex_.resize(size_t(from));
}

void GPU::tri(const Vtx& a, const Vtx& b, const Vtx& c, float depth, int tex, Blend blend) {
    if (prims_.size() >= (model_ == Model::S3_64 ? 65536u : 16384u)) return;  // over budget: the hardware would drop it
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

// ---------------------------------------------------------------- S3-32

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

// ---------------------------------------------------------------- S3-64

void GPU::raster64(const Prim& p, int yLo, int yHi) {
    const float s = float(scale_);
    Vtx A = p.v[0], B = p.v[1], C = p.v[2];
    for (Vtx* v : {&A, &B, &C}) v->x *= s, v->y *= s;
    const float area = edge(A.x, A.y, B.x, B.y, C.x, C.y);
    if (std::fabs(area) < 1e-6f) return;
    const int x0 = std::max(0, int(std::floor(std::min({A.x, B.x, C.x}))));
    const int x1 = std::min(fbW_ - 1, int(std::ceil(std::max({A.x, B.x, C.x}))));
    const int y0 = std::max(yLo, int(std::floor(std::min({A.y, B.y, C.y}))));
    const int y1 = std::min(yHi - 1, int(std::ceil(std::max({A.y, B.y, C.y}))));
    if (x0 > x1 || y0 > y1) return;
    const float inv = 1.0f / area;
    // Perspective: attributes are interpolated divided by depth, then divided back.
    const bool world = A.z > 0 && B.z > 0 && C.z > 0;
    const float qa = world ? 1 / A.z : 1, qb = world ? 1 / B.z : 1, qc = world ? 1 / C.z : 1;
    const Tex* t = p.tex >= 0 && p.tex < int(tex_.size()) ? &tex_[size_t(p.tex)] : nullptr;
    const bool filter = t && world;  // the HUD stays crisp
    const bool writeZ = world && p.blend == OPAQUE;
    // Edge functions and their steps along a row.
    const float d0 = -(C.y - B.y) * inv, d1 = -(A.y - C.y) * inv;
    auto at = [&](float px, float py, float& w0, float& w1, float& w2, float& q) {
        w0 = edge(B.x, B.y, C.x, C.y, px, py) * inv;
        w1 = edge(C.x, C.y, A.x, A.y, px, py) * inv;
        w2 = 1 - w0 - w1;
        q = qa * w0 + qb * w1 + qc * w2;
    };
    auto uvAt = [&](float px, float py, float& u, float& v) {
        float w0, w1, w2, q;
        at(px, py, w0, w1, w2, q);
        const float k = 1 / q;
        u = (A.u * qa * w0 + B.u * qb * w1 + C.u * qc * w2) * k;
        v = (A.v * qa * w0 + B.v * qb * w1 + C.v * qc * w2) * k;
    };
    const float fr = fogR_ * K5, fg = fogG_ * K5, fbl = fogB_ * K5;
    for (int y = y0; y <= y1; y++) {
        const float py = y + 0.5f;
        float w0, w1, w2, q;
        at(x0 + 0.5f, py, w0, w1, w2, q);
        // Solve for the span where all three edge functions are positive, rather than walking the whole box.
        int lo = x0, hi = x1;
        const float d2 = -d0 - d1;
        for (auto [a, d] : {std::pair<float, float>{w0, d0}, {w1, d1}, {w2, d2}}) {
            if (d > 1e-9f) lo = std::max(lo, x0 + int(std::ceil(-a / d)) - 1);
            else if (d < -1e-9f) hi = std::min(hi, x0 + int(std::floor(a / -d)) + 1);
            else if (a < 0) lo = hi + 1;
        }
        if (lo > hi) continue;
        w0 += d0 * float(lo - x0), w1 += d1 * float(lo - x0);
        uint32_t* row = &out_[size_t(y) * fbW_];
        float* zrow = &zb_[size_t(y) * fbW_];
        int level = -1;  // chosen at the row's first pixel, from how fast the texture moves across the screen
        const uint16_t* lt = nullptr;
        int lw = 0, lh = 0;
        float lscale = 1;
        for (int x = lo; x <= hi; x++, w0 += d0, w1 += d1) {
            w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const float px = x + 0.5f;
            q = qa * w0 + qb * w1 + qc * w2;
            if (world && q < zrow[x]) continue;  // something nearer is already here
            const float k = 1 / q, pa = qa * w0 * k, pb = qb * w1 * k, pc = qc * w2 * k;
            float r = (A.r * pa + B.r * pb + C.r * pc) / 128.0f;
            float g = (A.g * pa + B.g * pb + C.g * pc) / 128.0f;
            float b = (A.b * pa + B.b * pb + C.b * pc) / 128.0f;
            float cr = 255, cg = 255, cb = 255;
            if (t) {
                const float u = A.u * pa + B.u * pb + C.u * pc, v = A.v * pa + B.v * pb + C.v * pc;
                if (level < 0) {
                    level = 0;
                    if (filter && t->levels > 1) {
                        float ux, vx, uy, vy;
                        uvAt(px + 1, py, ux, vx);
                        uvAt(px, py + 1, uy, vy);
                        const float rho = std::max(std::hypot(ux - u, vx - v), std::hypot(uy - u, vy - v));
                        if (rho > 1) level = std::min(t->levels - 1, int(std::log2(rho)));
                    }
                    lt = &ram_[t->off], lw = t->w, lh = t->h;
                    for (int l = 0; l < level; l++) {
                        lt += size_t(lw) * lh;
                        lw = std::max(1, lw / 2), lh = std::max(1, lh / 2);
                    }
                    lscale = 1.0f / float(1 << level);
                }
                auto fetch = [&](int tu, int tv) {
                    if (t->pow2) tu &= lw - 1, tv &= lh - 1;
                    else tu = std::clamp(tu, 0, lw - 1), tv = std::clamp(tv, 0, lh - 1);
                    return lt[size_t(tv) * lw + tu];
                };
                const float fu = u * lscale, fv = v * lscale;
                if (!filter) {
                    const uint16_t tx = fetch(int(std::floor(fu)), int(std::floor(fv)));
                    if (!(tx & 0x8000)) continue;
                    cr = ((tx >> 10) & 31) * K5, cg = ((tx >> 5) & 31) * K5, cb = (tx & 31) * K5;
                } else {
                    // Bilinear: the four nearest texels, weighted; transparent ones drop out.
                    if (!(fetch(int(std::floor(fu)), int(std::floor(fv))) & 0x8000)) continue;
                    const float gu = fu - 0.5f, gv = fv - 0.5f;
                    const int iu = int(std::floor(gu)), iv = int(std::floor(gv));
                    const float au = gu - iu, av = gv - iv;
                    float sr = 0, sg = 0, sb = 0, sw = 0;
                    for (int k2 = 0; k2 < 4; k2++) {
                        const uint16_t tx = fetch(iu + (k2 & 1), iv + (k2 >> 1));
                        if (!(tx & 0x8000)) continue;
                        const float wt = ((k2 & 1) ? au : 1 - au) * ((k2 >> 1) ? av : 1 - av);
                        sr += ((tx >> 10) & 31) * wt, sg += ((tx >> 5) & 31) * wt, sb += (tx & 31) * wt, sw += wt;
                    }
                    if (sw <= 0) continue;
                    const float n = K5 / sw;
                    cr = sr * n, cg = sg * n, cb = sb * n;
                }
            }
            cr *= r, cg *= g, cb *= b;
            const float f = A.fog * pa + B.fog * pb + C.fog * pc;
            if (f > 0) {
                cr += (fr - cr) * f;
                cg += (fg - cg) * f;
                cb += (fbl - cb) * f;
            }
            int R = std::clamp(int(cr + 0.5f), 0, 255), G = std::clamp(int(cg + 0.5f), 0, 255), Bc = std::clamp(int(cb + 0.5f), 0, 255);
            uint32_t& dst = row[x];
            if (p.blend == HALF) {
                R = (R + int((dst >> 16) & 255)) / 2, G = (G + int((dst >> 8) & 255)) / 2, Bc = (Bc + int(dst & 255)) / 2;
            } else if (p.blend == ADD) {
                R = std::min(255, R + int((dst >> 16) & 255)), G = std::min(255, G + int((dst >> 8) & 255)), Bc = std::min(255, Bc + int(dst & 255));
            }
            dst = 0xff000000u | uint32_t(R) << 16 | uint32_t(G) << 8 | uint32_t(Bc);
            if (writeZ) zrow[x] = q;
        }
    }
}

void GPU::drawBand64(int y0, int y1) {
    for (int y = y0; y < y1; y++) {
        std::fill(out_.begin() + long(y) * fbW_, out_.begin() + long(y + 1) * fbW_, argb15(lineColor[y / scale_] & 0x7fff));
        std::fill(zb_.begin() + long(y) * fbW_, zb_.begin() + long(y + 1) * fbW_, 0.0f);
    }
    // Opaque 3D first, nearest first, so the depth test turns hidden pixels away before any work is done;
    // then everything that blends or sits on the screen (HUD), in ordering-table order.
    for (auto it = prims_.rbegin(); it != prims_.rend(); ++it)
        if (it->blend == OPAQUE && it->v[0].z > 0 && it->v[1].z > 0 && it->v[2].z > 0) raster64(*it, y0, y1);
    for (const Prim& p : prims_)
        if (!(p.blend == OPAQUE && p.v[0].z > 0 && p.v[1].z > 0 && p.v[2].z > 0)) raster64(p, y0, y1);
}

// ---------------------------------------------------------------- the frame

const uint32_t* GPU::draw() {
    // The ordering table: far to near, submission order within a depth.
    std::stable_sort(prims_.begin(), prims_.end(), [](const Prim& a, const Prim& b) { return a.depth > b.depth; });
    if (model_ == Model::S3_64) {
        // Parallel pipelines, each owning a band of the screen: the depth buffer makes the result the same as one.
#ifdef __EMSCRIPTEN__
        drawBand64(0, fbH_);
#else
        const char* pe = std::getenv("S3_PIPES");
        const int n = pe ? std::atoi(pe) : int(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
        std::vector<std::thread> pipes;
        for (int i = 1; i < n; i++) pipes.emplace_back([this, i, n] { drawBand64(fbH_ * i / n, fbH_ * (i + 1) / n); });
        drawBand64(0, fbH_ / n);
        for (auto& th : pipes) th.join();
#endif
    } else {
        for (int y = 0; y < H; y++) std::fill(fb_.begin() + y * W, fb_.begin() + (y + 1) * W, uint16_t(lineColor[y] & 0x7fff));
        for (const Prim& p : prims_) raster(p);
        for (size_t i = 0; i < fb_.size(); i++) out_[i] = argb15(fb_[i]);
    }
    lastPrims_ = int(prims_.size());
    prims_.clear();
    order_ = 0;
    return out_.data();
}

}  // namespace g32
