// (3) RALLY 32 - the first S3-32 cartridge.
//
// The same fifteen stages, the same car physics and the same scenery art as
// (3) RALLY on the S3-16, drawn by the 32-bit machine's polygon GPU: a
// real 3D road you can see the shape of, hills and drops, and the car as
// lit polygons instead of pre-rendered frames. Same pad, same keys, same menu.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "console/system.h"
#include "g32/gpu.h"
#include "g32/gte.h"
#include "game/radio.h"
#include "game/sound.h"
#include "game/versus.h"
#include "rc/art.h"
#include "rc/car.h"
#include "rc/course.h"

namespace rc32 {

class Rally32 : public gs::Cart {
public:
    explicit Rally32(g32::Model model = g32::Model::S3_32) : gpu_(model) {}
    const char* title() const override { return is64() ? "(3) RALLY 64" : "(3) RALLY 32"; }
    bool is64() const { return gpu_.model() == g32::Model::S3_64; }
    void init(gs::System& sys) override;
    void frame(gs::System& sys) override;
    bool video(const uint32_t*& px, int& w, int& h) override;

    // Headless: drive a stage with the autopilot; returns the stage time (0 if it didn't finish).
    float simulate(int stage, int frames, std::vector<std::string>* shots, const std::string& dir);
    // Headless: median milliseconds per frame (logic + render) while driving stage 0.
    double benchmark(int frames);
    int lastTriangles() const { return gpu_.lastPrimitives(); }
    void setView(int v) { view_ = v; }
    size_t textureBytes() const { return gpu_.texBytes(); }

    // Online, as in (3) RALLY: the same protocol, packet signature and physics, so the 16-bit,
    // 32-bit and 64-bit carts can race each other. These are what the split screen and the
    // network tests use (src/multi.cpp).
    struct VersusReport {
        bool finished = false, racing = false;
        int rank = 0, mySlot = 0;
        float time = 0;
        bool active[rally::MAX_PLAYERS] = {}, seen[rally::MAX_PLAYERS] = {}, finishedSlot[rally::MAX_PLAYERS] = {};
        float times[rally::MAX_PLAYERS] = {};
        std::string names[rally::MAX_PLAYERS], ids[rally::MAX_PLAYERS];
    };
    void testProfile(const std::string& name) { myName_ = name; }
    void testBotSkill(float s) { botSkill_ = s; }
    void testDiscoveryPort(uint16_t p) { versus_.discoveryPort = p; }
    void testStartGap(float seconds) { startGap_ = seconds; }
    bool testHost(int stage, uint16_t port, int autoStart = 2);
    bool testJoin(const std::string& ip, uint16_t port, int car);
    uint16_t testHostPort() const { return versus_.gamePort; }
    VersusReport versusReport() const;
    std::string debugLine() const;

private:
    enum class Mode { Title, Pick, Lobby, Start, Drive, Done };
    struct Other {  // another player's car, from the network
        int slot = -1, car = 0;
        float s = 0, x = 0, psi = 0, speed = 0, startAt = 0;
        bool running = false, finished = false;
        std::string name;
    };
    void updateOnline();
    void startOnline();
    void lobbyHud();
    void drawCar(g32::V3 at, g32::V3 ground, float yaw, float pitch, float roll, const uint16_t* livery, bool cockpit);
    void loadStage(int stage);
    rc::CarInput readPad();
    rc::CarInput autopilot();
    void scene();
    void follow();
    void hud();
    void text(const std::string& s, float x, float y, float scale, uint16_t color, int align = 0);
    g32::V3 roadPoint(float s, float x) const;  // world position of a point on the course
    float headingAt(float s) const;

    gs::System* sys_ = nullptr;
    g32::GPU gpu_;
    rc::Art art_;  // the S3-16 art, built once and turned into textures
    std::unique_ptr<rally::Radio> radio_;
    std::unique_ptr<rally::Sfx> sfx_;
    int font_ = -1, logo_ = -1, firstStageTex_ = -1;
    int roadTex_[rc::SURF_COUNT] = {}, groundTex_ = -1, vergeTex_ = -1, waterTex_ = -1, snowTex_ = -1, dropTex_ = -1;
    int objTex_[rc::O_COUNT] = {};
    uint16_t livery_[16] = {};

    Mode mode_ = Mode::Title;
    int t_ = 0, stage_ = -1, pick_ = 0, carId_ = 0;  // the forgiving 4WD first, as on the 16-bit
    bool attract_ = true;
    rc::Course course_;
    rc::Car car_;
    std::vector<float> botV_;
    std::vector<g32::V3> pos_;  // centre line, world metres, one per segment
    float time_ = 0, best_ = 0;
    g32::Camera cam_;
    float camYaw_ = 0, camY_ = 0;
    float camRel_ = 0;  // chase camera heading relative to the road (as the 16-bit machine does it)
    int view_ = 0;  // 0 chase, 1 cockpit, 2 far chase (V or Z)
    bool arcade_ = true;  // arcade handling (V on the stage screen), as in (3) RALLY
    void cockpitHud();

    rally::Versus versus_;
    bool online_ = false;     // this stage is a race against other players
    int autoStart_ = 0;       // tests and the split screen: go when this many have joined
    int startGo_ = 0;         // frames from the countdown to our own start
    float startGap_ = 10;     // seconds between starters (a real rally; the split-screen demo goes closer)
    float botSkill_ = 1;      // the autopilot's pace (split screen: a spread of drivers)
    std::string myName_ = "PLAYER", lobbyMsg_;
    std::vector<Other> others_;
};

}  // namespace rc32
