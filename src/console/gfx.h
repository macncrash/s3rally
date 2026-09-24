// S3-16 SDK: indexed bitmaps, the system font, and helpers to move art
// into VRAM tiles or sprite ROM.
#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vdp.h"

namespace gs {

using Pt = std::pair<float, float>;

struct Bitmap {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    Bitmap() = default;
    Bitmap(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * h_, 0) {}
    void set(int x, int y, int c) {
        if (x >= 0 && y >= 0 && x < w && y < h) px[size_t(y) * w + x] = uint8_t(c);
    }
    int get(int x, int y) const { return (x >= 0 && y >= 0 && x < w && y < h) ? px[size_t(y) * w + x] : 0; }
    void rect(float x, float y, float rw, float rh, int c);
    void poly(const std::vector<Pt>& pts, int c);
    void ellipse(float cx, float cy, float rx, float ry, int c);
    void line(float x0, float y0, float x1, float y1, int c, float thick = 1);
    void blit(const Bitmap& src, int dx, int dy);
    void outline(int c, bool diagonal = false);
    Bitmap resample(int nw, int nh) const;  // mode filter, keeps pixel art crisp
    Bitmap cropToContent(int pad = 0) const;
};

// System font (5x7).
const uint8_t* glyph(char ch);
struct TextStyle {
    int scale = 1, color = 1, outline = 0, shadow = 0, spacing = 1;
};
Bitmap textBitmap(const std::string& s, const TextStyle& st);

// Sprite ROM uploads. A mipmapped image stores full, 1/2 and 1/4 sizes so the
// scaler can sample a level close to the on-screen size (less shimmer).
Image uploadImage(VDP& vdp, const Bitmap& b);
struct Mipped {
    Image lv[3];
    int w = 0, h = 0;
    const Image& pick(float dstH) const;
};
Mipped uploadMipped(VDP& vdp, const Bitmap& b);

// VRAM tiles with deduplication. Tile 0 is the blank tile.
class TileAlloc {
public:
    explicit TileAlloc(VDP& v, int start = 1) : vdp_(v), next_(start) {}
    int alloc(int n);
    int shared(const uint8_t* px64);
    int used() const { return next_; }

private:
    VDP& vdp_;
    int next_;
    std::unordered_map<std::string, int> dedupe_;
};
void bitmapToPlane(TileAlloc& a, Plane& p, int cx, int cy, const Bitmap& b, int pal);

}  // namespace gs
