#include "system.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "gfx.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace gs {

bool Pad::anyPressed() const {
    for (int i = 0; i < BTN_COUNT; i++)
        if (cur[i] && !prev[i]) return true;
    return false;
}

void Pad::latch() {
    for (int i = 0; i < BTN_COUNT; i++) {
        prev[i] = cur[i];
        cur[i] = keys[i] || padBtn[i] || tapped[i];
        tapped[i] = false;
    }
}

int System::keyButton(int k) {
    switch (k) {
        case SDLK_UP: return BTN_UP;
        case SDLK_DOWN: return BTN_DOWN;
        case SDLK_LEFT: return BTN_LEFT;
        case SDLK_RIGHT: return BTN_RIGHT;
        case SDLK_z: case SDLK_v: return BTN_A;
        case SDLK_x: return BTN_B;
        case SDLK_c: return BTN_C;
        case SDLK_SPACE: return BTN_TURBO;
        case SDLK_q: return BTN_X;
        case SDLK_w: return BTN_Y;
        case SDLK_e: case SDLK_TAB: return BTN_Z;
        case SDLK_RETURN: return BTN_START;
        case SDLK_ESCAPE: return BTN_MODE;
        default: return -1;
    }
}

System::System(bool hl) : headless(hl) {
    if (!headless) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        // Rumble and light-bar support for PlayStation pads over USB and Bluetooth.
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
            std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    }
}

System::~System() {
    if (audioDev_) SDL_CloseAudioDevice(audioDev_);
    if (ctl_) SDL_GameControllerClose(ctl_);
    if (tex_) SDL_DestroyTexture(tex_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    if (!headless) SDL_Quit();
}

std::string System::dataPath(const std::string& file) const {
    static std::string base;
    if (base.empty()) {
        char* p = SDL_GetPrefPath("macncrash", "s3engine");
        base = p ? p : "./";
        SDL_free(p);
        // Saves from before the rename live in the old folder next to it: bring them across once.
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path old = fs::path(base).parent_path().parent_path() / "gensys16";
        if (base != "./" && fs::is_directory(old, ec) && fs::is_empty(base, ec))
            fs::copy(old, base, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    }
    return base + file;
}

void System::bootCart(Cart& cart) {
    cart_ = &cart;
    inBios_ = false;
    vdp.reset();
    apu.silence();
    frame = 0;
    cart.init(*this);
}

void System::powerOn(Cart& cart) {
    cart_ = &cart;
    vdp.reset();
    frame = 0;
    biosInit();
    inBios_ = true;
}

void System::step() {
    pad.latch();
    if (ejectPending_ && home_) {  // back to the menu between frames, never inside a cart's frame
        ejectPending_ = false;
        bootCart(*home_);
    }
    if (inBios_) {
        if (biosStep()) bootCart(*cart_);
    } else if (cart_) {
        cart_->frame(*this);
    }
    frame++;
}

void System::render() {
    if (!inBios_ && cart_ && cart_->video(shown, shownW, shownH)) return;
    vdp.render(fb);
    shown = fb;
    shownW = SCREEN_W;
    shownH = SCREEN_H;
}

static void putBE(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s));
}

// Minimal PNG writer (stored deflate blocks) so screenshots need no libraries.
static bool writePng(const std::string& path, const uint32_t* px, int w, int h) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        for (int x = 0; x < w; x++) {
            uint32_t c = px[y * w + x];
            raw.push_back(uint8_t(c >> 16));
            raw.push_back(uint8_t(c >> 8));
            raw.push_back(uint8_t(c));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t i = 0; i < raw.size(); i += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n == raw.size() ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
    }
    putBE(z, (b << 16) | a);
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
    std::vector<uint8_t> out = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        putBE(out, uint32_t(data.size()));
        size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        uint32_t c = 0xffffffffu;
        for (size_t i = start; i < out.size(); i++) c = table[(c ^ out[i]) & 255] ^ (c >> 8);
        putBE(out, c ^ 0xffffffffu);
    };
    std::vector<uint8_t> ihdr;
    putBE(ihdr, uint32_t(w));
    putBE(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool System::saveScreenshot(const std::string& path) {
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".png") == 0) return writePng(path, shown, shownW, shownH);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint32_t*>(shown), shownW, shownH, 32, shownW * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

static void audioCallback(void* user, Uint8* stream, int len) {
    static_cast<APU*>(user)->render(reinterpret_cast<float*>(stream), len / int(sizeof(float) * 2));
}

int System::run(Cart& cart) {
    cart_ = &cart;
    std::string title = std::string("S3-16  -  ") + cart.title();
    win_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    vsync_ = ren_ != nullptr;
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, 0);
    if (!ren_) {
        std::fprintf(stderr, "SDL renderer: %s\n", SDL_GetError());
        return 1;
    }
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audioCallback;
    want.userdata = &apu;
    audioDev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    apu.init(audioDev_ ? have.freq : 48000);
    if (audioDev_) SDL_PauseAudioDevice(audioDev_, 0);

    for (int i = 0; i < SDL_NumJoysticks() && !ctl_; i++)
        if (SDL_IsGameController(i)) openController(i);
    ctl.deserialize(loadBlob("controls.txt"));

    vdp.reset();
    biosInit();
    inBios_ = true;

    last_ = SDL_GetPerformanceCounter();
#ifdef __EMSCRIPTEN__
    // In a browser the page owns the loop: run one tick per animation frame, forever.
    emscripten_set_main_loop_arg([](void* s) { static_cast<System*>(s)->tick(); }, this, 0, 1);
#else
    while (tick()) {
        // Without vsync (software renderer, minimised window) don't spin the CPU.
        if (!vsync_ || (SDL_GetWindowFlags(win_) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))) SDL_Delay(1);
    }
#endif
    // Stop the audio thread before the cart (and the samples it owns) can be destroyed.
    if (audioDev_) {
        SDL_CloseAudioDevice(audioDev_);
        audioDev_ = 0;
    }
    apu.silence();
    return 0;
}

// One pass of the fixed-timestep loop: input, as many 60 Hz steps as are due, video out.
bool System::tick() {
    pollEvents();
    const uint64_t now = SDL_GetPerformanceCounter();
    acc_ += std::min(0.25, double(now - last_) / double(SDL_GetPerformanceFrequency()));
    last_ = now;
    const double dt = 1.0 / 60.0;
    int steps = 0;
    while (acc_ >= dt && steps < 5) {
        step();
        acc_ -= dt;
        steps++;
    }
    if (steps == 5) acc_ = 0;  // can't keep up: drop the backlog rather than spiral
    if (steps) render();
    present();
    return !quit_;
}

void System::pollEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: quit_ = true; break;
            case SDL_KEYDOWN: {
                const SDL_Keycode k = e.key.keysym.sym;
                const bool alt = (e.key.keysym.mod & KMOD_ALT) != 0;
                if (!e.key.repeat) {  // hotkeys fire once per press
                    if (k == SDLK_F1) crt = !crt;
                    if (k == SDLK_F11 || (k == SDLK_RETURN && alt)) {
                        bool fs = SDL_GetWindowFlags(win_) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                        SDL_SetWindowFullscreen(win_, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                    if (k == SDLK_F12) {
                        std::string p = dataPath("shot-" + std::to_string(frame) + ".png");
                        if (saveScreenshot(p)) std::printf("screenshot: %s\n", p.c_str());
                    }
                    if (k == SDLK_F10) quit_ = true;
                    // Remember what was typed, for cheat codes.
                    if ((k >= SDLK_a && k <= SDLK_z) || (k >= SDLK_0 && k <= SDLK_9)) typed += char(k);
                    else if (k == SDLK_RETURN && !alt) typed += '\n';
                    else if (k == SDLK_SPACE) typed += ' ';
                    else if (k == SDLK_BACKSPACE) typed += '\b';
                    else if (k == SDLK_PERIOD || k == SDLK_KP_PERIOD) typed += '.';
                    else if (k == SDLK_SEMICOLON && (e.key.keysym.mod & KMOD_SHIFT)) typed += ':';
                    else if (k >= SDLK_KP_1 && k <= SDLK_KP_0) typed += k == SDLK_KP_0 ? '0' : char('1' + (k - SDLK_KP_1));
                    if (typed.size() > 16) typed.erase(0, typed.size() - 16);
                }
                int b = keyButton(k);
                if (b >= 0 && !alt) pad.keys[b] = pad.tapped[b] = true;  // Alt+Enter is fullscreen, not START
                break;
            }
            case SDL_KEYUP: {
                int b = keyButton(e.key.keysym.sym);
                if (b >= 0) pad.keys[b] = false;  // always release, whatever the modifiers
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (!ctl_) openController(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (ctl_ && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctl_))) {
                    SDL_GameControllerClose(ctl_);
                    ctl_ = nullptr;
                    ctl.connected = false;
                    ctl.events++;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                ctl.lastPressed = e.cbutton.button;
                break;
        }
    }
    readController(ctl_, ctl, pad, trigWas_);
}

// Read one game controller into a pad through its button map. Shared by the
// normal board and the four-console quad mode.
void System::readController(_SDL_GameController* ctl_, Controller& ctl, Pad& pad, bool trigWas_[2]) {
    std::fill(std::begin(pad.padBtn), std::end(pad.padBtn), false);
    pad.axisX = pad.accel = pad.brake = 0;
    ctl.anyDown = false;
    if (!ctl_) return;
    const float ax = SDL_GameControllerGetAxis(ctl_, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    const float ay = SDL_GameControllerGetAxis(ctl_, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    const float lt = SDL_GameControllerGetAxis(ctl_, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
    const float rt = SDL_GameControllerGetAxis(ctl_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
    // Physical state, including the triggers as two extra "buttons".
    bool phys[PHYS_COUNT] = {};
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < PHYS_LTRIGGER; i++)
        phys[i] = SDL_GameControllerGetButton(ctl_, SDL_GameControllerButton(i)) != 0;
    phys[PHYS_LTRIGGER] = lt > 0.5f;
    phys[PHYS_RTRIGGER] = rt > 0.5f;
    for (int t = 0; t < 2; t++) {  // triggers have no button events: report fresh pulls here
        const bool on = phys[PHYS_LTRIGGER + t];
        if (on && !trigWas_[t]) ctl.lastPressed = PHYS_LTRIGGER + t;
        trigWas_[t] = on;
    }
    for (int i = 0; i < PHYS_COUNT; i++) ctl.anyDown |= phys[i];
    // Stick steering and directions are fixed; everything else goes through the map.
    if (std::fabs(ax) > 0.12f) pad.axisX = std::clamp((ax - std::copysign(0.12f, ax)) / 0.88f, -1.0f, 1.0f);
    pad.padBtn[BTN_UP] = ay < -0.5f;
    pad.padBtn[BTN_DOWN] = ay > 0.5f;
    pad.padBtn[BTN_LEFT] = ax < -0.5f;
    pad.padBtn[BTN_RIGHT] = ax > 0.5f;
    if (ctl.suppress) return;
    pad.accel = rt;
    pad.brake = lt;
    for (int i = 0; i < PHYS_COUNT; i++)
        if (phys[i] && ctl.map[i] >= 0 && ctl.map[i] < BTN_COUNT) pad.padBtn[ctl.map[i]] = true;
}

void System::present() {
    int tw = 0, th = 0;
    SDL_QueryTexture(tex_, nullptr, nullptr, &tw, &th);
    if (tw != shownW || th != shownH) {  // another console, another resolution
        SDL_DestroyTexture(tex_);
        tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, shownW, shownH);
    }
    SDL_UpdateTexture(tex_, nullptr, shown, shownW * 4);
    int ww, wh;
    SDL_GetRendererOutputSize(ren_, &ww, &wh);
    // 4:3 display, letterboxed.
    int dw = ww, dh = ww * 3 / 4;
    if (dh > wh) {
        dh = wh;
        dw = wh * 4 / 3;
    }
    SDL_Rect dst{(ww - dw) / 2, (wh - dh) / 2, dw, dh};
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
    SDL_RenderCopy(ren_, tex_, nullptr, &dst);
    if (crt) {
        // Scanlines: darken the lower part of every source row.
        const float rowH = float(dh) / shownH;
        if (rowH >= 2.5f) {
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 90);
            static std::vector<SDL_FRect> rects;
            rects.resize(size_t(shownH));
            for (int y = 0; y < shownH; y++)
                rects[size_t(y)] = SDL_FRect{float(dst.x), dst.y + (y + 0.62f) * rowH, float(dw), rowH * 0.38f};
            SDL_RenderFillRectsF(ren_, rects.data(), shownH);
        }
    }
    SDL_RenderPresent(ren_);
}

// ---------------------------------------------------------------- controller

void Controller::resetMap() {
    for (auto& m : map) m = -1;
    map[SDL_CONTROLLER_BUTTON_A] = BTN_C;              // accelerate (Cross)
    map[SDL_CONTROLLER_BUTTON_B] = BTN_B;              // brake (Circle)
    map[SDL_CONTROLLER_BUTTON_X] = BTN_TURBO;          // turbo (Square)
    map[SDL_CONTROLLER_BUTTON_Y] = BTN_Z;              // radio (Triangle)
    map[SDL_CONTROLLER_BUTTON_BACK] = BTN_MODE;        // back (Create / View)
    map[SDL_CONTROLLER_BUTTON_START] = BTN_START;      // start / pause (Options / Menu)
    map[SDL_CONTROLLER_BUTTON_LEFTSTICK] = BTN_TURBO;
    map[SDL_CONTROLLER_BUTTON_RIGHTSTICK] = BTN_TURBO;
    map[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = BTN_X;   // shift down
    map[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = BTN_Y;  // shift up
    map[SDL_CONTROLLER_BUTTON_TOUCHPAD] = BTN_A;       // change view (touchpad / share)
    map[SDL_CONTROLLER_BUTTON_MISC1] = BTN_A;
    map[SDL_CONTROLLER_BUTTON_DPAD_UP] = BTN_UP;
    map[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = BTN_DOWN;
    map[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = BTN_LEFT;
    map[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = BTN_RIGHT;
    map[20] = BTN_Z;                                   // touchpad click: radio
    map[PHYS_LTRIGGER] = BTN_B;
    map[PHYS_RTRIGGER] = BTN_C;
}

const char* Controller::physName(int phys) const {
    static const char* ps[PHYS_COUNT] = {"CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "CREATE", "PS", "OPTIONS", "L3", "R3", "L1", "R1",
                                         "UP", "DOWN", "LEFT", "RIGHT", "MUTE", "P1", "P2", "P3", "P4", "TPAD", "L2", "R2"};
    static const char* xb[PHYS_COUNT] = {"A", "B", "X", "Y", "VIEW", "GUIDE", "MENU", "LS", "RS", "LB", "RB",
                                         "UP", "DOWN", "LEFT", "RIGHT", "SHARE", "P1", "P2", "P3", "P4", "TPAD", "LT", "RT"};
    if (phys < 0 || phys >= PHYS_COUNT) return "?";
    if (type == PAD_PS4 && phys == 4) return "SHARE";
    return (type == PAD_PS4 || type == PAD_PS5) ? ps[phys] : xb[phys];
}

std::string Controller::serialize() const {
    std::string s;
    for (int i = 0; i < PHYS_COUNT; i++) s += std::to_string(map[i]) + (i + 1 < PHYS_COUNT ? " " : "\n");
    return s;
}

void Controller::deserialize(const std::string& s) {
    int8_t m[PHYS_COUNT];
    size_t pos = 0;
    for (int i = 0; i < PHYS_COUNT; i++) {
        char* end = nullptr;
        long v = std::strtol(s.c_str() + pos, &end, 10);
        if (end == s.c_str() + pos || v < -1 || v >= BTN_COUNT) return;  // missing or corrupt: keep defaults
        m[i] = int8_t(v);
        pos = size_t(end - s.c_str());
    }
    std::copy(std::begin(m), std::end(m), std::begin(map));
}

void System::openController(int index) {
    ctl_ = SDL_GameControllerOpen(index);
    if (!ctl_) return;
    const char* n = SDL_GameControllerName(ctl_);
    ctl.name = n ? n : "CONTROLLER";
    switch (SDL_GameControllerGetType(ctl_)) {
        case SDL_CONTROLLER_TYPE_PS4: ctl.type = PAD_PS4; break;
        case SDL_CONTROLLER_TYPE_PS5: ctl.type = PAD_PS5; break;
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE: ctl.type = PAD_XBOX; break;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: ctl.type = PAD_SWITCH; break;
        default: ctl.type = PAD_OTHER; break;
    }
    ctl.connected = true;
    ctl.events++;
}

void System::rumble(float low, float high, int ms) {
    if (!ctl_) return;
    auto u16 = [](float v) { return Uint16(std::clamp(v, 0.0f, 1.0f) * 65535); };
    SDL_GameControllerRumble(ctl_, u16(low), u16(high), Uint32(std::max(0, ms)));
}

void System::setLight(int r, int g, int b) {
    if (ctl_) SDL_GameControllerSetLED(ctl_, Uint8(r), Uint8(g), Uint8(b));
}

std::string System::loadBlob(const std::string& name) const {
#ifdef __EMSCRIPTEN__
    std::string js = "localStorage.getItem('s3-" + name + "') || localStorage.getItem('gensys-" + name + "') || ''";  // old key: saves from before the rename
    const char* v = emscripten_run_script_string(js.c_str());
    return v ? v : "";
#else
    std::ifstream f(dataPath(name));
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
#endif
}

void System::saveBlob(const std::string& name, const std::string& data) const {
    if (headless) return;
#ifdef __EMSCRIPTEN__
    EM_ASM({ try { localStorage.setItem('s3-' + UTF8ToString($0), UTF8ToString($1)); } catch (e) {} }, name.c_str(), data.c_str());
#else
    std::ofstream(dataPath(name)) << data;
#endif
}

// ---------------------------------------------------------------- boot ROM

void System::biosInit() {
    vdp.setColor(1, rgb4(15, 15, 15));
    vdp.setColor(2, rgb4(7, 11, 15));
    vdp.setColor(3, rgb4(3, 7, 15));
    vdp.setColor(4, rgb4(1, 2, 11));
    vdp.setColor(5, rgb4(15, 3, 2));
    vdp.setColor(6, rgb4(7, 7, 8));
    vdp.setColor(7, rgb4(0, 0, 4));
    TileAlloc alloc(vdp);
    Bitmap logo = textBitmap("S3", {5, 1, 7, 0, 1});
    for (int y = 0; y < logo.h; y++)
        for (int x = 0; x < logo.w; x++) {
            uint8_t& p = logo.px[size_t(y) * logo.w + x];
            if (p == 1) p = y < logo.h * 0.38f ? 2 : y < logo.h * 0.62f ? 3 : 4;
        }
    vdp.A.clear();
    vdp.B.clear();
    const int lx = std::max(0, (40 - (logo.w + 7) / 8 - 7) / 2);  // logo and "16-BIT" centred together
    bitmapToPlane(alloc, vdp.A, lx, 8, logo, 0);
    bitmapToPlane(alloc, vdp.B, lx + (logo.w + 7) / 8 + 1, 14, textBitmap("16-BIT", {2, 5, 0, 0, 1}), 0);
    bitmapToPlane(alloc, vdp.B, 15, 20, textBitmap("S3 ENGINE", {1, 6, 0, 0, 1}), 0);
    bitmapToPlane(alloc, vdp.B, 14, 22, textBitmap("FROM MACNCRASH", {1, 6, 0, 0, 1}), 0);
    vdp.HUD.clear();
}

bool System::biosStep() {
    const int f = int(frame);
    float t = std::min(1.0f, f / 40.0f);
    float ease = 1 - std::pow(1 - t, 3.0f);
    for (int y = 0; y < SCREEN_H; y++) {
        float wob = f < 60 ? std::sin(y * 0.3f + f * 0.4f) * (1 - ease) * 12 : 0;
        vdp.A.hscroll[y] = int16_t(std::lround((1 - ease) * 360 + wob));
    }
    float flash = (f > 45 && f < 75) ? (f - 45) / 30.0f : 0;
    float k = std::sin(flash * 3.14159f);
    vdp.setColor(2, rgb4(7 + int(8 * k), 11 + int(4 * k), 15));
    vdp.setColor(3, rgb4(3 + int(12 * k), 7 + int(8 * k), 15));
    if (f == 42) chime();
    return f >= 170 || (f > 20 && pad.pressed(BTN_START));
}

void System::chime() {
    FMPatch bell;
    bell.alg = 4;
    bell.op[0] = {3.5f, 0.5f, 0.001f, 0.4f, 0.0f, 0.3f};
    bell.op[1] = {1, 0.8f, 0.001f, 1.2f, 0.0f, 0.8f};
    bell.op[2] = {7, 0.3f, 0.001f, 0.2f, 0.0f, 0.3f};
    bell.op[3] = {2, 0.4f, 0.001f, 0.9f, 0.0f, 0.6f};
    bell.vol = 0.22f;
    const float notes[] = {659.3f, 987.8f, 1318.5f};
    for (int i = 0; i < 3; i++) {
        apu.setPatch(i, bell);
        apu.setPan(i, (i - 1) * 0.5f);
        apu.keyOn(i, notes[i]);
    }
}

}  // namespace gs
