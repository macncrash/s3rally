// S3 RALLY - procedurally generated graphics.
#pragma once
#include "console/gfx.h"
#include "console/vdp.h"
#include "stages.h"

namespace rally {

enum Palette {
    PAL_HUD = 0, PAL_FAR = 1, PAL_NEAR = 2, PAL_SCENE = 3, PAL_COMMON = 4, PAL_PLAYER = 5, PAL_RIVAL = 6,
    PAL_FX = 9, PAL_LOGO = 10, PAL_RED = 11, PAL_ROAD = 12, PAL_TARMAC = 13, PAL_ALT = 14, PAL_YELLOW = 15
};
constexpr int NUM_RIVAL_PALS = 3;

// Plane rows that sit on the horizon line, and the band that holds clouds.
constexpr int BACKDROP_BASE = 250;
constexpr int CLOUD_ROWS = 120;

struct CarSpec {
    const char* name;
    const char* drive;
    float accel, top, grip;
    uint16_t livery[16];
};
const CarSpec& carSpec(int i);
constexpr int NUM_CARS = 2;

struct Art {
    gs::Mipped obj[O_COUNT];
    gs::Mipped car[5];  // yaw 0 (straight) .. 4 (full slide)
    gs::Mipped shadow, puff, flake, splash, spray;
    gs::Mipped glyph[96];  // big font, ASCII 32..127
    gs::Mipped pace[6];
    gs::Mipped logo;
    gs::Mipped rpm[4];  // green, yellow, red, off
    gs::Mipped flame, streak;  // turbo effects
    gs::Mipped panel, panelWide;  // HUD backings (drawn as shadow sprites)
    gs::Image map;
    int fontTile[96] = {};
    int firstFreeTile = 1;
};

void buildArt(gs::VDP& vdp, Art& art);
int loadStage(gs::VDP& vdp, const Art& art, int stage);  // palettes + backdrop planes; returns tiles used
void setCarPalette(gs::VDP& vdp, int pal, int car);
void setRivalPalettes(gs::VDP& vdp);

}  // namespace rally
