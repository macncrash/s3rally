// S3 RALLY CHAMPIONSHIP - graphics, generated when the cartridge boots.
#pragma once
#include "car.h"
#include "carmodel.h"
#include "console/gfx.h"
#include "console/vdp.h"
#include "course.h"

namespace rc {

enum Palette {
    PAL_HUD = 0, PAL_FAR = 1, PAL_NEAR = 2, PAL_SCENE = 3, PAL_COMMON = 4, PAL_PLAYER = 5, PAL_RIVAL = 6,
    PAL_ROAD4 = 7, PAL_RIVAL2 = 8, PAL_FX = 9, PAL_LOGO = 10, PAL_RED = 11, PAL_ROAD1 = 12, PAL_ROAD2 = 13, PAL_ROAD3 = 14,
    PAL_YELLOW = 15
};
constexpr int PAL_RIVAL3 = PAL_LOGO;  // a third opponent's livery borrows the logo palette during a race
constexpr int ROAD_PALS[4] = {PAL_ROAD1, PAL_ROAD2, PAL_ROAD3, PAL_ROAD4};

// HUD palette roles: 1 white, 2 light grey, 3 white, 4 red, 5 green, 6 orange, 7 blue, 8 dark grey,
// 9 cyan, 10 grey, 11 black, 12-14 cockpit trim (dark to light), 15 text shadow.

constexpr int BACKDROP_BASE = 250;
constexpr int CLOUD_ROWS = 120;
constexpr int WHEEL_FRAMES = 33;   // steering wheel, -240 to +240 degrees
constexpr int NEEDLE_FRAMES = 28;  // rev counter needle
constexpr int NUM_ICONS = 8;       // corner icons: 1..6, hairpin, straight

enum ModIcon { MI_CREST, MI_JUMP, MI_WATER, MI_CAUTION, MI_DONTCUT, MI_TIGHTENS, MI_BUMPS, MI_FINISH, MI_COUNT };
enum DamageIcon { DI_ENGINE, DI_SUSPENSION, DI_TYRES, DI_BODY, DI_COUNT };

struct Art {
    gs::Mipped obj[O_COUNT];
    gs::Mipped car[YAW_FRAMES][PITCH_FRAMES];
    gs::Mipped carRoll[2][ROLL_FRAMES];  // rolling over, seen from behind and from the side
    gs::Mipped shadow, puff, haze, flake, splash, spray, clod, spark;
    gs::Mipped glyph[96];
    int fontTile[96] = {};
    int firstFreeTile = 1;
    gs::Mipped icon[NUM_ICONS], mod[MI_COUNT], damage[DI_COUNT];
    gs::Mipped logo;
    gs::Mipped rpm[4];
    gs::Mipped panel, panelWide;
    // Cockpit
    gs::Mipped dash, wheel[WHEEL_FRAMES], needle[NEEDLE_FRAMES], pillar, roofBar, mirror, codriver, dirt[3], wiper[3], gloves;
    gs::Image map;
};

void buildArt(gs::VDP& vdp, Art& art);
int loadVenue(gs::VDP& vdp, const Art& art, int venue, int timeOfDay);  // palettes and backdrop planes; returns tiles used
void setCarPalette(gs::VDP& vdp, int pal, int car);
void setLivery(gs::VDP& vdp, int pal, int livery);  // AI crews: a colour scheme on the first car's shape
constexpr int NUM_LIVERIES = 8;

}  // namespace rc
