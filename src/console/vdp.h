// S3-16 video hardware.
//
// A 16-bit tile/sprite VDP extended with the two custom chips that made the
// mid-80s "super scaler" arcade boards special:
//   * a sprite scaler: any sprite can be drawn at any size from sprite ROM
//   * a road generator: a dedicated layer rendering a textured road from
//     per-scanline parameters (centre, width, texture distance, palette bank)
// plus per-line backdrop colour, per-line distance fog and shadow sprites.
//
// Layer order, back to front:
//   backdrop (per line) < Plane B < Plane A < Road < shadows < sprites < HUD
#pragma once
#include <cstdint>
#include <vector>

namespace gs {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 224;
constexpr int NUM_TILES = 8192;
constexpr int NUM_PALETTES = 16;
constexpr int NUM_SPRITES = 256;
constexpr int SPRITE_ROM_SIZE = 24 << 20;
constexpr int FOG_LEVELS = 17;

// Colours are 12-bit RGB (4:4:4), 4096 colours.
inline uint16_t rgb4(int r, int g, int b) { return uint16_t(((r & 15) << 8) | ((g & 15) << 4) | (b & 15)); }
uint32_t rgb4ToArgb(uint16_t c);

// Nametable entry: 13-bit tile, h/v flip, 4-bit palette.
inline uint32_t entry(int tile, int pal, int hf = 0, int vf = 0) {
    return uint32_t(tile & 0x1fff) | uint32_t(hf) << 13 | uint32_t(vf) << 14 | uint32_t(pal & 15) << 15;
}

struct Plane {
    int w = 64, h = 32;  // in cells, powers of two
    std::vector<uint32_t> map;
    int16_t hscroll[SCREEN_H] = {};
    int16_t vscroll[SCREEN_H] = {};
    bool enabled = true;
    Plane() { resize(64, 32); }
    void resize(int cw, int ch);
    void set(int cx, int cy, uint32_t e) { map[(cy & (h - 1)) * w + (cx & (w - 1))] = e; }
    uint32_t get(int cx, int cy) const { return map[(cy & (h - 1)) * w + (cx & (w - 1))]; }
    void clear() { std::fill(map.begin(), map.end(), 0u); }
    void scroll(int hs, int vs);
};

// An image in sprite ROM (4-bit pixels, one per byte, 0 = transparent).
struct Image {
    uint32_t off = 0;
    uint16_t w = 0, h = 0;
};

struct Sprite {
    int16_t x = 0, y = 0, w = 0, h = 0;  // destination rectangle (scaled)
    Image img;                           // source
    uint8_t pal = 0;
    uint8_t fog = 0;         // 0..16 blend towards the fog colour
    bool hflip = false;
    bool shadow = false;     // darken what is underneath instead of drawing
    int16_t clipY = SCREEN_H;  // rows at or below this screen line are not drawn
};

// Road generator revision B: rally surface textures and roadside ground types.
enum RoadStyle : uint8_t { ROAD_RUTS = 4, ROAD_MUD = 5, ROAD_ICE = 6, ROAD_SNOW = 7, ROAD_ROCKY = 8 };
enum Ground : uint8_t { GROUND_LAND = 0, GROUND_WATER = 1, GROUND_SNOWWALL = 2, GROUND_DROP = 3 };

// Road generator parameters for one scanline.
struct RoadLine {
    bool on = false;
    float cx = 160;   // screen x of road centre
    float hw = 0;     // road half-width in pixels
    float v = 0;      // texture distance coordinate (world units)
    uint8_t pal = 12; // palette bank for this line's surface
    uint8_t band = 0; // light/dark band
    uint8_t style = 0; // 0 dirt, 1 tarmac (centre line), 2 water, 3 snow; rev B adds ROAD_RUTS..ROAD_ROCKY
    uint8_t left = 0, right = 0; // ground: 0 grass/sand/snow, 1 water, GROUND_SNOWWALL, GROUND_DROP
};

class VDP {
public:
    VDP();
    void reset();

    // Palette RAM
    void setColor(int i, uint16_t c);
    uint16_t color(int i) const { return cram_[i]; }
    void setFogColor(uint16_t c);

    // Tiles (8x8, 4-bit)
    void loadTile(int index, const uint8_t* px64);

    // Sprite ROM
    uint8_t* rom() { return rom_.data(); }
    Image allocImage(int w, int h);  // reserve space; caller fills pixels
    uint32_t romUsed() const { return romTop_; }

    // Sprites: earlier entries are drawn on top.
    void clearSprites() { spriteCount_ = 0; }
    bool sprite(const Sprite& s);
    int spriteCount() const { return spriteCount_; }

    void render(uint32_t* fb);

    Plane A, B, HUD;
    RoadLine road[SCREEN_H];
    uint16_t lineBackdrop[SCREEN_H] = {};
    uint8_t lineFog[SCREEN_H] = {};
    int roadTime = 0;  // animates water
    bool hudEnabled = true;

private:
    void rebuildLut();
    void planeLine(const Plane& p, int y, int hs, int vs, uint16_t* out) const;
    void roadLine(int y, uint16_t* out) const;
    int rallySurface(int style, float u, float au, int uc, int vc, int vcw, int band, bool detail, float v) const;
    void spriteLine(int y);

    std::vector<uint8_t> tiles_;
    std::vector<uint8_t> rom_;
    uint32_t romTop_ = 0;
    uint16_t cram_[NUM_PALETTES * 16] = {};
    uint16_t fogColor_ = 0;
    uint32_t lut_[FOG_LEVELS][NUM_PALETTES * 16];
    bool lutDirty_ = true;
    Sprite sprites_[NUM_SPRITES];
    int spriteCount_ = 0;

    // Line buffers: colour index (pal*16+pix), 0 = transparent.
    uint16_t bLine_[SCREEN_W], aLine_[SCREEN_W], rLine_[SCREEN_W], hLine_[SCREEN_W];
    uint16_t sLine_[SCREEN_W];
    uint8_t sFog_[SCREEN_W];
    uint8_t shadow_[SCREEN_W];
};

}  // namespace gs
