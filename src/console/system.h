// S3-16 system board: 60 Hz clock, video out, audio out, 6-button pad,
// boot ROM and cartridge slot.
#pragma once
#include <cstdint>
#include <string>

#include "apu.h"
#include "vdp.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct _SDL_GameController;

namespace gs {

enum Button { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_START, BTN_MODE, BTN_TURBO, BTN_COUNT };

struct Pad {
    bool cur[BTN_COUNT] = {};
    bool prev[BTN_COUNT] = {};
    bool keys[BTN_COUNT] = {};
    bool padBtn[BTN_COUNT] = {};
    bool tapped[BTN_COUNT] = {};  // pressed since the last frame (catches taps shorter than a frame)
    float axisX = 0;     // analog steering -1..1 (0 if none)
    float accel = 0;     // analog triggers 0..1
    float brake = 0;
    bool down(Button b) const { return cur[b]; }
    bool pressed(Button b) const { return cur[b] && !prev[b]; }
    bool anyPressed() const;
    void latch();
};

// Physical controller inputs: SDL game-controller buttons 0..20, then the two triggers.
constexpr int PHYS_LTRIGGER = 21, PHYS_RTRIGGER = 22, PHYS_COUNT = 23;
enum PadType { PAD_NONE, PAD_XBOX, PAD_PS4, PAD_PS5, PAD_SWITCH, PAD_OTHER };

// The attached controller and its (remappable) button map.
struct Controller {
    bool connected = false;
    PadType type = PAD_NONE;
    std::string name;
    int events = 0;            // bumps on connect/disconnect, so the game can show a notice
    int8_t map[PHYS_COUNT];    // physical input -> Button, -1 = unbound
    int lastPressed = -1;      // most recent physical press (the game clears it to capture a rebind)
    bool suppress = false;     // while set, physical presses don't reach the pad (used when rebinding)
    bool anyDown = false;      // any physical button or trigger held right now
    Controller() { resetMap(); }
    void resetMap();
    const char* physName(int phys) const;  // label as printed on this controller
    std::string serialize() const;
    void deserialize(const std::string& s);
};

class System;

class Cart {
public:
    virtual ~Cart() = default;
    virtual const char* title() const = 0;
    virtual void init(System& sys) = 0;
    virtual void frame(System& sys) = 0;  // 60 times a second
};

class System {
public:
    explicit System(bool headless = false);
    ~System();

    // Windowed run with boot ROM; returns when the window closes.
    int run(Cart& cart);

    // Headless helpers (tests, simulations, screenshots).
    void bootCart(Cart& cart);
    void powerOn(Cart& cart);  // headless power-on through the boot ROM
    void step();  // one frame of the current cart
    void render();
    bool saveScreenshot(const std::string& path);

    std::string dataPath(const std::string& file) const;  // per-user save directory
    // Small persistent settings: a file on desktop, localStorage in a browser.
    std::string loadBlob(const std::string& name) const;
    void saveBlob(const std::string& name, const std::string& data) const;
    // Controller feedback (no-ops without a capable pad).
    void rumble(float low, float high, int ms);
    void setLight(int r, int g, int b);
    void quit() { quit_ = true; }

    VDP vdp;
    APU apu;
    Pad pad;
    Controller ctl;
    uint64_t frame = 0;
    uint32_t fb[SCREEN_W * SCREEN_H] = {};
    bool headless;
    bool scripted = false;  // headless, but input comes from the pad (demo recording)
    std::string typed;  // recent keyboard letters/digits, ' ', "\n" Enter, "\b" Backspace (cheat codes, name entry)
    bool crt = true;

private:
    bool tick();  // one frame of the main loop; false once quit
    void pollEvents();
    void present();
    void biosInit();
    bool biosStep();
    void chime();

    Cart* cart_ = nullptr;
    bool inBios_ = false;
    bool quit_ = false;
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;
    _SDL_GameController* ctl_ = nullptr;
    uint32_t audioDev_ = 0;
    bool trigWas_[2] = {false, false};
    void openController(int index);
    bool vsync_ = false;
    uint64_t last_ = 0;
    double acc_ = 0;
};

}  // namespace gs
