#include "course.h"

#include <algorithm>
#include <cmath>

#include "console/vdp.h"

namespace rc {

namespace {
constexpr uint16_t C(int r, int g, int b) { return uint16_t(((r & 15) << 8) | ((g & 15) << 4) | (b & 15)); }
constexpr float PI = 3.14159265f;
}  // namespace

//                         name       mu    peak   fall  roll   dust rough
const SurfInfo SURF[SURF_COUNT] = {
    /* GRAVEL */ {"GRAVEL", 0.80f, 0.16f, 0.08f, 0.020f, 1, false},
    /* LOOSE  */ {"LOOSE", 0.64f, 0.19f, 0.06f, 0.030f, 1, false},
    /* MUD    */ {"MUD", 0.52f, 0.20f, 0.10f, 0.080f, 4, false},
    /* SNOW   */ {"SNOW", 0.56f, 0.18f, 0.10f, 0.030f, 3, false},
    /* ICE    */ {"ICE", 0.34f, 0.15f, 0.15f, 0.010f, 3, false},
    /* TARMAC */ {"TARMAC", 1.05f, 0.11f, 0.35f, 0.012f, 0, false},
    /* WATER  */ {"WATER", 0.50f, 0.16f, 0.20f, 0.450f, 2, false},
    /* ROCKY  */ {"ROCKY", 0.72f, 0.16f, 0.10f, 0.030f, 1, true},
};

//                          h(m)   w(m)  hit  shadow scene
const ObjInfo OBJ[O_COUNT] = {
    /* PINE        */ {11.0f, 0.35f, 2, true, true},
    /* PINE_SNOW   */ {11.0f, 0.35f, 2, true, true},
    /* BIRCH       */ {9.0f, 0.25f, 2, true, true},
    /* BUSH        */ {1.4f, 0.8f, 1, false, true},
    /* ROCK        */ {0.9f, 0.5f, 2, true, true},
    /* BOULDER     */ {2.4f, 1.1f, 2, true, true},
    /* SNOWBANK    */ {1.1f, 1.2f, 3, false, true},
    /* CLIFF       */ {9.0f, 1.5f, 2, false, true},
    /* LOGS        */ {1.2f, 1.0f, 2, true, true},
    /* BALE        */ {1.1f, 0.8f, 1, true, false},
    /* OLIVE       */ {4.5f, 0.35f, 2, true, true},
    /* GUM         */ {12.0f, 0.4f, 2, true, true},
    /* STONEWALL   */ {1.0f, 1.6f, 2, false, true},
    /* SPECTATORS  */ {1.9f, 0.0f, 0, false, false},
    /* MARSHAL     */ {1.9f, 0.0f, 0, false, false},
    /* BOARD_START */ {3.2f, 0.0f, 0, false, false},
    /* BOARD_SPLIT */ {2.4f, 0.0f, 0, false, false},
    /* BOARD_FINISH*/ {2.6f, 0.0f, 0, false, false},
    /* BOARD_STOP  */ {2.4f, 0.0f, 0, false, false},
    /* ROO_SIGN    */ {2.2f, 0.1f, 1, false, false},
    /* CAUTION_SIGN*/ {2.2f, 0.1f, 1, false, false},
    /* ARROW_L     */ {1.3f, 0.5f, 1, false, false},
    /* ARROW_R     */ {1.3f, 0.5f, 1, false, false},
    /* KM_POST     */ {1.0f, 0.1f, 1, false, false},
    /* TAPE        */ {1.0f, 0.0f, 0, false, false},
    /* FENCE       */ {1.2f, 0.0f, 0, false, true},
    /* ANTHILL     */ {1.6f, 0.6f, 2, true, true},
    /* CYPRESS     */ {10.0f, 0.3f, 2, true, true},
    /* HUT         */ {4.0f, 2.5f, 2, true, true},
    /* ROO         */ {1.6f, 0.0f, 0, false, true},
};

const char* PHRASE_TEXT[P_COUNT] = {
    "left", "right", "one", "two", "three", "four", "five", "six", "hairpin", "square", "flat",
    "long", "very long", "tightens", "opens", "don't cut", "keep in", "keep out", "caution", "double caution",
    "over crest", "jump", "big jump", "into", "and", "fifty", "one hundred", "one fifty", "two hundred", "three hundred",
    "water splash", "ice", "mud", "rocks", "bumps", "narrows", "flying finish", "split", "stop",
    "ten seconds", "five", "four", "three", "two", "one", "go!", "good stage!", "well done. That's quick.", "OK. We lost some time.",
    "car behind!", "car ahead", "watch the kangaroos", "over bridge", "dip", "floodway", "snow bank", "onto tarmac", "onto gravel",
    "puncture!", "that's it. We're out.",
};

// ------------------------------------------------------------ venues

namespace {

Venue makeFinland() {
    Venue v{};
    v.name = "FINLAND";
    v.rally = "RALLY FINLAND";
    v.character = "FAST GRAVEL. BLIND CRESTS. BIG JUMPS.";
    v.stages[0] = "KUUSIMAKI";
    v.stages[1] = "JARVIRANTA";
    v.stages[2] = "HYPPYLA";
    v.skyTop = C(3, 7, 14);
    v.skyHorizon = C(12, 14, 15);
    v.fog = C(11, 13, 14);
    v.fogNear = 70;
    v.fogFar = 300;
    v.backdropFog = 4;
    v.weather = 0;
    v.width = 3.8f;
    const uint16_t gravel[16] = {0, C(3, 7, 2), C(2, 6, 2), C(5, 8, 3), C(9, 8, 5), C(8, 7, 4), C(11, 9, 6), C(10, 8, 5),
                                 C(7, 6, 4), C(9, 7, 5), C(12, 10, 7), C(3, 6, 10), C(2, 5, 9), C(10, 13, 15), C(13, 12, 9), C(13, 11, 8)};
    const uint16_t loose[16] = {0, C(3, 7, 2), C(2, 6, 2), C(5, 8, 3), C(10, 9, 6), C(9, 8, 5), C(12, 11, 8), C(12, 10, 7),
                                C(8, 7, 5), C(10, 8, 6), C(13, 12, 9), C(3, 6, 10), C(2, 5, 9), C(10, 13, 15), C(14, 13, 10), C(14, 13, 11)};
    const uint16_t mud[16] = {0, C(3, 6, 2), C(2, 5, 1), C(4, 7, 2), C(6, 5, 3), C(5, 4, 2), C(6, 4, 2), C(5, 4, 2),
                              C(3, 2, 1), C(4, 3, 1), C(7, 5, 3), C(5, 5, 4), C(4, 4, 3), C(10, 10, 9), C(8, 7, 5), C(8, 6, 4)};
    std::copy(gravel, gravel + 16, v.road[0]);
    std::copy(loose, loose + 16, v.road[1]);
    std::copy(mud, mud + 16, v.road[2]);
    std::copy(gravel, gravel + 16, v.road[3]);
    const uint16_t scene[16] = {0, C(1, 1, 1), C(4, 2, 1), C(7, 5, 3), C(0, 4, 1), C(1, 6, 2), C(3, 9, 3), C(15, 15, 15),
                                C(5, 5, 5), C(8, 8, 7), C(11, 11, 10), C(15, 3, 3), C(9, 7, 4), C(12, 10, 7), C(3, 6, 9), C(8, 12, 4)};
    std::copy(scene, scene + 16, v.scene);
    const uint16_t far[16] = {0, C(15, 15, 15), C(12, 13, 15), C(5, 8, 10), C(4, 6, 9), C(15, 15, 15), C(11, 12, 15),
                              C(4, 8, 7), C(15, 15, 12), C(15, 14, 10), C(10, 12, 13), C(3, 6, 10), C(6, 9, 12), 0, 0, 0};
    std::copy(far, far + 16, v.far);
    const uint16_t near[16] = {0, C(4, 9, 4), C(3, 7, 3), C(0, 4, 2), C(1, 5, 2), C(2, 7, 3), C(3, 6, 10), C(6, 9, 12),
                               C(6, 10, 3), C(4, 8, 2), C(6, 6, 6), C(15, 15, 15), C(1, 3, 1), 0, 0, 0};
    std::copy(near, near + 16, v.near);
    v.dust[0] = C(12, 11, 8);
    v.dust[1] = C(10, 9, 6);
    return v;
}

Venue makeNorway() {
    Venue v{};
    v.name = "NORWAY";
    v.rally = "RALLY NORWAY";
    v.character = "SNOW AND ICE. LEAN ON THE BANKS.";
    v.stages[0] = "FJELLVEG";
    v.stages[1] = "SKOGSLIA";
    v.stages[2] = "ISVANN NIGHT";
    v.skyTop = C(3, 5, 11);
    v.skyHorizon = C(12, 13, 15);
    v.fog = C(12, 13, 15);
    v.fogNear = 40;
    v.fogFar = 220;
    v.backdropFog = 5;
    v.weather = 1;
    v.night = true;  // the third stage runs in the dark
    v.width = 3.3f;
    const uint16_t snow[16] = {0, C(14, 14, 15), C(12, 13, 15), C(10, 11, 13), C(13, 13, 15), C(12, 12, 14), C(13, 13, 14), C(12, 12, 14),
                               C(9, 9, 11), C(10, 11, 13), C(14, 14, 15), C(4, 6, 9), C(3, 5, 8), C(12, 13, 15), C(15, 15, 15), C(15, 15, 15)};
    const uint16_t ice[16] = {0, C(14, 14, 15), C(12, 13, 15), C(10, 11, 13), C(13, 13, 15), C(12, 12, 14), C(10, 12, 14), C(9, 11, 13),
                              C(7, 9, 11), C(8, 10, 12), C(11, 13, 14), C(4, 6, 9), C(3, 5, 8), C(12, 13, 15), C(14, 15, 15), C(12, 14, 15)};
    const uint16_t gravel[16] = {0, C(14, 14, 15), C(12, 13, 15), C(10, 11, 13), C(12, 12, 13), C(11, 11, 12), C(8, 7, 6), C(7, 6, 5),
                                 C(5, 5, 5), C(7, 7, 7), C(12, 12, 13), C(4, 6, 9), C(3, 5, 8), C(12, 13, 15), C(14, 14, 15), C(13, 13, 14)};
    std::copy(snow, snow + 16, v.road[0]);
    std::copy(ice, ice + 16, v.road[1]);
    std::copy(gravel, gravel + 16, v.road[2]);
    std::copy(snow, snow + 16, v.road[3]);
    const uint16_t scene[16] = {0, C(1, 1, 2), C(4, 3, 2), C(6, 5, 3), C(0, 3, 2), C(1, 5, 3), C(2, 7, 4), C(15, 15, 15),
                                C(4, 4, 5), C(7, 7, 8), C(10, 10, 11), C(14, 3, 3), C(11, 12, 14), C(13, 14, 15), C(5, 7, 10), C(12, 13, 15)};
    std::copy(scene, scene + 16, v.scene);
    const uint16_t far[16] = {0, C(15, 15, 15), C(12, 13, 15), C(6, 7, 11), C(4, 5, 9), C(15, 15, 15), C(11, 12, 15),
                              C(7, 9, 12), C(15, 15, 12), C(15, 14, 10), C(10, 12, 14), C(3, 4, 8), C(8, 9, 12), 0, 0, 0};
    std::copy(far, far + 16, v.far);
    const uint16_t near[16] = {0, C(13, 14, 15), C(11, 12, 14), C(0, 3, 2), C(1, 4, 3), C(2, 6, 4), C(5, 7, 10), C(7, 9, 12),
                               C(14, 14, 15), C(11, 12, 14), C(6, 6, 7), C(15, 15, 15), C(2, 3, 4), 0, 0, 0};
    std::copy(near, near + 16, v.near);
    v.dust[0] = C(15, 15, 15);
    v.dust[1] = C(12, 13, 15);
    return v;
}

Venue makeItaly() {
    Venue v{};
    v.name = "ITALY";
    v.rally = "RALLY ITALIA SARDEGNA";
    v.character = "NARROW, ROCKY, TWISTY. STONE WALLS.";
    v.stages[0] = "CALA ROSSA";
    v.stages[1] = "NURAGHE";
    v.stages[2] = "SU MONTE";
    v.skyTop = C(2, 6, 14);
    v.skyHorizon = C(11, 13, 15);
    v.fog = C(13, 13, 12);
    v.fogNear = 50;
    v.fogFar = 240;
    v.backdropFog = 4;
    v.weather = 0;
    v.width = 2.9f;
    const uint16_t gravel[16] = {0, C(7, 8, 3), C(6, 7, 3), C(10, 9, 5), C(12, 11, 8), C(11, 10, 7), C(13, 11, 8), C(12, 10, 7),
                                 C(8, 7, 5), C(11, 9, 6), C(14, 13, 10), C(3, 7, 11), C(2, 6, 10), C(11, 14, 15), C(14, 14, 12), C(14, 13, 11)};
    const uint16_t rocky[16] = {0, C(7, 8, 3), C(6, 7, 3), C(10, 9, 5), C(11, 10, 8), C(10, 9, 7), C(11, 10, 8), C(10, 9, 7),
                                C(6, 5, 4), C(9, 8, 6), C(12, 11, 9), C(3, 7, 11), C(2, 6, 10), C(11, 14, 15), C(14, 14, 12), C(14, 13, 11)};
    const uint16_t tarmac[16] = {0, C(7, 8, 3), C(6, 7, 3), C(10, 9, 5), C(12, 11, 8), C(11, 10, 7), C(5, 5, 6), C(4, 4, 5),
                                 C(3, 3, 4), C(6, 6, 6), C(6, 6, 7), C(3, 7, 11), C(2, 6, 10), C(11, 14, 15), C(14, 14, 14), C(7, 7, 8)};
    std::copy(gravel, gravel + 16, v.road[0]);
    std::copy(rocky, rocky + 16, v.road[1]);
    std::copy(tarmac, tarmac + 16, v.road[2]);
    std::copy(gravel, gravel + 16, v.road[3]);
    const uint16_t scene[16] = {0, C(1, 1, 1), C(5, 3, 2), C(8, 6, 3), C(3, 5, 2), C(5, 7, 3), C(8, 9, 4), C(15, 15, 14),
                                C(8, 7, 6), C(11, 10, 8), C(14, 13, 11), C(14, 4, 3), C(12, 10, 6), C(14, 12, 8), C(3, 7, 12), C(11, 11, 6)};
    std::copy(scene, scene + 16, v.scene);
    const uint16_t far[16] = {0, C(15, 15, 15), C(12, 13, 15), C(9, 9, 8), C(7, 7, 7), C(15, 15, 15), C(11, 12, 15),
                              C(8, 10, 8), C(15, 15, 12), C(15, 14, 10), C(12, 13, 13), C(2, 6, 12), C(4, 9, 14), 0, 0, 0};
    std::copy(far, far + 16, v.far);
    const uint16_t near[16] = {0, C(8, 9, 4), C(6, 7, 3), C(3, 5, 2), C(4, 6, 2), C(6, 8, 3), C(2, 6, 12), C(5, 9, 14),
                               C(12, 11, 8), C(10, 9, 6), C(9, 8, 7), C(15, 15, 15), C(3, 3, 2), 0, 0, 0};
    std::copy(near, near + 16, v.near);
    v.dust[0] = C(14, 13, 10);
    v.dust[1] = C(12, 11, 8);
    return v;
}

Venue makeAustralia() {
    Venue v{};
    v.name = "AUSTRALIA";
    v.rally = "RALLY AUSTRALIA";
    v.character = "RED DIRT. MARBLES. FLOODWAYS. ROOS.";
    v.stages[0] = "RED GUM CREEK";
    v.stages[1] = "BULLDUST FLAT";
    v.stages[2] = "SUNDOWN RIDGE";
    v.skyTop = C(2, 6, 14);
    v.skyHorizon = C(13, 14, 15);
    v.fog = C(14, 12, 10);
    v.fogNear = 80;
    v.fogFar = 320;
    v.backdropFog = 3;
    v.weather = 0;
    v.width = 4.2f;
    const uint16_t red[16] = {0, C(10, 8, 4), C(9, 6, 3), C(7, 7, 3), C(12, 7, 4), C(11, 6, 3), C(12, 6, 3), C(11, 5, 3),
                              C(7, 3, 2), C(10, 5, 3), C(13, 7, 4), C(7, 6, 4), C(6, 5, 3), C(12, 11, 9), C(14, 9, 6), C(14, 9, 6)};
    const uint16_t marbles[16] = {0, C(10, 8, 4), C(9, 6, 3), C(7, 7, 3), C(12, 7, 4), C(11, 6, 3), C(13, 7, 4), C(13, 7, 4),
                                  C(9, 4, 2), C(11, 6, 3), C(14, 8, 5), C(7, 6, 4), C(6, 5, 3), C(12, 11, 9), C(15, 10, 7), C(15, 10, 7)};
    std::copy(red, red + 16, v.road[0]);
    std::copy(marbles, marbles + 16, v.road[1]);
    std::copy(red, red + 16, v.road[2]);
    std::copy(red, red + 16, v.road[3]);
    const uint16_t scene[16] = {0, C(1, 1, 1), C(6, 4, 3), C(10, 8, 6), C(4, 6, 3), C(6, 8, 4), C(9, 10, 6), C(15, 15, 14),
                                C(8, 4, 2), C(11, 6, 3), C(13, 8, 5), C(15, 5, 3), C(12, 9, 5), C(14, 11, 7), C(6, 6, 5), C(12, 12, 6)};
    std::copy(scene, scene + 16, v.scene);
    const uint16_t far[16] = {0, C(15, 15, 15), C(12, 13, 15), C(12, 6, 4), C(10, 5, 3), C(14, 10, 7), C(12, 8, 5),
                              C(11, 8, 5), C(15, 15, 12), C(15, 14, 9), C(14, 12, 10), C(9, 4, 3), C(15, 9, 5), 0, 0, 0};
    std::copy(far, far + 16, v.far);
    const uint16_t near[16] = {0, C(11, 8, 4), C(9, 6, 3), C(4, 6, 3), C(5, 7, 3), C(7, 9, 4), C(4, 8, 12), C(8, 11, 14),
                               C(13, 7, 4), C(11, 6, 3), C(10, 6, 4), C(15, 15, 15), C(5, 3, 2), 0, 0, 0};
    std::copy(near, near + 16, v.near);
    v.dust[0] = C(14, 8, 5);
    v.dust[1] = C(11, 6, 4);
    return v;
}

Venue makeCyprus() {
    Venue v{};
    v.name = "CYPRUS";
    v.rally = "CYPRUS RALLY";
    v.character = "ROUGH MOUNTAIN ROADS. SHEER DROPS.";
    v.stages[0] = "TROODOS PASS";
    v.stages[1] = "KALOPETRA";
    v.stages[2] = "ELIA TERRACES";
    v.skyTop = C(3, 7, 15);
    v.skyHorizon = C(13, 13, 14);
    v.fog = C(13, 12, 11);
    v.fogNear = 50;
    v.fogFar = 260;
    v.backdropFog = 4;
    v.weather = 2;
    v.width = 2.9f;
    const uint16_t rocky[16] = {0, C(8, 8, 4), C(7, 6, 4), C(10, 9, 6), C(10, 9, 7), C(9, 8, 6), C(10, 9, 7), C(9, 8, 6),
                                C(5, 4, 3), C(8, 7, 5), C(11, 10, 8), C(3, 6, 10), C(2, 5, 9), C(11, 13, 15), C(13, 12, 10), C(13, 12, 10)};
    const uint16_t gravel[16] = {0, C(8, 8, 4), C(7, 6, 4), C(10, 9, 6), C(11, 10, 7), C(10, 9, 6), C(12, 10, 7), C(11, 9, 6),
                                 C(7, 6, 4), C(10, 8, 5), C(13, 11, 8), C(3, 6, 10), C(2, 5, 9), C(11, 13, 15), C(14, 13, 10), C(14, 12, 9)};
    const uint16_t tarmac[16] = {0, C(8, 8, 4), C(7, 6, 4), C(10, 9, 6), C(11, 10, 7), C(10, 9, 6), C(5, 5, 6), C(4, 4, 5),
                                 C(3, 3, 4), C(6, 6, 6), C(6, 6, 7), C(3, 6, 10), C(2, 5, 9), C(11, 13, 15), C(14, 14, 14), C(7, 7, 8)};
    std::copy(rocky, rocky + 16, v.road[0]);
    std::copy(gravel, gravel + 16, v.road[1]);
    std::copy(tarmac, tarmac + 16, v.road[2]);
    std::copy(rocky, rocky + 16, v.road[3]);
    const uint16_t scene[16] = {0, C(1, 1, 1), C(5, 3, 2), C(8, 6, 3), C(1, 4, 2), C(2, 6, 3), C(5, 8, 3), C(15, 15, 14),
                                C(7, 6, 5), C(10, 9, 7), C(13, 12, 10), C(14, 4, 3), C(11, 9, 6), C(13, 11, 8), C(3, 6, 10), C(10, 11, 5)};
    std::copy(scene, scene + 16, v.scene);
    const uint16_t far[16] = {0, C(15, 15, 15), C(12, 13, 15), C(8, 8, 9), C(6, 6, 8), C(15, 15, 15), C(11, 12, 15),
                              C(9, 9, 9), C(15, 15, 12), C(15, 14, 10), C(12, 12, 13), C(5, 6, 8), C(7, 8, 10), 0, 0, 0};
    std::copy(far, far + 16, v.far);
    const uint16_t near[16] = {0, C(8, 8, 5), C(6, 6, 4), C(1, 4, 2), C(2, 5, 2), C(4, 7, 3), C(3, 6, 10), C(6, 9, 12),
                               C(10, 9, 7), C(8, 7, 5), C(9, 8, 7), C(15, 15, 15), C(3, 3, 3), 0, 0, 0};
    std::copy(near, near + 16, v.near);
    v.dust[0] = C(13, 12, 10);
    v.dust[1] = C(10, 9, 7);
    return v;
}

}  // namespace

const Venue& venue(int v) {
    static const Venue vs[NUM_VENUES] = {makeFinland(), makeNorway(), makeItaly(), makeAustralia(), makeCyprus()};
    return vs[std::clamp(v, 0, NUM_VENUES - 1)];
}

// Which palette bank and texture draws each surface at each venue.
int surfPalette(int v, Surf s) {
    switch (v) {
        case 0: return s == LOOSE ? 1 : s == MUD ? 2 : 0;
        case 1: return s == ICE ? 1 : (s == GRAVEL || s == LOOSE) ? 2 : 0;
        case 2: return s == ROCKY ? 1 : s == TARMAC ? 2 : 0;
        case 3: return s == LOOSE ? 1 : 0;
        default: return s == GRAVEL || s == LOOSE ? 1 : s == TARMAC ? 2 : 0;
    }
}

int surfStyle(int, Surf s) {
    switch (s) {
        case GRAVEL: case LOOSE: return gs::ROAD_RUTS;
        case MUD: return gs::ROAD_MUD;
        case SNOW: return gs::ROAD_SNOW;
        case ICE: return gs::ROAD_ICE;
        case TARMAC: return 1;
        case WATER: return 2;
        default: return gs::ROAD_ROCKY;
    }
}

// ------------------------------------------------------------ building a stage

namespace {

struct Rng {
    uint32_t a;
    float operator()() {
        a += 0x6d2b79f5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (1 | t);
        t ^= t + ((t ^ (t >> 7)) * (61 | t));
        return float((t ^ (t >> 14)) >> 8) / 16777216.0f;
    }
    float range(float a0, float a1) { return a0 + (a1 - a0) * (*this)(); }
    int pick(std::initializer_list<float> weights) {
        float total = 0;
        for (float w : weights) total += w;
        float r = (*this)() * total;
        int i = 0;
        for (float w : weights) {
            if (r < w) return i;
            r -= w;
            i++;
        }
        return int(weights.size()) - 1;
    }
};

// Pace note number to corner radius (metres).
float radiusFor(int sev) {
    static const float R[8] = {0, 17, 27, 42, 65, 100, 160, 9};
    return R[std::clamp(sev, 1, 7)];
}

struct Corner {
    int start, end;  // segments
    int sev, dir;
    float angle;     // degrees
    int tightensTo;  // 0 no
};

struct Event {
    enum Kind { CREST, JUMP, DIP, FLOODWAY, FORD, BUMPS, NARROWS } kind;
    int seg;       // where it begins
    int len;       // segments
    float size;    // metres (height) or strength
};

struct Profile {  // venue character
    float lengthM;
    float straight[2];
    float sevW[8];      // weights for 1..6 and hairpin (index 7); [0] unused
    float jumpsPerKm, crestsPerKm, dipsPerKm, fordsPerKm, bumpsPerKm;
    float hills, hillWave;  // base terrain amplitude and wavelength, metres
    float jumpAngle;    // lip angle, radians (the landing slope is built to suit)
    Surf base, patch1, patch2;
    float patch1Share, patch2Share;
};

Profile profileFor(int v, int n) {
    Profile p{};
    switch (v) {
        case 0:  // Finland
            p = {2900, {25, 160}, {0, 2, 5, 11, 22, 30, 30, 0}, 1.6f, 3.0f, 0.4f, 0.4f, 0.0f, 6, 700, 0.07f, GRAVEL, LOOSE, MUD, 0.18f, 0.08f};
            if (n == 1) { p.patch2Share = 0.18f; p.fordsPerKm = 0.8f; }   // lakeside: wetter
            if (n == 2) { p.jumpsPerKm = 3.0f; p.jumpAngle = 0.09f; }      // the jumps stage
            break;
        case 1:  // Norway
            p = {2500, {20, 110}, {0, 5, 14, 24, 28, 18, 8, 3}, 0.6f, 2.0f, 0.3f, 0.0f, 0.0f, 5, 600, 0.055f, SNOW, ICE, GRAVEL, 0.22f, 0.04f};
            break;
        case 2:  // Italy (Sardinia)
            p = {2200, {6, 70}, {0, 14, 26, 28, 18, 8, 2, 8}, 0.5f, 1.5f, 0.6f, 0.5f, 1.0f, 5, 500, 0.055f, GRAVEL, ROCKY, TARMAC, 0.30f, 0.06f};
            break;
        case 3:  // Australia
            p = {3200, {50, 320}, {0, 2, 5, 12, 20, 30, 31, 1}, 1.2f, 2.0f, 1.2f, 0.0f, 0.6f, 4, 900, 0.065f, GRAVEL, LOOSE, LOOSE, 0.28f, 0.0f};
            break;
        default:  // Cyprus
            p = {2000, {5, 55}, {0, 18, 30, 26, 12, 4, 1, 14}, 0.2f, 1.2f, 0.3f, 0.2f, 2.0f, 9, 700, 0.05f, ROCKY, GRAVEL, TARMAC, 0.30f, 0.08f};
            break;
    }
    return p;
}

// A smooth "bump" from 0 to 1 and back, for crests and dips.
float bell(float t) { return t <= 0 || t >= 1 ? 0 : 0.5f - 0.5f * std::cos(t * 2 * PI); }

}  // namespace

float Course::heading(int seg) const {
    if (headings.empty()) return 0;
    return headings[size_t(std::clamp(seg, 0, int(headings.size()) - 1))];
}

Course buildCourse(int stage) {
    Course c;
    stage = std::clamp(stage, 0, NUM_STAGES - 1);
    c.index = stage;
    c.venue = stage / STAGES_PER_VENUE;
    const int n = stage % STAGES_PER_VENUE;
    const Venue& V = venue(c.venue);
    c.name = V.stages[n];
    const Profile P = profileFor(c.venue, n);
    Rng rnd{uint32_t(1987 + stage * 7919)};
    const float segM = SEG / U;  // metres per segment

    std::vector<float> kap;       // curvature per segment
    std::vector<Corner> corners;
    auto metres = [&]() { return kap.size() * segM; };
    auto straight = [&](float m) {
        const int k = std::max(1, int(m / segM));
        for (int i = 0; i < k; i++) kap.push_back(0);
    };
    auto arc = [&](float k0, float k1, float lenM) {  // curvature ramps from k0 to k1
        const int k = std::max(1, int(lenM / segM));
        for (int i = 0; i < k; i++) kap.push_back(k0 + (k1 - k0) * (i + 0.5f) / k);
    };
    auto corner = [&](int sev, int dir, float deg, int tightensTo) {
        const float R = radiusFor(sev), kc = dir / R;
        const float theta = deg * PI / 180;
        float trans = std::clamp(R * 0.5f, 6.0f, 30.0f);
        Corner cn{int(kap.size()), 0, sev, dir, deg, tightensTo};
        if (tightensTo > 0) {
            // Half the angle at this radius, then tightening to the next.
            const float R2 = radiusFor(tightensTo), k2 = dir / R2;
            float t2 = std::clamp(R2 * 0.5f, 5.0f, 20.0f);
            float half = theta / 2;
            float l1 = std::max(0.0f, half / std::fabs(kc) - trans / 2);
            float l2 = std::max(0.0f, half / std::fabs(k2) - t2 / 2);
            arc(0, kc, trans);
            arc(kc, kc, l1);
            arc(kc, k2, t2 * 0.6f);
            arc(k2, k2, l2);
            arc(k2, 0, t2);
        } else {
            float body = theta / std::fabs(kc) - trans;
            if (body < 0) {
                trans = theta / std::fabs(kc);
                body = 0;
            }
            arc(0, kc, trans);
            if (body > 0) arc(kc, kc, body);
            arc(kc, 0, trans);
        }
        cn.end = int(kap.size());
        corners.push_back(cn);
    };

    // Run-up behind the start line, then the stage.
    straight(40);
    c.startSeg = int(kap.size());
    straight(60);
    int lastDir = rnd() < 0.5f ? -1 : 1;
    int sameDirRun = 0;
    while (metres() - c.startSeg * segM < P.lengthM - 140) {
        // Sometimes corners come straight after each other ("into").
        const bool linked = rnd() < (c.venue == 2 || c.venue == 4 ? 0.45f : 0.25f);
        straight(linked ? rnd.range(0, 12) : rnd.range(P.straight[0], P.straight[1]));
        int sev = 1 + rnd.pick({P.sevW[1], P.sevW[2], P.sevW[3], P.sevW[4], P.sevW[5], P.sevW[6], P.sevW[7]});
        float deg;
        if (sev == 7) deg = rnd.range(150, 185);
        else if (sev >= 5) deg = rnd.range(18, 75);
        else if (sev >= 3) deg = rnd.range(35, 110);
        else deg = rnd.range(60, 125);
        int dir = (rnd() < 0.68f || sameDirRun >= 2) ? -lastDir : lastDir;
        sameDirRun = dir == lastDir ? sameDirRun + 1 : 0;
        lastDir = dir;
        int tight = 0;
        if (sev >= 2 && sev <= 6 && rnd() < 0.12f) tight = std::max(1, sev - (rnd() < 0.5f ? 1 : 2));
        corner(sev, dir, deg, tight);
    }
    straight(90);
    c.finishSeg = int(kap.size());
    straight(110);
    c.stopSeg = int(kap.size());
    straight(60);
    const int N = int(kap.size());
    c.N = N;
    c.length = N * SEG;
    c.stageMetres = (c.finishSeg - c.startSeg) * segM;

    // ---------------------------------------------------------------- terrain height (metres)
    std::vector<float> h(size_t(N) + 1, 0.0f);
    {
        const float ph0 = rnd() * 6.28f, ph1 = rnd() * 6.28f, ph2 = rnd() * 6.28f;
        for (int i = 0; i <= N; i++) {
            const float s = i * segM;
            h[size_t(i)] = P.hills * (0.6f * std::sin(s / P.hillWave * 6.28f + ph0) + 0.3f * std::sin(s / (P.hillWave * 0.43f) * 6.28f + ph1) +
                                      0.1f * std::sin(s / (P.hillWave * 0.17f) * 6.28f + ph2));
        }
    }
    std::vector<Event> events;
    std::vector<Surf> surf(size_t(N), P.base);
    std::vector<uint8_t> flags(size_t(N), 0);
    // Straights long enough for a jump or crest: find them from the curvature.
    struct Run { int a, b; };
    std::vector<Run> straights;
    for (int i = c.startSeg + 60, runStart = -1; i < c.finishSeg - 40; i++) {
        const bool st = std::fabs(kap[size_t(i)]) < 1.0f / 150;
        if (st && runStart < 0) runStart = i;
        if ((!st || i == c.finishSeg - 41) && runStart >= 0) {
            if (i - runStart > 50) straights.push_back({runStart, i});
            runStart = -1;
        }
    }
    const float km = c.stageMetres / 1000;
    auto want = [&](float perKm) {
        float x = perKm * km;
        int k = int(x);
        if (rnd() < x - k) k++;
        return k;
    };
    auto freeAt = [&](int a, int b) {
        for (const Event& e : events)
            if (a < e.seg + e.len + 40 && e.seg < b + 40) return false;
        return true;
    };
    // Jumps: a kicker lip on a straight, a long landing slope after it.
    for (int k = want(P.jumpsPerKm), tries = 0; k > 0 && tries < 60; tries++) {
        if (straights.empty()) break;
        const Run& r = straights[size_t(rnd() * straights.size()) % straights.size()];
        // The landing is built long enough for a committed run at the venue's pace; faster overshoots it.
        const float theta = P.jumpAngle * rnd.range(0.8f, 1.15f);
        const float design = c.venue == 0 || c.venue == 3 ? 36.0f : 30.0f;
        const float flight = 3.7f * theta * design * design / GRAV;  // up and back down relative to a slope falling at 0.85 theta
        const int ramp = int(12 / segM), land = int(std::clamp(flight / 0.7f + 10, 30.0f, 80.0f) / segM);
        if (r.b - r.a < ramp + 20) continue;
        const int lip = r.a + ramp + int(rnd() * float(std::max(1, r.b - r.a - ramp - 10)));
        if (!freeAt(lip - ramp, lip + land) || lip + land + 30 >= c.finishSeg) continue;
        events.push_back({Event::JUMP, lip - ramp, ramp + land, theta});
        k--;
    }
    for (int k = want(P.crestsPerKm), tries = 0; k > 0 && tries < 80; tries++) {
        const int at = c.startSeg + 80 + int(rnd() * float(c.finishSeg - c.startSeg - 160));
        const int len = int(rnd.range(45, 85) / segM);
        if (!freeAt(at, at + len)) continue;
        events.push_back({Event::CREST, at, len, rnd.range(1.0f, 2.6f)});
        k--;
    }
    for (int k = want(P.dipsPerKm), tries = 0; k > 0 && tries < 60; tries++) {
        const int at = c.startSeg + 80 + int(rnd() * float(c.finishSeg - c.startSeg - 160));
        const int len = int(rnd.range(30, 50) / segM);
        if (!freeAt(at, at + len)) continue;
        const bool flood = c.venue == 3 && rnd() < 0.6f;
        events.push_back({flood ? Event::FLOODWAY : Event::DIP, at, len, rnd.range(0.8f, 1.5f)});
        k--;
    }
    for (int k = want(P.fordsPerKm), tries = 0; k > 0 && tries < 60; tries++) {
        const int at = c.startSeg + 80 + int(rnd() * float(c.finishSeg - c.startSeg - 160));
        const int len = int(rnd.range(8, 14) / segM);
        if (!freeAt(at, at + len)) continue;
        events.push_back({Event::FORD, at, len, 0});
        k--;
    }
    for (int k = want(P.bumpsPerKm), tries = 0; k > 0 && tries < 60; tries++) {
        const int at = c.startSeg + 80 + int(rnd() * float(c.finishSeg - c.startSeg - 160));
        const int len = int(rnd.range(40, 90) / segM);
        if (!freeAt(at, at + len)) continue;
        events.push_back({Event::BUMPS, at, len, 0});
        k--;
    }
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.seg < b.seg; });
    // Apply the shapes on top of the terrain.
    std::vector<float> add(size_t(N) + 1, 0.0f);
    std::vector<std::pair<int, float>> jumpSafe;
    for (const Event& e : events) {
        switch (e.kind) {
            case Event::JUMP: {
                const int ramp = int(12 / segM);
                const int lip = e.seg + ramp;
                const float theta = e.size;                   // lip angle
                const float H = theta * ramp * segM / 2;      // the ramp is a parabola rising to it
                for (int i = e.seg; i <= lip; i++) {
                    const float t = float(i - e.seg) / ramp;
                    add[size_t(i)] += H * t * t;
                }
                // Landing: the road falls away at nearly the lip angle, then eases back to level.
                const int land = e.len - ramp;
                const float drop = theta * 0.85f;
                float y = H;
                for (int i = lip + 1; i <= lip + land && i <= N; i++) {
                    const float t = float(i - lip) / land;
                    const float slope = drop * (t < 0.7f ? 1.0f : 1 - (t - 0.7f) / 0.3f);
                    y -= slope * segM;
                    add[size_t(i)] += y;
                }
                // Whatever height is left over stays as a gentle step (the land keeps its shape).
                for (int i = lip + land + 1; i <= N; i++) add[size_t(i)] += y;
                flags[size_t(lip)] |= F_JUMP;
                jumpSafe.push_back({lip, std::sqrt(GRAV * 0.7f * land * segM / (3.7f * theta))});
                break;
            }
            case Event::CREST:
                for (int i = 0; i <= e.len; i++) add[size_t(e.seg + i)] += e.size * bell(float(i) / e.len);
                flags[size_t(e.seg + e.len / 2)] |= F_CREST;
                break;
            case Event::DIP:
            case Event::FLOODWAY:
                for (int i = 0; i <= e.len; i++) add[size_t(e.seg + i)] -= e.size * bell(float(i) / e.len);
                if (e.kind == Event::FLOODWAY)
                    for (int i = e.len / 2 - 5; i < e.len / 2 + 5; i++) surf[size_t(e.seg + i)] = WATER;
                break;
            case Event::FORD:
                for (int i = 0; i < e.len; i++) surf[size_t(e.seg + i)] = WATER;
                for (int i = 0; i <= e.len; i++) add[size_t(e.seg + i)] -= 0.4f * bell(float(i) / e.len);
                break;
            case Event::BUMPS:
                for (int i = 0; i < e.len; i++) flags[size_t(e.seg + i)] |= F_BUMPS;
                break;
            default:
                break;
        }
    }
    for (int i = 0; i <= N; i++) h[size_t(i)] += add[size_t(i)];
    // Keep the start and stop areas level.
    for (int i = 0; i <= c.startSeg + 20 && i <= N; i++) h[size_t(i)] = h[size_t(c.startSeg + 20)];

    // ---------------------------------------------------------------- surface patches
    auto patch = [&](Surf s, float share) {
        if (share <= 0) return;
        int budget = int(share * (c.finishSeg - c.startSeg));
        for (int tries = 0; budget > 0 && tries < 200; tries++) {
            const int len = int(rnd.range(30, 120) / segM);
            const int at = c.startSeg + 40 + int(rnd() * float(c.finishSeg - c.startSeg - 80 - len));
            bool clash = false;
            for (int i = at; i < at + len; i++) clash |= surf[size_t(i)] == WATER;
            if (clash) continue;
            for (int i = at; i < at + len; i++) surf[size_t(i)] = s;
            budget -= len;
        }
    };
    patch(P.patch1, P.patch1Share);
    patch(P.patch2, P.patch2Share);

    // ---------------------------------------------------------------- segments
    c.segs.resize(size_t(N));
    float hwBase = V.width;
    for (int i = 0; i < N; i++) {
        Segment& s = c.segs[size_t(i)];
        s.i = i;
        s.z1 = i * SEG;
        s.z2 = (i + 1) * SEG;
        s.y1 = h[size_t(i)] * U;
        s.y2 = h[size_t(i + 1)] * U;
        s.kappa = kap[size_t(i)];
        s.curve = s.kappa * SEG * SEG / U;
        s.surf = surf[size_t(i)];
        s.flags = flags[size_t(i)];
        s.band = uint8_t((i / 5) % 2);
        // Width breathes slowly; tarmac villages are wider, hairpins get a little extra.
        s.hw = hwBase * (1 + 0.08f * std::sin(i * 0.013f + stage)) + (std::fabs(s.kappa) > 1 / 12.0f ? 0.6f : 0) + (s.surf == TARMAC ? 0.4f : 0);
        s.left = s.right = gs::GROUND_LAND;
    }
    for (auto& js : jumpSafe) c.segs[size_t(js.first)].safe = js.second;
    c.segs[size_t(c.startSeg)].flags |= F_START;
    c.segs[size_t(c.finishSeg)].flags |= F_FINISH;
    c.segs[size_t(c.stopSeg)].flags |= F_STOP;
    // Split points at about one third and two thirds.
    for (int k = 1; k <= 2; k++) {
        int at = c.startSeg + (c.finishSeg - c.startSeg) * k / 3;
        // Move it off a corner if it can (split boards stand on straights).
        for (int d = 0; d < 80; d++)
            if (std::fabs(kap[size_t(at + d)]) < 1 / 200.0f) { at += d; break; }
        c.segs[size_t(at)].flags |= F_SPLIT;
        c.splits.push_back({at, (at - c.startSeg) * segM});
    }

    // Roadside ground: snow walls in Norway, lakes in Finland, drops in the mountains.
    for (const Corner& cn : corners) {
        const int outside = -cn.dir;  // the outside of the corner
        if (c.venue == 4 && cn.sev <= 4 && rnd() < 0.55f) {
            for (int i = cn.start - 10; i < cn.end + 10; i++)
                if (i >= 0 && i < N) (outside < 0 ? c.segs[size_t(i)].left : c.segs[size_t(i)].right) = gs::GROUND_DROP;
        }
        if (c.venue == 2 && cn.sev >= 3 && cn.sev <= 5 && rnd() < 0.15f) {
            for (int i = cn.start; i < cn.end; i++)
                if (i >= 0 && i < N) (outside < 0 ? c.segs[size_t(i)].left : c.segs[size_t(i)].right) = gs::GROUND_DROP;
        }
    }
    if (c.venue == 1) {
        for (int i = 0; i < N; i++) c.segs[size_t(i)].left = c.segs[size_t(i)].right = gs::GROUND_SNOWWALL;
    }
    if (c.venue == 0 && n == 1) {  // Jarviranta: the lake along one side for long stretches
        for (int i = c.startSeg; i < c.finishSeg; i++)
            if (std::sin(i * segM / 380.0f) > 0.35f) c.segs[size_t(i)].right = gs::GROUND_WATER;
    }

    // ---------------------------------------------------------------- scenery
    auto put = [&](int i, Obj type, float off, bool flip = false) {
        if (i < 0 || i >= N) return;
        Segment& s = c.segs[size_t(i)];
        const uint8_t g = off < 0 ? s.left : s.right;
        if (g == gs::GROUND_WATER || (g == gs::GROUND_DROP && OBJ[type].hit >= 2 && std::fabs(off) > s.hw + 0.3f)) return;
        s.objs.push_back({type, off, flip});
    };
    auto side = [&]() { return rnd() < 0.5f ? -1.0f : 1.0f; };
    for (int i = 0; i < N; i++) {
        const Segment& s = c.segs[size_t(i)];
        const float e = s.hw;  // road edge
        const float r = rnd();
        switch (c.venue) {
            case 0:  // Finland: pine and birch forest close to the road
                if (i % 4 == 0) put(i, O_PINE, -(e + rnd.range(3, 6)), rnd() < 0.5f);
                if (i % 4 == 2) put(i, O_PINE, e + rnd.range(3, 6), rnd() < 0.5f);
                if (i % 5 == 1) put(i, rnd() < 0.5f ? O_BIRCH : O_PINE, side() * (e + rnd.range(6, 16)), rnd() < 0.5f);
                if (r < 0.03f) put(i, O_ROCK, side() * (e + rnd.range(0.8f, 2.5f)));
                if (r > 0.992f) put(i, O_LOGS, side() * (e + rnd.range(2, 4)));
                break;
            case 1:  // Norway: snowbanks lining the road, snowy pines behind
                if (i % 3 == 0) put(i, O_SNOWBANK, -(e + 0.9f), rnd() < 0.5f);
                if (i % 3 == 1) put(i, O_SNOWBANK, e + 0.9f, rnd() < 0.5f);
                if (i % 4 == 2) put(i, O_PINE_SNOW, side() * (e + rnd.range(4, 12)), rnd() < 0.5f);
                break;
            case 2:  // Italy: maquis, olive trees, rocks, stone walls
                if (r < 0.10f) put(i, O_BUSH, side() * (e + rnd.range(1, 4)), rnd() < 0.5f);
                if (i % 9 == 0) put(i, O_OLIVE, side() * (e + rnd.range(3, 10)), rnd() < 0.5f);
                if (r > 0.97f) put(i, O_ROCK, side() * (e + rnd.range(0.6f, 2)));
                if (i % 23 == 0 && rnd() < 0.4f) put(i, O_CYPRESS, side() * (e + rnd.range(5, 12)));
                break;
            case 3:  // Australia: gum trees, spinifex, termite mounds
                if (i % 8 == 0) put(i, O_GUM, side() * (e + rnd.range(4, 14)), rnd() < 0.5f);
                if (r < 0.07f) put(i, O_BUSH, side() * (e + rnd.range(1.5f, 6)), rnd() < 0.5f);
                if (r > 0.985f) put(i, O_ANTHILL, side() * (e + rnd.range(2, 6)));
                break;
            default: {  // Cyprus: pines above, rock on the inside, the drop below
                const float uphill = s.left == gs::GROUND_DROP ? 1.0f : s.right == gs::GROUND_DROP ? -1.0f : side();
                if (i % 5 == 0) put(i, O_PINE, uphill * (e + rnd.range(3, 9)), rnd() < 0.5f);
                if (i % 17 == 3) put(i, O_CLIFF, uphill * (e + rnd.range(3.5f, 6)), rnd() < 0.5f);
                if (r < 0.05f) put(i, O_ROCK, uphill * (e + rnd.range(0.6f, 2)));
                if (r > 0.96f) put(i, O_BUSH, side() * (e + rnd.range(1, 4)));
                break;
            }
        }
    }
    // Stone walls through Italian and Cypriot villages (the tarmac bits).
    if (c.venue == 2 || c.venue == 4) {
        for (int i = 0; i < N; i++)
            if (c.segs[size_t(i)].surf == TARMAC && i % 3 == 0) {
                put(i, O_STONEWALL, -(c.segs[size_t(i)].hw + 1.4f));
                put(i, O_STONEWALL, c.segs[size_t(i)].hw + 1.4f);
                if (i % 30 == 0) put(i, O_HUT, side() * (c.segs[size_t(i)].hw + 6));
            }
    }
    // Corners: arrow boards on the outside of slow ones, rocks on the inside where you'd cut.
    std::vector<uint8_t> cutRock(corners.size(), 0), dropOut(corners.size(), 0);
    for (size_t k = 0; k < corners.size(); k++) {
        const Corner& cn = corners[k];
        const float outside = float(-cn.dir);
        const int apex = (cn.start + cn.end) / 2;
        if (cn.sev <= 2 || cn.sev == 7)
            for (int i = cn.start; i < cn.end; i += 8) put(i, cn.dir > 0 ? O_ARROW_R : O_ARROW_L, outside * (c.segs[size_t(i)].hw + 1.2f));
        const bool rocky = c.venue == 2 || c.venue == 4 || (c.venue == 0 && cn.sev <= 3);
        if (rocky && rnd() < 0.45f) {
            for (int i = apex - 6; i <= apex + 6; i += 3) put(i, O_ROCK, -outside * (c.segs[size_t(std::clamp(i, 0, N - 1))].hw + 0.5f));
            cutRock[k] = 1;
        }
        const Segment& ap = c.segs[size_t(std::clamp(apex, 0, N - 1))];
        dropOut[k] = (outside < 0 ? ap.left : ap.right) == gs::GROUND_DROP;
    }
    // Spectators where the action is: jumps, crests, hairpins. Never close to the line.
    for (const Event& ev : events)
        if (ev.kind == Event::JUMP || ev.kind == Event::CREST) {
            const int at = ev.seg + ev.len / 3;
            for (int i = at; i < at + 30; i += 6) {
                put(i, O_SPECTATORS, -(c.segs[size_t(i)].hw + 6 + rnd() * 2), rnd() < 0.5f);
                put(i + 3, O_SPECTATORS, c.segs[size_t(i)].hw + 6 + rnd() * 2, rnd() < 0.5f);
            }
            put(at - 4, O_TAPE, -(c.segs[size_t(at)].hw + 4.5f));
            put(at - 4, O_TAPE, c.segs[size_t(at)].hw + 4.5f, true);
        }
    for (const Corner& cn : corners)
        if (cn.sev == 7 && rnd() < 0.6f)
            for (int i = cn.start; i < cn.end; i += 7) put(i, O_SPECTATORS, float(-cn.dir) * (c.segs[size_t(i)].hw + 7 + rnd() * 3), rnd() < 0.5f);
    // Kangaroo warning signs, and sometimes a mob grazing by the road.
    if (c.venue == 3)
        for (int i = c.startSeg + 200; i < c.finishSeg - 100; i += 500 + int(rnd() * 400)) {
            put(i, O_ROO_SIGN, c.segs[size_t(i)].hw + 1.5f);
            for (int k = 0; k < 3; k++) put(i + 80 + k * 4, O_ROO, side() * (c.segs[size_t(i)].hw + rnd.range(5, 12)), rnd() < 0.5f);
        }
    // Kilometre posts.
    for (int km1 = 1; km1 * 1000 < c.stageMetres; km1++) put(c.startSeg + int(km1 * 1000 / segM), O_KM_POST, -(c.segs[size_t(c.startSeg)].hw + 0.8f));

    // Start, splits, flying finish and stop control: clear the verges, put up the boards.
    auto clear = [&](int at, int before, int after) {
        for (int i = at - before; i < at + after; i++) {
            if (i < 0 || i >= N) continue;
            auto& o = c.segs[size_t(i)].objs;
            o.erase(std::remove_if(o.begin(), o.end(), [&](const Placed& p) { return std::fabs(p.off) < c.segs[size_t(i)].hw + 5; }), o.end());
        }
    };
    clear(c.startSeg, 30, 30);
    put(c.startSeg + 1, O_BOARD_START, -(c.segs[size_t(c.startSeg)].hw + 1.5f));
    put(c.startSeg + 1, O_MARSHAL, c.segs[size_t(c.startSeg)].hw + 1.2f, true);
    put(c.startSeg - 12, O_SPECTATORS, -(c.segs[size_t(c.startSeg)].hw + 5));
    put(c.startSeg - 8, O_SPECTATORS, c.segs[size_t(c.startSeg)].hw + 5, true);
    for (const Split& sp : c.splits) {
        clear(sp.seg, 8, 8);
        put(sp.seg, O_BOARD_SPLIT, -(c.segs[size_t(sp.seg)].hw + 1.5f));
        put(sp.seg, O_BOARD_SPLIT, c.segs[size_t(sp.seg)].hw + 1.5f);
    }
    clear(c.finishSeg, 40, 200);
    put(c.finishSeg - 125, O_CAUTION_SIGN, c.segs[size_t(c.finishSeg)].hw + 1.5f);  // "finish ahead"
    put(c.finishSeg, O_BOARD_FINISH, -(c.segs[size_t(c.finishSeg)].hw + 1.5f));
    put(c.finishSeg, O_BOARD_FINISH, c.segs[size_t(c.finishSeg)].hw + 1.5f);
    for (int i = c.finishSeg + 6; i < c.finishSeg + 40; i += 7) {
        put(i, O_SPECTATORS, -(c.segs[size_t(i)].hw + 5), rnd() < 0.5f);
        put(i + 3, O_SPECTATORS, c.segs[size_t(i)].hw + 5, rnd() < 0.5f);
    }
    put(c.stopSeg, O_BOARD_STOP, -(c.segs[size_t(c.stopSeg)].hw + 1.5f));
    put(c.stopSeg + 2, O_MARSHAL, c.segs[size_t(c.stopSeg)].hw + 1.2f, true);

    // ---------------------------------------------------------------- the plan (map) and headings
    c.headings.resize(size_t(N));
    c.map.resize(size_t(N));
    {
        std::vector<std::pair<double, double>> pts(static_cast<size_t>(N));
        double hd = 0, x = 0, y = 0;
        for (int i = 0; i < N; i++) {
            pts[size_t(i)] = {x, y};
            c.headings[size_t(i)] = float(hd);
            hd += kap[size_t(i)] * segM;
            x += std::sin(hd) * segM;
            y -= std::cos(hd) * segM;
        }
        double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
        for (auto& p : pts) {
            minX = std::min(minX, p.first); maxX = std::max(maxX, p.first);
            minY = std::min(minY, p.second); maxY = std::max(maxY, p.second);
        }
        const double span = std::max({maxX - minX, maxY - minY, 1.0});
        for (int i = 0; i < N; i++)
            c.map[size_t(i)] = {float((pts[size_t(i)].first - minX) / span + (1 - (maxX - minX) / span) / 2),
                                float((pts[size_t(i)].second - minY) / span + (1 - (maxY - minY) / span) / 2)};
    }

    // ---------------------------------------------------------------- pace notes
    // Everything the co-driver will mention, in road order.
    struct Item { int at, end; bool corner; size_t k; const Event* ev; };
    std::vector<Item> items;
    for (size_t k = 0; k < corners.size(); k++) items.push_back({corners[k].start, corners[k].end, true, k, nullptr});
    for (const Event& ev : events)
        items.push_back({ev.kind == Event::JUMP ? ev.seg + int(12 / segM) : ev.seg + (ev.kind == Event::CREST ? ev.len / 2 : 0),
                                                         ev.seg + ev.len, false, 0, &ev});
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.at < b.at; });
    // Surface changes worth a call.
    for (int i = c.startSeg + 20; i < c.finishSeg; i++) {
        const Surf a = surf[size_t(i - 1)], b = surf[size_t(i)];
        if (a == b || b == WATER || a == WATER) continue;
        Note note;
        note.at = i;
        if (b == ICE) note.words = {P_ICE}, note.text = "ICE", note.mods = M_CAUTION;
        else if (b == MUD) note.words = {P_MUD}, note.text = "MUD";
        else if (b == TARMAC) note.words = {P_ONTO_TARMAC}, note.text = "TARMAC";
        else if (a == TARMAC) note.words = {P_ONTO_GRAVEL}, note.text = "GRAVEL";
        else if (b == ROCKY && c.venue != 4) note.words = {P_ROCKS}, note.text = "ROCKS", note.mods = M_CAUTION;
        else continue;
        note.seg = i;
        c.notes.push_back(note);
    }
    for (size_t j = 0; j < items.size(); j++) {
        const Item& it = items[j];
        Note note;
        note.at = it.at;
        std::vector<int>& w = note.words;
        std::string& t = note.text;
        if (it.corner) {
            const Corner& cn = corners[it.k];
            note.sev = cn.sev;
            note.dir = cn.dir;
            // Numbers first ("left four"), hairpins by name.
            w.push_back(cn.dir < 0 ? P_LEFT : P_RIGHT);
            t = cn.dir < 0 ? "L" : "R";
            if (cn.sev == 7) {
                w.push_back(P_HAIRPIN);
                t += " HAIRPIN";
            } else {
                w.push_back(P_ONE + cn.sev - 1);
                t += " " + std::to_string(cn.sev);
            }
            if (cn.sev != 7 && cn.angle > 100) { w.push_back(P_LONG); t += " LONG"; note.mods |= M_LONG; }
            if (cn.tightensTo) { w.push_back(P_TIGHTENS); t += " TIGHTENS"; note.mods |= M_TIGHTENS; }
            // Something sitting in the corner (a crest or jump) gets said with it.
            for (size_t q = j + 1; q < items.size() && items[q].at < cn.end; q++)
                if (!items[q].corner && items[q].ev && (items[q].ev->kind == Event::CREST || items[q].ev->kind == Event::JUMP)) {
                    w.push_back(items[q].ev->kind == Event::JUMP ? P_JUMP : P_OVER_CREST);
                    t += items[q].ev->kind == Event::JUMP ? " JUMP" : " CREST";
                    note.mods |= items[q].ev->kind == Event::JUMP ? M_JUMP : M_CREST;
                }
            if (cutRock[it.k]) { w.push_back(P_DONT_CUT); t += " DON'T CUT"; note.mods |= M_DONTCUT; }
            if (dropOut[it.k]) { w.push_back(cn.sev <= 3 ? P_DOUBLE_CAUTION : P_CAUTION); t += cn.sev <= 3 ? " !!" : " !"; note.mods |= M_CAUTION; }
        } else {
            const Event& ev = *it.ev;
            // Skip crests and jumps already called as part of a corner.
            bool inCorner = false;
            for (const Corner& cn : corners) inCorner |= it.at >= cn.start && it.at < cn.end;
            if (inCorner && (ev.kind == Event::CREST || ev.kind == Event::JUMP)) continue;
            switch (ev.kind) {
                case Event::JUMP:
                    w.push_back(ev.size > P.jumpAngle * 1.08f ? P_BIG_JUMP : P_JUMP);
                    t = ev.size > P.jumpAngle * 1.08f ? "BIG JUMP" : "JUMP";
                    note.mods = M_JUMP;
                    break;
                case Event::CREST: w.push_back(P_OVER_CREST); t = "CREST"; note.mods = M_CREST; break;
                case Event::DIP: w.push_back(P_DIP); t = "DIP"; break;
                case Event::FLOODWAY: w.push_back(P_FLOODWAY); t = "FLOODWAY"; note.mods = M_WATER; break;
                case Event::FORD: w.push_back(P_WATER_SPLASH); t = "WATER"; note.mods = M_WATER; break;
                case Event::BUMPS: w.push_back(P_BUMPS); t = "BUMPS"; note.mods = M_BUMPS; break;
                default: continue;
            }
        }
        // Link to what comes next: "into" if it's right there, a distance if it's a way off.
        if (j + 1 < items.size()) {
            const float gap = (items[j + 1].at - it.end) * segM;
            if (gap < 20) { w.push_back(P_INTO); t += " >"; }
            else if (gap < 45 && it.corner) { w.push_back(P_AND); }
            else if (gap >= 90) {
                const int d = gap >= 280 ? P_THREE_HUNDRED : gap >= 190 ? P_TWO_HUNDRED : gap >= 140 ? P_ONE_FIFTY : P_ONE_HUNDRED;
                w.push_back(d);
                t += d == P_THREE_HUNDRED ? " 300" : d == P_TWO_HUNDRED ? " 200" : d == P_ONE_FIFTY ? " 150" : " 100";
            }
        }
        note.seg = it.at;
        c.notes.push_back(note);
    }
    // The finish.
    {
        Note f;
        f.at = c.finishSeg;
        f.words = {P_FLYING_FINISH};
        f.text = "FLYING FINISH";
        f.seg = c.finishSeg;
        c.notes.push_back(f);
    }
    std::sort(c.notes.begin(), c.notes.end(), [](const Note& a, const Note& b) { return a.at < b.at; });

    // When to read each call: about three seconds ahead at a good pace.
    const std::vector<float> vp = speedProfile(c, 1.0f, 7.0f, 8.0f, 55.0f);
    for (Note& nt : c.notes) {
        const float v = vp[size_t(std::clamp(nt.at, 0, N - 1))];
        const float lead = (2.6f + 0.25f * float(nt.words.size())) * std::max(v, 12.0f);
        nt.seg = std::max(c.startSeg - 5, nt.at - int(lead / segM));
    }

    // Reference time: a flawless run.
    {
        float t = 0;
        for (int i = c.startSeg; i < c.finishSeg; i++) t += segM / std::max(vp[size_t(i)], 1.0f);
        c.idealTime = t;
    }
    return c;
}

std::vector<float> speedProfile(const Course& c, float grip, float accel, float brake, float top) {
    const int N = c.N;
    const float segM = SEG / U;
    std::vector<float> v(size_t(N), top);
    for (int i = 0; i < N; i++) {
        const Segment& s = c.segs[size_t(i)];
        const float mu = SURF[s.surf].mu * grip;
        const float k = std::fabs(s.kappa);
        if (k > 1e-5f) v[size_t(i)] = std::min(top, std::sqrt(mu * GRAV * 1.12f / k));  // a bit over the limit: cars slide
        if (s.surf == WATER) v[size_t(i)] = std::min(v[size_t(i)], 16.0f);
        if (s.safe > 0) v[size_t(i)] = std::min(v[size_t(i)], s.safe);
    }
    v[size_t(c.startSeg)] = 0;
    for (int i = c.startSeg + 1; i < N; i++) {  // accelerating out
        const float mu = SURF[c.segs[size_t(i)].surf].mu * grip;
        const float a = std::min(accel, mu * GRAV) * (1 - v[size_t(i - 1)] / (top * 1.15f));
        v[size_t(i)] = std::min(v[size_t(i)], std::sqrt(v[size_t(i - 1)] * v[size_t(i - 1)] + 2 * std::max(a, 0.3f) * segM));
    }
    for (int i = N - 2; i >= 0; i--) {  // braking in
        const float mu = SURF[c.segs[size_t(i)].surf].mu * grip;
        const float b = std::min(brake, mu * GRAV);
        v[size_t(i)] = std::min(v[size_t(i)], std::sqrt(v[size_t(i + 1)] * v[size_t(i + 1)] + 2 * b * segM));
    }
    return v;
}

}  // namespace rc
