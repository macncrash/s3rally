// (3) RALLY - venues, special stages and pace notes.
//
// A stage is point to point: a start, two split points and a flying finish.
// It is built from real road geometry: corners have a radius (the pace note
// number comes from it), crests and jumps have a shape the car can fly off,
// and every metre of road has a surface.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rc {

constexpr float U = 250;       // world units per metre
constexpr float SEG = 200;     // segment length in world units (0.8 m)
constexpr float GRAV = 9.81f;  // m/s^2

constexpr int NUM_VENUES = 5;
constexpr int STAGES_PER_VENUE = 3;
constexpr int NUM_STAGES = NUM_VENUES * STAGES_PER_VENUE;

enum Surf : uint8_t { GRAVEL, LOOSE, MUD, SNOW, ICE, TARMAC, WATER, ROCKY, SURF_COUNT };

struct SurfInfo {
    const char* name;
    float mu;      // peak tyre grip (with the right tyres: studs on snow and ice)
    float peak;    // slip angle of peak grip, radians (loose surfaces peak late and stay up)
    float fall;    // how far grip falls past the peak (0 none .. 1 a lot)
    float roll;    // rolling resistance, in g
    int dust;      // 0 none, 1 dust, 2 spray, 3 snow, 4 mud
    bool rough;    // hard on tyres and suspension at speed
};
extern const SurfInfo SURF[SURF_COUNT];

// Objects beside (or on) the road.
enum Obj : uint8_t {
    O_PINE, O_PINE_SNOW, O_BIRCH, O_BUSH, O_ROCK, O_BOULDER, O_SNOWBANK, O_CLIFF, O_LOGS, O_BALE,
    O_OLIVE, O_GUM, O_STONEWALL, O_SPECTATORS, O_MARSHAL, O_BOARD_START, O_BOARD_SPLIT, O_BOARD_FINISH,
    O_BOARD_STOP, O_ROO_SIGN, O_CAUTION_SIGN, O_ARROW_L, O_ARROW_R, O_KM_POST, O_TAPE, O_FENCE,
    O_ANTHILL, O_CYPRESS, O_HUT, O_ROO, O_COUNT
};

struct ObjInfo {
    float h;      // height in metres
    float w;      // half width for collisions in metres (0 = never hit)
    int hit;      // 0 pass through, 1 soft (slows), 2 solid (crash), 3 snowbank (bounces you back)
    bool shadow;  // casts a ground shadow
    bool scene;   // venue palette (true) or common palette (false)
};
extern const ObjInfo OBJ[O_COUNT];

struct Placed {
    Obj type;
    float off;  // lateral position in metres from the road centre (negative is left)
    bool flip;
};

struct Proj {
    float x = 0, y = 0, w = 0, s = 0, z = 0;
};

enum SegFlag : uint8_t { F_JUMP = 1, F_CREST = 2, F_SPLASH = 4, F_BUMPS = 8, F_SPLIT = 16, F_FINISH = 32, F_STOP = 64, F_START = 128 };

struct Segment {
    int i = 0;
    float z1 = 0, z2 = 0;     // along the road, world units
    float y1 = 0, y2 = 0;     // height, world units
    float curve = 0;          // render curvature: lateral world units per segment^2
    float kappa = 0;          // physical curvature, 1/metre (positive turns right)
    float hw = 4;             // road half width, metres
    Surf surf = GRAVEL;
    uint8_t band = 0;
    uint8_t left = 0, right = 0;  // roadside ground (gs::Ground)
    uint8_t flags = 0;
    float safe = 0;           // jump lips: fastest take-off that still lands on the down slope, m/s
    std::vector<Placed> objs;
    Proj p1, p2;
    float clip = 0;
    int vis = -1;
};

// A co-driver's call. The words are said in order; the icons are the strip on screen.
struct Note {
    int seg;                  // where the call is made
    int at;                   // where the thing it describes begins
    std::vector<int> words;   // phrase ids (see PHRASES)
    std::string text;         // shown on screen
    int sev = 0;              // 1..6 corner, 7 hairpin, 0 not a corner
    int dir = 0;              // -1 left, 1 right
    uint8_t mods = 0;         // NoteMod bits
};
enum NoteMod : uint8_t { M_CREST = 1, M_JUMP = 2, M_DONTCUT = 4, M_CAUTION = 8, M_LONG = 16, M_WATER = 32, M_TIGHTENS = 64, M_BUMPS = 128 };

// Everything the co-driver can say. Each is one recorded phrase.
enum Phrase {
    P_LEFT, P_RIGHT, P_ONE, P_TWO, P_THREE, P_FOUR, P_FIVE, P_SIX, P_HAIRPIN, P_SQUARE, P_FLAT,
    P_LONG, P_VERY_LONG, P_TIGHTENS, P_OPENS, P_DONT_CUT, P_KEEP_IN, P_KEEP_OUT, P_CAUTION, P_DOUBLE_CAUTION,
    P_OVER_CREST, P_JUMP, P_BIG_JUMP, P_INTO, P_AND, P_FIFTY, P_ONE_HUNDRED, P_ONE_FIFTY, P_TWO_HUNDRED, P_THREE_HUNDRED,
    P_WATER_SPLASH, P_ICE, P_MUD, P_ROCKS, P_BUMPS, P_NARROWS, P_FLYING_FINISH, P_SPLIT, P_STOP,
    P_TEN_SECONDS, P_FIVE_S, P_FOUR_S, P_THREE_S, P_TWO_S, P_ONE_S, P_GO, P_GOOD_STAGE, P_WELL_DONE, P_OK_WE_LOST_TIME,
    P_CAR_BEHIND, P_CAR_AHEAD, P_KANGAROOS, P_OVER_BRIDGE, P_DIP, P_FLOODWAY, P_SNOW_BANK, P_ONTO_TARMAC, P_ONTO_GRAVEL,
    P_PUNCTURE, P_WE_ARE_OUT, P_COUNT
};
extern const char* PHRASE_TEXT[P_COUNT];

struct Venue {
    const char* name;      // FINLAND
    const char* rally;     // RALLY FINLAND
    const char* character; // short description for the menus
    const char* stages[STAGES_PER_VENUE];
    uint16_t skyTop, skyHorizon, fog;
    float fogNear, fogFar;  // metres
    int backdropFog;
    int weather;            // 0 clear, 1 snow, 2 heat haze
    bool night;
    // Road palettes: up to four surfaces drawn from four palette banks.
    uint16_t road[4][16];
    uint16_t scene[16], far[16], near[16];
    uint16_t dust[2];       // dust colours for this venue's ground
    float width;            // typical road half width, metres
};
const Venue& venue(int v);

struct Split {
    int seg;
    float dist;  // metres from the start line
};

struct Course {
    int venue = 0, index = 0;         // stage index 0..14 = venue * 3 + n
    std::string name;
    std::vector<Segment> segs;
    int N = 0;
    float length = 0;                 // world units, whole track (includes run-up and stop area)
    int startSeg = 0, finishSeg = 0, stopSeg = 0;
    float stageMetres = 0;            // start line to flying finish
    std::vector<Split> splits;        // two intermediate timing points
    std::vector<Note> notes;
    std::vector<std::pair<float, float>> map;  // normalised course plan, one point per segment
    float idealTime = 0;              // a perfect run on this road, seconds
    float heading(int seg) const;     // road direction there, radians (from the plan)
    std::vector<float> headings;
};

Course buildCourse(int stage);
int surfPalette(int venue, Surf s);   // which road palette bank (0-3) draws this surface
int surfStyle(int venue, Surf s);     // road generator texture for it

// The fastest a car can take the road here, m/s (used for the reference time and the AI).
std::vector<float> speedProfile(const Course& c, float grip, float accel, float brake, float top);

}  // namespace rc
