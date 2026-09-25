// S3 RALLY - the cartridge.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "art.h"
#include "console/system.h"
#include "radio.h"
#include "sound.h"
#include "stages.h"
#include "profile.h"
#include "versus.h"
#include "version.h"

namespace rally {

enum class Mode { Title, Secret, Menu, Lobby, Controls, Profile, StageSelect, CarSelect, Intro, Countdown, Race, Pause, Over, Finish, Result, Ending };
enum class GameType { Championship, Practice, TimeAttack, Versus };

struct Input {
    float steer = 0, throttle = 0, brake = 0;
    bool analog = false, shiftUp = false, shiftDown = false;
};

struct Rival {
    float dist = 0, x = 0, lane = 0, speed = 0, top = 0;
    int pal = 0;
    const char* name = "";
    bool remote = false;  // driven by another player over the network, not by the AI
    int slot = -1;        // that player's slot in the session
};

struct Particle {
    float x, y, vx, vy, size, grow;
    int life, max, kind;  // kind: 0 dust, 1 spray, 2 splash, 3 spark
};

struct StageResult {
    int stage = 0, position = 16;
    float time = 0, bestLap = 0;
};

class Rally : public gs::Cart {
public:
    const char* title() const override { return "S3 RUN " S3_VERSION; }
    void init(gs::System& sys) override;
    void frame(gs::System& sys) override;

    // Headless driving for tests and the daily simulation report.
    struct SimReport {
        int stage;
        bool finished;
        int position;
        float raceTime, bestLap, minTimer, timeLeft;
    };
    SimReport simulateStage(int stage, std::vector<std::string>* shots, const std::string& shotDir);
    int tilesUsed() const { return tilesUsed_; }
    // Head-to-head test hooks: skip the menus and host or join directly.
    bool testHost(int stage, uint16_t port, int autoStart = 2);
    void testDiscoveryPort(uint16_t p) { versus_.discoveryPort = p; }
    void testProfile(const std::string& name) {
        profile_.name = name;
        profile_.id = localUuid();
        profile_.idSource = "local";
    }
    uint16_t testHostPort() const { return versus_.gamePort; }  // may differ if the first port was busy
    bool testJoin(const std::string& ip, uint16_t port, int car);
    struct VersusReport {  // what this console sees of the race
        bool finished = false, racing = false;
        int rank = 0, mySlot = 0;
        float time = 0;
        bool active[MAX_PLAYERS] = {}, seen[MAX_PLAYERS] = {}, finishedSlot[MAX_PLAYERS] = {};
        float times[MAX_PLAYERS] = {};
        std::string names[MAX_PLAYERS], ids[MAX_PLAYERS];
    };
    void testBotSkill(float s) { botSkill_ = s; }  // autopilot pace, for varied demo races
    std::string debugLine() const;  // where our car is and what it's doing (test diagnostics)
    VersusReport versusReport() const;
    // What the autopilot would do right now (used by the demo recorder).
    Input botInput() { return autopilot(); }
    bool racing() const { return mode_ == Mode::Race; }
    bool finished() const { return mode_ == Mode::Finish || mode_ == Mode::Result || mode_ == Mode::Over; }
    float curveAhead(float segs) const { return segAt(dist_ + segs * SEG).curve; }

private:
    // flow
    void toTitle();
    void startStage(int stage, bool withRivals);
    void beginCountdown();
    void finishStage();
    void gameOver();
    void afterResult();
    void setStage(int stage);

    // simulation
    Input readPad();
    Input autopilot();
    void drive(const Input& in, bool attract);
    void updateRivals();
    void checkCrossings(float a, float b, bool attract);
    void updateParticles();
    void updateSound();
    const Segment& segAt(float z) const;

    // rendering
    void render();
    void project(Proj& p, float wy, float wz, float camX, float camY, float camZ) const;
    void drawWorld(int baseIndex, float basePct, float position);
    void drawHud();
    void drawMenus();
    void drawMap();
    void drawSecret();
    void drawLobby();
    void drawControls();
    void updateLobby(bool confirm, bool back);
    void updateControls(bool confirm, bool back);
    void updateVersus();
    void startProfile(bool fromMenu);
    void updateProfile(bool confirm, bool back);
    void drawProfile();
    void startVersusRace();
    void padFeedback();
    bool timed() const { return type_ == GameType::Championship || type_ == GameType::Practice; }
    void drawTurboFx();
    int fogFor(float dz) const;
    void spr(const gs::Mipped& m, float cx, float bottom, float h, int pal, bool flip, int fog, int clipY = 224, bool shadow = false);
    void text(const std::string& s, float x, float y, float scale, int pal, int align = 0);
    void hud(int col, int row, const std::string& s, int pal = PAL_HUD);
    void say(std::vector<std::string> lines, int frames, int pal = PAL_YELLOW);
    void tuneRadio();
    void drawRadio(int row);
    void loadRecords();
    void saveRecords();

    gs::System* sys_ = nullptr;
    gs::VDP* vdp_ = nullptr;
    Art art_;
    Track track_;
    std::unique_ptr<Radio> radio_;
    std::unique_ptr<Sfx> sfx_;
    std::unique_ptr<CoDriver> voice_;
    int tilesUsed_ = 0;

    Mode mode_ = Mode::Title;
    GameType type_ = GameType::Championship;
    int stage_ = 0;
    int t_ = 0;  // frames in current mode
    uint64_t frameNo_ = 0;
    int menuSel_ = 0;
    int carId_ = 0;
    bool manual_ = false;
    bool withRivals_ = true;
    bool dim_ = false;
    Versus versus_;
    Profile profile_;
    std::unique_ptr<IdFetcher> fetch_;
    std::string nameEdit_;
    char pendingChar_ = 'A';
    int profStep_ = 0, profSel_ = 0;
    bool profFromMenu_ = false;
    int lobbyStep_ = 0, lobbySel_ = 0, ctlSel_ = 0;
    bool rebinding_ = false;
    std::string toast_;
    int toastT_ = 0, padEvents_ = 0;
    int ledColor_ = -1;
    bool opponentLeft_ = false;
    bool leftShown_[MAX_PLAYERS] = {};
    int autoStart_ = 0;  // test hook: host starts when this many players are in
    float botSkill_ = 1;
    std::string addrEdit_;
    bool turbo_ = false;  // unlocked by typing the secret code on the title screen
    int boosts_ = 0, boostT_ = 0;

    // player
    float dist_ = 0, x_ = 0, latV_ = 0, steer_ = 0, speed_ = 0, yaw_ = 0;
    float throttle_ = 0, rpm_ = 0.2f;
    int gear_ = 1;
    bool drifting_ = false, offroad_ = false, inWater_ = false;
    int crashCool_ = 0;
    float shake_ = 0;

    // race
    std::vector<Rival> rivals_;
    int rank_ = 16, startRank_ = 16;
    float timer_ = 0, raceTime_ = 0, lapTime_ = 0, bestLap_ = 0, minTimer_ = 99;
    int lap_ = 0, extends_ = 0;
    int noteShow_ = 0;
    PaceNote note_{};
    bool newRecord_ = false;
    std::vector<StageResult> champ_;

    // presentation
    float skyX_ = 0, nearX_ = 0, cloudX_ = 0;
    int horizon_ = 100;
    std::vector<Particle> parts_;
    struct Flake { float x, y, s; };
    std::vector<Flake> flakes_;
    std::vector<std::string> msg_;
    int msgT_ = 0, msgPal_ = PAL_YELLOW;
    float lineZ_[gs::SCREEN_H] = {};

    struct Item {
        float d;
        const Segment* seg;
        const Obj* obj;
        const Rival* car;
    };
    std::vector<Item> items_;

    // records per stage
    float recLap_[NUM_STAGES] = {}, recRace_[NUM_STAGES] = {};
};

}  // namespace rally
