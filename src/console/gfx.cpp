#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

namespace gs {

void Bitmap::rect(float x, float y, float rw, float rh, int c) {
    int x0 = std::max(0, int(std::lround(x))), y0 = std::max(0, int(std::lround(y)));
    int x1 = std::min(w, int(std::lround(x + rw))), y1 = std::min(h, int(std::lround(y + rh)));
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) px[size_t(j) * w + i] = uint8_t(c);
}

void Bitmap::poly(const std::vector<Pt>& pts, int c) {
    float minY = 1e9f, maxY = -1e9f;
    for (auto& p : pts) {
        minY = std::min(minY, p.second);
        maxY = std::max(maxY, p.second);
    }
    std::vector<float> xs;
    for (int y = std::max(0, int(std::floor(minY))); y <= std::min(h - 1, int(std::ceil(maxY))); y++) {
        float sy = y + 0.5f;
        xs.clear();
        for (size_t i = 0; i < pts.size(); i++) {
            const Pt& a = pts[i];
            const Pt& b = pts[(i + 1) % pts.size()];
            if ((a.second <= sy && b.second > sy) || (b.second <= sy && a.second > sy))
                xs.push_back(a.first + (sy - a.second) / (b.second - a.second) * (b.first - a.first));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2) {
            int x0 = std::max(0, int(std::lround(xs[i]))), x1 = std::min(w, int(std::lround(xs[i + 1])));
            for (int x = x0; x < x1; x++) px[size_t(y) * w + x] = uint8_t(c);
        }
    }
}

void Bitmap::ellipse(float cx, float cy, float rx, float ry, int c) {
    for (int y = int(std::floor(cy - ry)); y <= int(std::ceil(cy + ry)); y++) {
        float dy = (y + 0.5f - cy) / ry;
        if (std::fabs(dy) > 1) continue;
        float hw = rx * std::sqrt(1 - dy * dy);
        for (int x = int(std::lround(cx - hw)); x < int(std::lround(cx + hw)); x++) set(x, y, c);
    }
}

void Bitmap::line(float x0, float y0, float x1, float y1, int c, float thick) {
    int n = int(std::ceil(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)))) + 1;
    for (int i = 0; i <= n; i++) {
        float x = x0 + (x1 - x0) * i / n, y = y0 + (y1 - y0) * i / n;
        if (thick <= 1) set(int(x), int(y), c);
        else rect(x - thick / 2, y - thick / 2, thick, thick, c);
    }
}

void Bitmap::blit(const Bitmap& src, int dx, int dy) {
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++) {
            int v = src.px[size_t(y) * src.w + x];
            if (v) set(dx + x, dy + y, v);
        }
}

void Bitmap::outline(int c, bool diagonal) {
    std::vector<uint8_t> src = px;
    auto g = [&](int x, int y) { return (x >= 0 && y >= 0 && x < w && y < h) ? src[size_t(y) * w + x] : 0; };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (src[size_t(y) * w + x]) continue;
            bool hit = g(x - 1, y) || g(x + 1, y) || g(x, y - 1) || g(x, y + 1);
            if (!hit && diagonal) hit = g(x - 1, y - 1) || g(x + 1, y - 1) || g(x - 1, y + 1) || g(x + 1, y + 1);
            if (hit) px[size_t(y) * w + x] = uint8_t(c);
        }
}

Bitmap Bitmap::resample(int nw, int nh) const {
    Bitmap out(std::max(1, nw), std::max(1, nh));
    float sx = float(w) / out.w, sy = float(h) / out.h;
    int counts[16];
    for (int y = 0; y < out.h; y++)
        for (int x = 0; x < out.w; x++) {
            int x0 = int(x * sx), x1 = std::max(x0 + 1, int((x + 1) * sx));
            int y0 = int(y * sy), y1 = std::max(y0 + 1, int((y + 1) * sy));
            std::memset(counts, 0, sizeof counts);
            for (int j = y0; j < y1; j++)
                for (int i = x0; i < x1; i++) counts[get(i, j) & 15]++;
            int best = 0, bc = 0;
            for (int k = 1; k < 16; k++)
                if (counts[k] > bc) {
                    bc = counts[k];
                    best = k;
                }
            int total = (x1 - x0) * (y1 - y0);
            out.px[size_t(y) * out.w + x] = uint8_t(counts[0] > total * 0.55f ? 0 : best);
        }
    return out;
}

Bitmap Bitmap::cropToContent(int pad) const {
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (px[size_t(y) * w + x]) {
                x0 = std::min(x0, x); y0 = std::min(y0, y);
                x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
    if (x1 < 0) return Bitmap(1, 1);
    Bitmap out(x1 - x0 + 1 + pad * 2, y1 - y0 + 1 + pad * 2);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) out.set(x - x0 + pad, y - y0 + pad, get(x, y));
    return out;
}

// ---------------------------------------------------------------- font

static const std::map<char, const char*>& fontData() {
    static const std::map<char, const char*> G = {
        {'A', ".###.#...##...#######...##...##...#"}, {'B', "####.#...##...#####.#...##...#####."},
        {'C', ".###.#...##....#....#....#...#.###."}, {'D', "####.#...##...##...##...##...#####."},
        {'E', "######....#....####.#....#....#####"}, {'F', "######....#....####.#....#....#...."},
        {'G', ".###.#...##....#.####...##...#.####"}, {'H', "#...##...##...#######...##...##...#"},
        {'I', ".###...#....#....#....#....#...###."}, {'J', "..###...#....#....#....#.#..#..##.."},
        {'K', "#...##..#.#.#..##...#.#..#..#.#...#"}, {'L', "#....#....#....#....#....#....#####"},
        {'M', "#...###.###.#.##.#.##...##...##...#"}, {'N', "#...##...###..##.#.##..###...##...#"},
        {'O', ".###.#...##...##...##...##...#.###."}, {'P', "####.#...##...#####.#....#....#...."},
        {'Q', ".###.#...##...##...##.#.##..#..##.#"}, {'R', "####.#...##...#####.#.#..#..#.#...#"},
        {'S', ".#####....#.....###.....#....#####."}, {'T', "#####..#....#....#....#....#....#.."},
        {'U', "#...##...##...##...##...##...#.###."}, {'V', "#...##...##...##...##...#.#.#...#.."},
        {'W', "#...##...##...##.#.##.#.##.#.#.#.#."}, {'X', "#...##...#.#.#...#...#.#.#...##...#"},
        {'Y', "#...##...#.#.#...#....#....#....#.."}, {'Z', "#####....#...#...#...#...#....#####"},
        {'0', ".###.#...##..###.#.###..##...#.###."}, {'1', "..#...##....#....#....#....#...###."},
        {'2', ".###.#...#....#...#...#...#...#####"}, {'3', "#####...#...#.....#.....##...#.###."},
        {'4', "...#...##..#.#.#..#.#####...#....#."}, {'5', "######....####.....#....##...#.###."},
        {'6', "..##..#...#....####.#...##...#.###."}, {'7', "#####....#...#...#...#....#....#..."},
        {'8', ".###.#...##...#.###.#...##...#.###."}, {'9', ".###.#...##...#.####....#...#..##.."},
        {' ', "..................................."}, {'.', ".........................##...##..."},
        {',', "....................##....#...#...."}, {':', "......##...##.........##...##......"},
        {'/', ".........#...#...#...#...#........."}, {'-', "...............###................."},
        {'+', ".......#....#..#####..#....#......."}, {'!', "..#....#....#....#....#.........#.."},
        {'?', ".###.#...#....#...#...#.........#.."}, {'\'', "..#....#...#......................."},
        {'"', ".#.#..#.#.........................."}, {'(', "...#...#...#....#....#.....#.....#."},
        {')', ".#.....#.....#....#....#...#...#..."}, {'>', ".#.....#.....#.....#...#...#...#..."},
        {'<', "...#...#...#...#.....#.....#.....#."}, {'%', "##..###.#....#...#...#....#.###..##"},
        {'#', ".#.#.#####.#.#..#.#..#.#.#####.#.#."}, {'*', ".....#.#.#.###.#####.###.#.#.#....."},
        {'@', ".###.#...##.####.#.##.####....#####"}, {'=', "..........#####.....#####.........."},
    };
    return G;
}

const uint8_t* glyph(char ch) {
    static std::map<char, std::vector<uint8_t>> cache;
    if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);
    auto it = cache.find(ch);
    if (it != cache.end()) return it->second.data();
    const auto& G = fontData();
    auto g = G.find(ch);
    const char* src = g != G.end() ? g->second : G.at('?');
    std::vector<uint8_t> v(35);
    for (int i = 0; i < 35; i++) v[i] = src[i] == '#';
    return (cache[ch] = v).data();
}

Bitmap textBitmap(const std::string& s, const TextStyle& st) {
    int pad = st.outline ? 1 : 0;
    int cw = (5 + st.spacing) * st.scale;
    int w = std::max(1, int(s.size()) * cw - st.spacing * st.scale + pad * 2 + (st.shadow ? 1 : 0));
    int h = 7 * st.scale + pad * 2 + (st.shadow ? 1 : 0);
    Bitmap b(w, h);
    auto draw = [&](int ox, int oy, int c) {
        for (size_t i = 0; i < s.size(); i++) {
            const uint8_t* g = glyph(s[i]);
            for (int y = 0; y < 7; y++)
                for (int x = 0; x < 5; x++)
                    if (g[y * 5 + x]) b.rect(float(ox + int(i) * cw + x * st.scale), float(oy + y * st.scale), float(st.scale), float(st.scale), c);
        }
    };
    if (st.shadow) draw(pad + 1, pad + 1, st.shadow);
    draw(pad, pad, st.color);
    if (st.outline) b.outline(st.outline, true);
    return b;
}

// ---------------------------------------------------------------- uploads

Image uploadImage(VDP& vdp, const Bitmap& b) {
    Image img = vdp.allocImage(b.w, b.h);
    std::memcpy(vdp.rom() + img.off, b.px.data(), b.px.size());
    return img;
}

const Image& Mipped::pick(float dstH) const {
    // Use the smallest level that is still at least as tall as the output.
    if (dstH <= lv[2].h) return lv[2];
    if (dstH <= lv[1].h) return lv[1];
    return lv[0];
}

Mipped uploadMipped(VDP& vdp, const Bitmap& b) {
    Mipped m;
    m.w = b.w;
    m.h = b.h;
    m.lv[0] = uploadImage(vdp, b);
    m.lv[1] = uploadImage(vdp, b.resample(std::max(1, b.w / 2), std::max(1, b.h / 2)));
    m.lv[2] = uploadImage(vdp, b.resample(std::max(1, b.w / 4), std::max(1, b.h / 4)));
    return m;
}

int TileAlloc::alloc(int n) {
    if (next_ + n > NUM_TILES) throw std::runtime_error("VRAM full");
    int t = next_;
    next_ += n;
    return t;
}

int TileAlloc::shared(const uint8_t* px) {
    bool empty = true;
    for (int i = 0; i < 64; i++)
        if (px[i]) {
            empty = false;
            break;
        }
    if (empty) return 0;
    std::string key(reinterpret_cast<const char*>(px), 64);
    auto it = dedupe_.find(key);
    if (it != dedupe_.end()) return it->second;
    int t = alloc(1);
    vdp_.loadTile(t, px);
    dedupe_[key] = t;
    return t;
}

void bitmapToPlane(TileAlloc& a, Plane& p, int cx, int cy, const Bitmap& b, int pal) {
    int tw = (b.w + 7) / 8, th = (b.h + 7) / 8;
    uint8_t px[64];
    for (int ty = 0; ty < th; ty++)
        for (int tx = 0; tx < tw; tx++) {
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) px[y * 8 + x] = uint8_t(b.get(tx * 8 + x, ty * 8 + y));
            int t = a.shared(px);
            p.set(cx + tx, cy + ty, t ? entry(t, pal) : 0);
        }
}

}  // namespace gs
