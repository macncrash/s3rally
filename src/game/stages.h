// S3 RALLY - stage definitions and track construction.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rally {

constexpr float SEG = 200;     // segment length (world units)
constexpr float ROADW = 1000;  // road half-width
constexpr int BAND = 3;        // segments per light/dark band
constexpr int NUM_STAGES = 4;

enum Surface : uint8_t { DIRT, TARMAC, SAND, SNOW, FORD };

enum ObjType : uint8_t {
    O_PINE, O_PINE_SNOW, O_BIRCH, O_BUSH, O_DRYBUSH, O_ROCK, O_BOULDER, O_CACTUS, O_PALM, O_REEDS,
    O_SNOWBANK, O_CLIFF, O_LOGS, O_CHEVRON, O_CROWD, O_FLAG, O_ARCH_START, O_ARCH_CP, O_TIRES, O_BALE,
    O_BOAT, O_COUNT
};

struct ObjInfo {
    float h;      // world height
    float w;      // collision half-width in road units
    bool solid;   // crash on contact (otherwise just slows)
    bool shadow;  // cast a ground shadow
    bool scene;   // stage palette (true) or common palette (false)
};
extern const ObjInfo OBJ[O_COUNT];

struct Obj {
    ObjType type;
    float off;  // lateral position in road half-widths
    bool flip;
};

struct Proj {
    float x = 0, y = 0, w = 0, s = 0, z = 0;  // screen x/y, half-width, scale, camera z
};

struct Segment {
    int i = 0;
    float z1 = 0, z2 = 0, y1 = 0, y2 = 0, curve = 0;
    Surface surface = DIRT;
    uint8_t band = 0;
    uint8_t left = 0, right = 0;  // ground type each side: 0 land, 1 water
    std::vector<Obj> objs;
    Proj p1, p2;
    float clip = 0;
    int vis = -1;
};

enum Voice {
    V_EASY_L, V_EASY_R, V_MED_L, V_MED_R, V_HARD_L, V_HARD_R, V_HAIR_L, V_HAIR_R, V_LONG_L, V_LONG_R,
    V_CREST, V_SPLASH, V_DONT_CUT, V_CHECKPOINT, V_EXTENDED, V_FINAL_LAP, V_GAME_OVER, V_CONGRATS,
    V_THREE, V_TWO, V_ONE, V_GO, V_FINISH, V_YOU_WIN, V_MAYBE,
    V_ST_BLADE, V_ST_NEON, V_ST_KOOL, V_TURBO, V_SECRET, V_COUNT
};
extern const char* VOICE_TEXT[V_COUNT];

struct PaceNote {
    int seg;
    int voice;
    int icon;  // 0 easy, 1 medium, 2 hard, 3 hairpin, 4 crest, 5 splash
    int dir;   // -1 left, 1 right, 0 none
    std::string text;
};

struct StageDef {
    const char* name;
    const char* subtitle;
    int laps;
    float startTime;
    float extend;
    uint16_t skyTop, skyHorizon, fog;
    float fogNear, fogFar;  // in segments
    int backdropFog;        // fog level applied to the backdrop planes
    uint16_t roadMain[16], roadTarmac[16], roadAlt[16];
    uint16_t scene[16];  // palette 3
    uint16_t far[16];    // palette 1 (Plane B)
    uint16_t near[16];   // palette 2 (Plane A)
    int weather;         // 0 clear, 1 snow
    int music;
};
const StageDef& stageDef(int i);

struct Track {
    std::vector<Segment> segs;
    int N = 0;
    float length = 0;
    std::vector<int> checkpoints;  // segment indices; [0] is the start line
    std::vector<PaceNote> notes;
    std::vector<std::pair<float, float>> map;  // normalised course map, one point per segment
};

Track buildTrack(int stage);

}  // namespace rally
