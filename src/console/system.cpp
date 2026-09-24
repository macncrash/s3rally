#include "system.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gfx.h"

namespace gs {

bool Pad::anyPressed() const {
    for (int i = 0; i < BTN_COUNT; i++)
        if (cur[i] && !prev[i]) return true;
    return false;
}

void Pad::latch() {
    for (int i = 0; i < BTN_COUNT; i++) {
        prev[i] = cur[i];
        cur[i] = keys[i] || padBtn[i];
    }
}

static int keyToButton(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return BTN_UP;
        case SDLK_DOWN: return BTN_DOWN;
        case SDLK_LEFT: return BTN_LEFT;
        case SDLK_RIGHT: return BTN_RIGHT;
        case SDLK_z: return BTN_A;
        case SDLK_x: return BTN_B;
        case SDLK_c: case SDLK_SPACE: return BTN_C;
        case SDLK_q: return BTN_X;
        case SDLK_w: return BTN_Y;
        case SDLK_e: case SDLK_TAB: return BTN_Z;
        case SDLK_RETURN: return BTN_START;
        case SDLK_ESCAPE: case SDLK_BACKSPACE: return BTN_MODE;
        default: return -1;
    }
}

System::System(bool hl) : headless(hl) {
    if (!headless) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
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
        char* p = SDL_GetPrefPath("macncrash", "gensys16");
        base = p ? p : "./";
        SDL_free(p);
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
    if (inBios_) {
        if (biosStep()) bootCart(*cart_);
    } else if (cart_) {
        cart_->frame(*this);
    }
    frame++;
}

void System::render() { vdp.render(fb); }

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
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".png") == 0) return writePng(path, fb, SCREEN_W, SCREEN_H);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(fb, SCREEN_W, SCREEN_H, 32, SCREEN_W * 4, SDL_PIXELFORMAT_ARGB8888);
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

    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) {
            ctl_ = SDL_GameControllerOpen(i);
            break;
        }

    vdp.reset();
    biosInit();
    inBios_ = true;

    const double freq = double(SDL_GetPerformanceFrequency());
    uint64_t last = SDL_GetPerformanceCounter();
    double acc = 0;
    const double tick = 1.0 / 60.0;
    while (!quit_) {
        pollEvents();
        uint64_t now = SDL_GetPerformanceCounter();
        acc += std::min(0.25, double(now - last) / freq);
        last = now;
        int steps = 0;
        while (acc >= tick && steps < 5) {
            step();
            acc -= tick;
            steps++;
        }
        if (steps == 5) acc = 0;  // can't keep up: drop the backlog rather than spiral
        if (steps) render();
        present();
        // Without vsync (software renderer, minimised window) don't spin the CPU.
        if (!vsync_ || (SDL_GetWindowFlags(win_) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))) SDL_Delay(1);
    }
    // Stop the audio thread before the cart (and the samples it owns) can be destroyed.
    if (audioDev_) {
        SDL_CloseAudioDevice(audioDev_);
        audioDev_ = 0;
    }
    apu.silence();
    return 0;
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
                }
                int b = keyToButton(k);
                if (b >= 0 && !alt) pad.keys[b] = true;  // Alt+Enter is fullscreen, not START
                break;
            }
            case SDL_KEYUP: {
                int b = keyToButton(e.key.keysym.sym);
                if (b >= 0) pad.keys[b] = false;  // always release, whatever the modifiers
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (!ctl_) ctl_ = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (ctl_ && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctl_))) {
                    SDL_GameControllerClose(ctl_);
                    ctl_ = nullptr;
                }
                break;
        }
    }
    std::fill(std::begin(pad.padBtn), std::end(pad.padBtn), false);
    pad.axisX = pad.accel = pad.brake = 0;
    if (ctl_) {
        auto b = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(ctl_, x) != 0; };
        float ax = SDL_GameControllerGetAxis(ctl_, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        float ay = SDL_GameControllerGetAxis(ctl_, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        pad.accel = SDL_GameControllerGetAxis(ctl_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
        pad.brake = SDL_GameControllerGetAxis(ctl_, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
        if (std::fabs(ax) > 0.12f) pad.axisX = std::clamp((ax - std::copysign(0.12f, ax)) / 0.88f, -1.0f, 1.0f);
        pad.padBtn[BTN_UP] = b(SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -0.5f;
        pad.padBtn[BTN_DOWN] = b(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 0.5f;
        pad.padBtn[BTN_LEFT] = b(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -0.5f;
        pad.padBtn[BTN_RIGHT] = b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 0.5f;
        pad.padBtn[BTN_C] = b(SDL_CONTROLLER_BUTTON_A) || pad.accel > 0.5f;
        pad.padBtn[BTN_B] = b(SDL_CONTROLLER_BUTTON_B) || pad.brake > 0.5f;
        pad.padBtn[BTN_A] = b(SDL_CONTROLLER_BUTTON_X);
        pad.padBtn[BTN_Z] = b(SDL_CONTROLLER_BUTTON_Y);
        pad.padBtn[BTN_X] = b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        pad.padBtn[BTN_Y] = b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        pad.padBtn[BTN_START] = b(SDL_CONTROLLER_BUTTON_START);
        pad.padBtn[BTN_MODE] = b(SDL_CONTROLLER_BUTTON_BACK);
    }
}

void System::present() {
    SDL_UpdateTexture(tex_, nullptr, fb, SCREEN_W * 4);
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
        float rowH = float(dh) / SCREEN_H;
        if (rowH >= 2.5f) {
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 90);
            static SDL_FRect rects[SCREEN_H];
            for (int y = 0; y < SCREEN_H; y++)
                rects[y] = SDL_FRect{float(dst.x), dst.y + (y + 0.62f) * rowH, float(dw), rowH * 0.38f};
            SDL_RenderFillRectsF(ren_, rects, SCREEN_H);
        }
    }
    SDL_RenderPresent(ren_);
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
    bitmapToPlane(alloc, vdp.A, 5, 8, logo, 0);
    bitmapToPlane(alloc, vdp.B, 26, 14, textBitmap("16-BIT", {2, 5, 0, 0, 1}), 0);
    bitmapToPlane(alloc, vdp.B, 8, 20, textBitmap("PRODUCED BY OR UNDER LICENSE", {1, 6, 0, 0, 1}), 0);
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
