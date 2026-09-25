// S3 RALLY CHAMPIONSHIP - the cartridge.
//
// Real rallying: special stages driven against the clock, one car at a time,
// starting ten seconds apart. Five rallies of three stages each, with a
// co-driver reading pace notes, service between stages, and fifteen other
// crews on the time sheets.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "art.h"
#include "car.h"
#include "console/system.h"
#include "course.h"
#include "game/profile.h"
#include "game/radio.h"
#include "game/sound.h"
#include "game/versus.h"
#include "version.h"
#include "voice.h"

namespace rc {

enum class Mode { Title, Menu, Pick, CarSelect, Lobby, Controls, Profile, RallyIntro, StageIntro, Start, Stage, Finish, Result, Service, Standings, Podium };
enum class Game { Championship, Rally, TimeAttack, Online };
enum class View { Chase, Far, Cockpit };
constexpr int REPAIR_MINUTES[4] = {10, 8, 3, 6};  // engine, suspension, tyres, body

struct Crew {
    std::string name, nat;
    float pace = 1;          // 1.00 is the quickest crew
    float home[NUM_VENUES];  // venue specialists are quicker there
    int livery = 0;
    float time[NUM_STAGES] = {};  // stage times, seconds (0 = not run)
    float split[NUM_STAGES][2] = {};
    bool out = false;        // retired from the current rally
    int points = 0;          // championship
    float total = 0;         // current rally
};

// Another car on the stage: an AI crew ahead or behind, or another player online.
struct Other {
    int crew = -1;           // index into crews_, or -1 for a player
    int slot = -1;           // online slot
    float startAt = 0;       // seconds after our start (negative: started before us)
    float stageTime = 0;     // how long they will take (AI)
    float s = 0, x = 0, speed = 0, psi = 0, y = 0;
    bool running = false, finished = false, yielding = false;
    float stopAt = -1, stopFor = 0;  // an AI incident: where and for how long
    int pal = PAL_RIVAL;
    std::string name;
};

struct Particle {
    float x, y, vx, vy, size, grow;
    int life, max, kind;  // 0 dust, 1 gravel, 2 splash, 3 spark, 4 snow, 5 mud
};

struct Cloud {  // dust hanging in the air behind a car, in world space
    float s, x, h, size;
    int life, max;
};

class RallyChamp : public gs::Cart {
public:
    RallyChamp();
    ~RallyChamp() override;
    const char* title() const override { return "S3 RALLY CHAMPIONSHIP " S3_VERSION; }
    void init(gs::System& sys) override;
    void frame(gs::System& sys) override;

    // Headless driving for tests and the daily report.
    struct SimReport {
        int stage;
        bool finished;
        float time, ideal, topKmh, airTime;
        int jumps, crashes, hardLandings, notes;
        float damage;
        int rank;
    };
    SimReport simulateStage(int stage, int car, std::vector<std::string>* shots, const std::string& shotDir);
    float romUsedMB() const;
    int tilesUsed() const { return tilesUsed_; }
    void testProfile(const std::string& name);
    void testBotSkill(float s) { botSkill_ = s; }
    bool testHost(int stage, uint16_t port, int autoStart = 2);
    bool testJoin(const std::string& ip, uint16_t port, int car);
    void testDiscoveryPort(uint16_t p) { versus_.discoveryPort = p; }
    uint16_t testHostPort() const { return versus_.gamePort; }
    struct VersusReport {
        bool finished = false, racing = false;
        int rank = 0, mySlot = 0;
        float time = 0;
        bool active[rally::MAX_PLAYERS] = {}, seen[rally::MAX_PLAYERS] = {}, finishedSlot[rally::MAX_PLAYERS] = {};
        float times[rally::MAX_PLAYERS] = {};
        std::string names[rally::MAX_PLAYERS], ids[rally::MAX_PLAYERS];
    };
    VersusReport versusReport() const;
    std::string debugLine() const;
    CarInput botInput();
    CarInput lastIn_;  // what the driver (or autopilot) did last frame, for telemetry
    int stuckT_ = 0;   // autopilot: frames spent stuck
    float progressS_ = 0;  // where we were when the stuck clock started
    int noProgress_ = 0;   // frames off the road without getting anywhere
    bool driving() const { return mode_ == Mode::Stage; }
    int modeId() const { return int(mode_); }  // for scripted tests
    const Car& car() const { return car_; }
    // Trailer hooks: jump straight onto a stage mid-run, and let the autopilot drive.
    void demoStage(int stage, View v, float metresIn, int car);
    void demoStart(int stage, View v, int car);  // on the start line, the clock about to run
    void demoTitle() { toTitle(); }
    void testAutopilot(bool on) { autopilot_ = on; }
    int voiceReady() const { return voice_ ? voice_->ready() : 0; }
    bool done() const { return mode_ == Mode::Result || mode_ == Mode::Finish; }
    void setView(View v) { view_ = v; }

private:
    // ---- flow (game.cpp)
    void toTitle();
    void startRally(int venue);
    void loadStage(int stage);
    void beginStart();
    void finishStage();
    void afterResult();
    void afterStandings();
    void retire();
    void updateMenus(bool confirm, bool back);
    void startProfile(bool fromMenu);
    void updateProfile(bool confirm, bool back);
    void updateControls(bool confirm, bool back);
    void updateLobby(bool confirm, bool back);
    void updateService(bool confirm, bool back);
    void startOnlineStage();
    void updateOnline();
    void loadRecords();
    void saveRecords();
    void padFeedback();
    void tuneRadio();

    // ---- the stage (drive.cpp)
    CarInput readPad();
    void drive(const CarInput& in, bool attract);
    void stageClock();
    void callNotes();
    void updateOthers();
    void setupCrews();
    void planStageField();
    void crewTimes(int stage);
    void updateParticles();
    void emitEffects();
    void updateSound();
    void recordGhost();
    float splitOf(float time, int k) const;

    // ---- drawing (render.cpp)
    void render();
    void drawRoad(float& camS, float& camX, float& camY, float& camPsi);
    void drawWorld(float camS, float camX, float camY, float camPsi);
    void drawCockpit();
    void drawHud();
    void drawNotes(int y);
    void drawMenus();
    void drawResult();
    void drawStandings();
    void drawService();
    void drawLobby();
    void drawProfile();
    void drawControls();
    void drawMap(float cx, float cy);
    void drawRadio(int row);
    int fogFor(float metres) const;
    void spr(const gs::Mipped& m, float cx, float bottom, float h, int pal, bool flip, int fog, int clipY = 224, bool shadow = false);
    void text(const std::string& s, float x, float y, float scale, int pal, int align = 0);
    void hud(int col, int row, const std::string& s, int pal = PAL_HUD);
    void say(std::vector<std::string> lines, int frames, int pal = PAL_YELLOW);
    void drawCar(float cx, float groundY, float scale, float yawRel, float pitch, float roll, int pal, int fog, int clip, bool rolling, int rollView);

    // hardware and shared modules
    gs::System* sys_ = nullptr;
    gs::VDP* vdp_ = nullptr;
    Art art_;
    int tilesUsed_ = 0;
    std::unique_ptr<rally::Radio> radio_;
    std::unique_ptr<rally::Sfx> sfx_;
    std::unique_ptr<Voice> voice_;
    rally::Versus versus_;
    rally::Profile profile_;
    std::unique_ptr<rally::IdFetcher> fetch_;

    // flow
    Mode mode_ = Mode::Title;
    Game game_ = Game::Championship;
    View view_ = View::Chase;
    int t_ = 0;
    uint64_t frameNo_ = 0;
    int menuSel_ = 0, pickSel_ = 0, serviceSel_ = 0;
    int carId_ = 0;
    bool manual_ = false;
    bool assist_ = true;  // traction help (car select)
    int difficulty_ = 0;  // 0 amateur, 1 pro, 2 legend
    int venue_ = 0, stageNo_ = 0, stage_ = -1, loadedVenue_ = -1, loadedTod_ = -1;
    bool attract_ = true;
    float botSkill_ = 1;
    std::string toast_;
    int toastT_ = 0, padEvents_ = 0, ledColor_ = -1;
    std::vector<std::string> msg_;
    int msgT_ = 0, msgPal_ = PAL_YELLOW;

    // profile, controls, lobby (as in the other cartridge)
    std::string nameEdit_, addrEdit_;
    char pendingChar_ = 'A';
    int profStep_ = 0, profSel_ = 0, ctlSel_ = 0, lobbyStep_ = 0, lobbySel_ = 0, autoStart_ = 0;
    bool profFromMenu_ = false, rebinding_ = false, leftShown_[rally::MAX_PLAYERS] = {};

    // the stage
    Course course_;
    Car car_;
    std::vector<float> profile_v;     // reference speed profile (AI and autopilot)
    std::vector<float> profileT_;     // reference time to reach each segment
    std::vector<float> botV_;         // the autopilot's own, more careful speeds
    float stageTime_ = 0;             // our clock, seconds since the start
    bool started_ = false, finished_ = false;
    int splitsDone_ = 0;
    float mySplit_[2] = {};
    int nextNote_ = 0;
    std::vector<int> shownNotes_;
    float dirt_ = 0;                  // windscreen dirt 0..1
    int wiperT_ = 0;
    float camY_ = 0, camPsi_ = 0, camPitch_ = 0, shake_ = 0;
    float jumpsFlown_ = 0, airTotal_ = 0, topSpeed_ = 0;
    int jumps_ = 0, crashes_ = 0, hardLandings_ = 0;
    std::vector<Other> others_;
    std::vector<Particle> parts_;
    std::vector<Cloud> clouds_;
    struct Flake { float x, y, s; };
    std::vector<Flake> flakes_;
    int carBehindWarned_ = 0;
    bool paused_ = false;
    bool autopilot_ = false;       // test hook: the autopilot drives even with a pad attached
    int startGo_ = 480;           // frames on the start line before GO (online: later for later starters)
    float crewIncident_[15] = {};  // seconds each crew loses on this stage
    float yieldLost_[4] = {};      // time the cars ahead lost letting us by
    int contactCool_ = 0;
    int pauseT_ = 0;
    float lastSplitDelta_ = 0;
    int splitShow_ = 0;

    // the rally and the championship
    std::vector<Crew> crews_;
    std::vector<int> startOrder_;     // crew indices, -1 is us
    float myTime_[NUM_STAGES] = {};
    float myPenalty_[NUM_STAGES] = {};
    float myTotal_ = 0;
    int myPoints_ = 0;
    bool superRally_ = false;
    int serviceMinutes_ = 0;
    bool repair_[4] = {};
    int rallyRank_ = 0;

    // records and ghost
    float best_[NUM_STAGES] = {};
    std::vector<float> ghost_[NUM_STAGES];  // s, x, psi at 10 Hz
    std::vector<float> ghostRec_;
    bool newRecord_ = false;

    // drawing scratch
    float lineZ_[gs::SCREEN_H] = {};
    int horizon_ = 100;
    struct Item {
        float z;  // metres from the camera
        int kind; // 0 object, 1 other car, 2 our car, 3 cloud, 4 ghost
        const Segment* seg;
        const Placed* obj;
        int index;
    };
    std::vector<Item> items_;
};

}  // namespace rc
