#include "vdp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace gs {

uint32_t rgb4ToArgb(uint16_t c) {
    uint32_t r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    return 0xff000000u | (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}

static inline uint32_t hash2(int x, int y) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

void Plane::resize(int cw, int ch) {
    auto pow2 = [](int v) { return v > 0 && (v & (v - 1)) == 0; };
    if (!pow2(cw) || !pow2(ch)) throw std::runtime_error("plane size must be a power of two");
    w = cw;
    h = ch;
    map.assign(size_t(w) * h, 0u);
}

void Plane::scroll(int hs, int vs) {
    for (int y = 0; y < SCREEN_H; y++) {
        hscroll[y] = int16_t(hs);
        vscroll[y] = int16_t(vs);
    }
}

VDP::VDP() : tiles_(size_t(NUM_TILES) * 64), rom_(SPRITE_ROM_SIZE) { reset(); }

void VDP::reset() {
    std::fill(tiles_.begin(), tiles_.end(), 0);
    romTop_ = 0;
    std::memset(cram_, 0, sizeof cram_);
    fogColor_ = 0;
    lutDirty_ = true;
    for (Plane* p : {&A, &B, &HUD}) {
        p->resize(64, 32);
        p->scroll(0, 0);
        p->enabled = true;
    }
    for (auto& r : road) r = RoadLine{};
    std::memset(lineBackdrop, 0, sizeof lineBackdrop);
    std::memset(lineFog, 0, sizeof lineFog);
    spriteCount_ = 0;
    hudEnabled = true;
}

void VDP::setColor(int i, uint16_t c) {
    if (i < 0 || i >= NUM_PALETTES * 16) return;
    if (cram_[i] != c) {
        cram_[i] = c;
        lutDirty_ = true;
    }
}

void VDP::setFogColor(uint16_t c) {
    if (fogColor_ != c) {
        fogColor_ = c;
        lutDirty_ = true;
    }
}

void VDP::loadTile(int index, const uint8_t* px64) {
    if (index < 0 || index >= NUM_TILES) return; std::memcpy(&tiles_[size_t(index) * 64], px64, 64); }

Image VDP::allocImage(int w, int h) {
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535) throw std::runtime_error("bad sprite ROM image size");
    const uint64_t need = uint64_t(w) * uint64_t(h);
    if (need > rom_.size() - romTop_) throw std::runtime_error("sprite ROM full");
    Image img{romTop_, uint16_t(w), uint16_t(h)};
    std::memset(&rom_[romTop_], 0, need);
    romTop_ += uint32_t(need);
    return img;
}

bool VDP::sprite(const Sprite& s) {
    if (spriteCount_ >= NUM_SPRITES) return false;
    if (s.w <= 0 || s.h <= 0 || s.img.w == 0 || s.img.h == 0 || s.x >= SCREEN_W || s.y >= SCREEN_H || s.x + s.w <= 0 || s.y + s.h <= 0) return true;
    sprites_[spriteCount_++] = s;
    return true;
}

void VDP::rebuildLut() {
    int fr = (fogColor_ >> 8) & 15, fg = (fogColor_ >> 4) & 15, fb = fogColor_ & 15;
    for (int f = 0; f < FOG_LEVELS; f++)
        for (int i = 0; i < NUM_PALETTES * 16; i++) {
            uint16_t c = cram_[i];
            int r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
            // Blend in 8-bit space so the fog ramps smoothly.
            int R = (r * 17 * (16 - f) + fr * 17 * f) / 16;
            int G = (g * 17 * (16 - f) + fg * 17 * f) / 16;
            int B = (b * 17 * (16 - f) + fb * 17 * f) / 16;
            lut_[f][i] = 0xff000000u | uint32_t(R) << 16 | uint32_t(G) << 8 | uint32_t(B);
        }
    lutDirty_ = false;
}

void VDP::planeLine(const Plane& p, int y, int hs, int vs, uint16_t* out) const {
    const int wpx = p.w * 8, hpx = p.h * 8, wmask = wpx - 1;
    const int py = (y + vs) & (hpx - 1);
    const int fy = py & 7;
    const uint32_t* row = &p.map[size_t(py >> 3) * p.w];
    int px = (-hs) & wmask;
    int x = 0;
    while (x < SCREEN_W) {
        uint32_t e = row[px >> 3];
        int tile = e & 0x1fff;
        bool hf = (e >> 13) & 1, vf = (e >> 14) & 1;
        uint16_t pal = uint16_t(((e >> 15) & 15) << 4);
        const uint8_t* t = &tiles_[size_t(tile) * 64 + (vf ? 7 - fy : fy) * 8];
        int fx0 = px & 7;
        int n = std::min(8 - fx0, SCREEN_W - x);
        for (int i = 0; i < n; i++, x++) {
            int fx = fx0 + i;
            uint8_t v = t[hf ? 7 - fx : fx];
            out[x] = v ? uint16_t(pal | v) : 0;
        }
        px = (px + n) & wmask;
    }
}

// Road generator revision B: the rally surfaces. u is across the road
// (-1..1 edge to edge), v along it in world units.
int VDP::rallySurface(int style, float u, float au, int uc, int vc, int vcw, int band, bool detail, float v) const {
    const bool track = au > 0.24f && au < 0.52f;  // where the wheels run
    int idx = band ? 6 : 7;
    switch (style) {
        case ROAD_RUTS: {  // gravel: two swept wheel tracks, loose stones in the middle and at the edges
            if (track) idx = 9;
            else if (au < 0.12f) idx = 10;
            if (detail) {
                const uint32_t h = hash2(uc, vc);
                const int every = track ? 23 : au > 0.8f ? 3 : 7;  // tracks swept clean, edges piled up
                if (h % every == 0) idx = (h >> 8) & 1 ? 15 : 8;
            }
            break;
        }
        case ROAD_MUD: {  // mud: dark wet ruts and standing puddles
            if (track) idx = 9;
            const int pu = int(std::floor(u * 2.5f + 8)), pv = int(std::floor(v / 420.0f));
            const uint32_t h = hash2(pu, pv);
            if (h % 5 == 0) {
                // A puddle: an ellipse in its cell.
                const float cu = (u * 2.5f + 8) - float(pu) - 0.5f;
                const float cv = v / 420.0f - float(pv) - 0.5f;
                const float rad = 0.25f + float((h >> 8) % 100) / 500.0f;
                if (cu * cu + cv * cv * 0.6f < rad * rad) {
                    const uint32_t w = hash2(int(std::floor(u * 14)), vcw);
                    idx = w % 9 == 0 ? 13 : (band ? 11 : 12);
                    break;
                }
            }
            if (detail && hash2(uc, vc) % 13 == 0) idx = 8;
            break;
        }
        case ROAD_ICE: {  // sheet ice: long glassy streaks
            if (track) idx = 9;
            const uint32_t h = hash2(int(std::floor(u * 18)), int(std::floor(v / 700.0f)));
            if (h % 4 == 0) idx = 14;
            else if (detail && hash2(uc, vc) % 19 == 0) idx = 15;
            break;
        }
        case ROAD_SNOW: {  // packed snow: polished wheel tracks, fresh snow at the edges
            if (track) {
                idx = 9;
                if (detail && hash2(uc, vc) % 5 == 0) idx = 8;  // tyre tread marks
            } else if (au > 0.86f) {
                idx = 15;
            } else if (detail && hash2(uc, vc) % 17 == 0) {
                idx = 14;
            }
            break;
        }
        case ROAD_ROCKY: {  // rough mountain road: embedded rocks everywhere
            if (track) idx = 9;
            if (detail) {
                const uint32_t big = hash2(int(std::floor(u * 12)), int(std::floor(v / 60.0f)));
                const uint32_t h = hash2(uc, vc);
                if (big % 9 == 0) idx = (big >> 9) & 1 ? 15 : 8;
                else if (h % 6 == 0) idx = (h >> 8) % 3 == 0 ? 15 : 8;
            }
            break;
        }
        default:
            break;
    }
    return idx;
}

// The road generator. Texture indices within the line's palette bank:
//  1-3 ground (light, dark, speck)   4-5 verge   6-7 road   8 pebbles
//  9 tyre tracks  10 centre ridge  11-13 water  14 paint  15 road speck
void VDP::roadLine(int y, uint16_t* out) const {
    const RoadLine& r = road[y];
    if (!r.on) {
        std::memset(out, 0, sizeof(uint16_t) * SCREEN_W);
        return;
    }
    const uint16_t base = uint16_t((r.pal & 15) * 16);
    const float hw = std::max(r.hw, 0.25f);
    const float du = 1.0f / hw;
    float u = (0.5f - r.cx) * du;
    const bool detail = hw > 36;  // texture cells at least ~1px wide
    const int vc = int(std::floor(r.v / 26.0f));
    const int vcw = int(std::floor(r.v / 90.0f)) + roadTime / 5;
    const bool dash = (int(std::floor(r.v / 500.0f)) & 1) != 0;
    const int band = r.band;
    for (int x = 0; x < SCREEN_W; x++, u += du) {
        const float au = std::fabs(u);
        int idx;
        if (au < 1.0f) {
            const int uc = int(std::floor(u * 40));
            if (r.style >= ROAD_RUTS) {
                idx = rallySurface(r.style, u, au, uc, vc, vcw, band, detail, r.v);
            } else if (r.style == 2) {
                uint32_t h = hash2(int(std::floor(u * 14)), vcw);
                idx = (h % 9 == 0) ? 13 : (band ? 11 : 12);
            } else if (r.style == 1) {
                idx = band ? 6 : 7;
                if ((au < 0.022f && dash) || (au > 0.92f && au < 0.95f)) idx = 14;
                else if (detail && hash2(uc, vc) % 13 == 0) idx = 15;
            } else {
                if (au > 0.34f && au < 0.52f) idx = 9;
                else if (au < 0.07f) idx = 10;
                else idx = band ? 6 : 7;
                if (detail) {
                    uint32_t h = hash2(uc, vc);
                    if (h % 11 == 0) idx = 8;
                    else if (h % 17 == 1) idx = 15;
                }
            }
        } else if (au < 1.14f) {
            idx = band ? 4 : 5;
            if (detail && hash2(int(std::floor(u * 40)), vc) % 7 == 0) idx = 8;
        } else {
            const int ground = u < 0 ? r.left : r.right;
            if (ground == GROUND_DROP) {  // the hillside falls away: the backdrop shows through
                out[x] = 0;
                continue;
            }
            if (ground == GROUND_SNOWWALL) {  // ploughed snow banked up beside the road
                idx = au < 1.3f ? 14 : (band ? 1 : 2);
                if (detail && hash2(int(std::floor(u * 30)), vc) % 6 == 0) idx = 3;
            } else if (ground == 1) {
                uint32_t h = hash2(int(std::floor(u * 5)), vcw / 2);
                idx = (h % 7 == 0) ? 13 : (band ? 11 : 12);
            } else {
                idx = band ? 1 : 2;
                if (detail && hash2(int(std::floor(u * 24)), vc) % 9 == 0) idx = 3;
            }
        }
        out[x] = uint16_t(base + idx);
    }
}

void VDP::spriteLine(int y) {
    std::memset(sLine_, 0, sizeof sLine_);
    std::memset(shadow_, 0, sizeof shadow_);
    const uint8_t* rom = rom_.data();
    for (int i = 0; i < spriteCount_; i++) {
        const Sprite& s = sprites_[i];
        if (y < s.y || y >= s.y + s.h || y >= s.clipY) continue;
        const int sy = int(int64_t(y - s.y) * s.img.h / s.h);
        const uint8_t* row = rom + s.img.off + size_t(sy) * s.img.w;
        const int x0 = std::max(0, int(s.x)), x1 = std::min(SCREEN_W, s.x + s.w);
        const int64_t step = (int64_t(s.img.w) << 16) / s.w;
        int64_t fx = int64_t(x0 - s.x) * step + step / 2;
        const uint16_t pal = uint16_t(s.pal << 4);
        const int wm1 = s.img.w - 1;
        for (int x = x0; x < x1; x++, fx += step) {
            int sx = int(fx >> 16);
            if (sx > wm1) sx = wm1;
            if (s.hflip) sx = wm1 - sx;
            uint8_t v = row[sx];
            if (!v) continue;
            if (s.shadow) {
                shadow_[x] = 1;
            } else if (!sLine_[x]) {
                sLine_[x] = uint16_t(pal | v);
                sFog_[x] = s.fog;
            }
        }
    }
}

void VDP::render(uint32_t* fb) {
    if (lutDirty_) rebuildLut();
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t* o = fb + y * SCREEN_W;
        if (B.enabled) planeLine(B, y, B.hscroll[y], B.vscroll[y], bLine_);
        else std::memset(bLine_, 0, sizeof bLine_);
        if (A.enabled) planeLine(A, y, A.hscroll[y], A.vscroll[y], aLine_);
        else std::memset(aLine_, 0, sizeof aLine_);
        roadLine(y, rLine_);
        spriteLine(y);
        if (hudEnabled) planeLine(HUD, y, 0, 0, hLine_);
        else std::memset(hLine_, 0, sizeof hLine_);
        const uint32_t bg = rgb4ToArgb(lineBackdrop[y]);
        const uint32_t* lf = lut_[std::min<int>(lineFog[y], 16)];
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t c;
            if (hLine_[x]) {
                o[x] = lut_[0][hLine_[x]];
                continue;
            }
            if (sLine_[x]) {
                o[x] = lut_[std::min<int>(sFog_[x], 16)][sLine_[x]];
                continue;
            }
            if (rLine_[x]) c = lf[rLine_[x]];
            else if (aLine_[x]) c = lf[aLine_[x]];
            else if (bLine_[x]) c = lf[bLine_[x]];
            else c = bg;
            if (shadow_[x]) c = 0xff000000u | ((c >> 1) & 0x7f7f7fu) + ((c >> 3) & 0x1f1f1fu);
            o[x] = c;
        }
    }
    roadTime++;
}

}  // namespace gs
