#include "multi.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "console/gfx.h"
#include "console/system.h"
#include "game/rally.h"
#include "rc/game.h"
#include "rc32/rally32.h"

namespace {

constexpr uint16_t TEST_PORT = 47117;
constexpr uint16_t TEST_DISCOVERY = 47216;  // private, so nothing else on 47016 interferes
const char* DEMO_NAMES[4] = {"RADRACER", "NEON FOX", "BIG RED", "SIDEWAYS"};
g32::Model g_model = g32::Model::S3_32;  // which machine the 3D carts are built for (RALLY 32 or 64)

template <class Cart>
std::unique_ptr<Cart> makeCart() {
    if constexpr (std::is_same_v<Cart, rc32::Rally32>) return std::make_unique<Cart>(g_model);
    else return std::make_unique<Cart>();
}

// One console in a session, with either cartridge in it.
template <class Cart>
struct Seat {
    std::unique_ptr<gs::System> sys;
    std::unique_ptr<Cart> cart;
};

const char* stageName(const rally::Rally*, int stage) { return rally::stageDef(stage).name; }
const char* stageName(const rc::RallyChamp*, int stage) { return rc::venue(stage / 3).stages[stage % 3]; }
int stageCount(const rally::Rally*) { return rally::NUM_STAGES; }
int stageCount(const rc::RallyChamp*) { return rc::NUM_STAGES; }
const char* stageName(const rc32::Rally32*, int stage) { return rc::venue(stage / 3).stages[stage % 3]; }
int stageCount(const rc32::Rally32*) { return rc::NUM_STAGES; }

// Boot n consoles; seat 0 hosts, the others join it over UDP.
template <class Cart>
bool startSession(std::vector<Seat<Cart>>& seats, int n, int stage, bool discover, const std::vector<std::string>& names,
                  const std::vector<float>& skill) {
    for (int i = 0; i < n; i++) {
        if (int(seats.size()) <= i) {
            Seat<Cart> s;
            s.sys = std::make_unique<gs::System>(true);
            s.cart = makeCart<Cart>();
            s.sys->bootCart(*s.cart);
            seats.push_back(std::move(s));
        }
        seats[size_t(i)].cart->testProfile(names[size_t(i)]);
        seats[size_t(i)].cart->testDiscoveryPort(TEST_DISCOVERY);
        seats[size_t(i)].cart->testBotSkill(skill[size_t(i)]);
    }
    if (!seats[0].cart->testHost(stage, TEST_PORT, n)) return false;
    const uint16_t port = seats[0].cart->testHostPort();
    for (int i = 1; i < n; i++)
        if (!seats[size_t(i)].cart->testJoin(discover ? "discover" : "127.0.0.1", port, i % 2)) return false;
    return true;
}

template <class Cart>
bool allDone(const std::vector<Seat<Cart>>& seats) {
    for (const Seat<Cart>& s : seats) {
        const auto r = s.cart->versusReport();
        if (!r.finished) return false;
        for (int k = 0; k < rally::MAX_PLAYERS; k++)
            if (r.active[k] && k != r.mySlot && !r.finishedSlot[k]) return false;
    }
    return true;
}

// Paste each console's picture (as rendered: 320x224 for the S3-16, 320x240 for the S3-32,
// 640x480 for the S3-64) into a 2x2 grid with thin dividers. `w` and `h` come back as the
// grid's size.
template <class Cart>
void composite(const std::vector<Seat<Cart>>& seats, std::vector<uint32_t>& out, int& w, int& h) {
    const int W = seats[0].sys->shownW, H = seats[0].sys->shownH;
    w = W * 2, h = H * 2;
    out.assign(size_t(w) * size_t(h), 0xff101010u);
    for (size_t i = 0; i < seats.size() && i < 4; i++) {
        const gs::System& s = *seats[i].sys;
        if (s.shownW != W || s.shownH != H) continue;  // (all seats run the same machine)
        const int ox = int(i % 2) * W, oy = int(i / 2) * H;
        for (int y = 0; y < H; y++) std::memcpy(&out[size_t(oy + y) * size_t(w) + size_t(ox)], &s.shown[size_t(y) * size_t(W)], size_t(W) * 4);
    }
    for (int y = 0; y < h; y++) out[size_t(y) * size_t(w) + size_t(W - 1)] = out[size_t(y) * size_t(w) + size_t(W)] = 0xff000000u;
    for (int x = 0; x < w; x++) out[size_t(H - 1) * size_t(w) + size_t(x)] = out[size_t(H) * size_t(w) + size_t(x)] = 0xff000000u;
}

}  // namespace

template <class Cart>
int versusTestT(int stage, int n, bool discover) {
    n = std::clamp(n, 2, 4);
    std::vector<Seat<Cart>> seats;
    // Two players share a name on purpose: only the IDs tell them apart.
    std::vector<std::string> names = {"RADRACER", "RADRACER", "NEON FOX", "BIG RED"};
    if (!startSession(seats, n, stage, discover, names, {1.0f, 0.97f, 0.94f, 0.91f})) {
        std::printf("versus: could not open sockets\n");
        return 1;
    }
    int frames = 0;
    for (; frames < 60 * 450 && !allDone(seats); frames++) {
        for (int i = 1; i < n; i++)  // in discovery mode joiners press Start in the lobby, as a player would
            seats[size_t(i)].sys->pad.keys[gs::BTN_START] = discover && frames < 600 && frames % 30 == 10;
        for (auto& s : seats) s.sys->step();
        std::this_thread::sleep_for(std::chrono::microseconds(250));  // let loopback packets land, like a real frame gap
    }
    std::printf("versus: %d players on %s (%s), %.1f s\n", n, stageName(seats[0].cart.get(), stage),
                discover ? "found by LAN discovery" : "direct join", frames / 60.0);
    bool ok = allDone(seats);
    std::vector<float> myTime(static_cast<size_t>(n));
    std::vector<int> mySlot(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
        const auto r = seats[size_t(i)].cart->versusReport();
        myTime[size_t(i)] = r.time;
        mySlot[size_t(i)] = r.mySlot;
        std::printf("  %-9s slot %d  finished %d  place %d  time %6.2f  id %s\n", names[size_t(i)].c_str(), r.mySlot, r.finished,
                    r.rank, r.time, r.ids[r.mySlot].empty() ? "(host)" : r.ids[r.mySlot].substr(0, 8).c_str());
        if (!r.finished) std::printf("            stuck? %s\n", seats[size_t(i)].cart->debugLine().c_str());
    }
    // Everyone must see everyone else's finishing time as that player recorded it,
    // and everyone's own place must match the order of the times.
    for (int i = 0; i < n; i++) {
        const auto r = seats[size_t(i)].cart->versusReport();
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            const int s = mySlot[size_t(j)];
            if (!r.seen[s] || std::fabs(r.times[s] - myTime[size_t(j)]) > 0.1f) {
                std::printf("  %s sees %s's time as %.2f (really %.2f)\n", names[size_t(i)].c_str(), names[size_t(j)].c_str(), r.times[s],
                            myTime[size_t(j)]);
                ok = false;
            }
            if (r.names[s] != names[size_t(j)]) ok = false;
        }
        int place = 1;
        for (int j = 0; j < n; j++) place += myTime[size_t(j)] < myTime[size_t(i)] - 0.05f;
        if (r.rank != place) {
            std::printf("  %s shows place %d, times say %d\n", names[size_t(i)].c_str(), r.rank, place);
            ok = false;
        }
    }
    std::printf("%s\n", ok ? "VERSUS OK" : "VERSUS FAILED");
    return ok ? 0 : 1;
}

template <class Cart>
int recordQuadT(int n, int stage, const char* mp4Path) {
    n = std::clamp(n, 2, 4);
    std::vector<Seat<Cart>> seats;
    std::vector<std::string> names(DEMO_NAMES, DEMO_NAMES + 4);
    if (!startSession(seats, n, stage, false, names, {1.0f, 0.985f, 0.97f, 0.955f})) return 1;
    seats[0].sys->apu.init(48000);  // the film carries player 1's sound
    const std::string tmpVideo = std::string(mp4Path) + ".video.mp4", tmpAudio = std::string(mp4Path) + ".audio.raw";
    // The picture's size depends on the machine: the encoder starts with the first frame.
    auto encoder = [&](int fw, int fh) {
        const int scale = fw >= 1280 ? 1 : 2;
        const std::string enc = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s " + std::to_string(fw) + "x" + std::to_string(fh) +
                                " -r 60 -i - -vf scale=" + std::to_string(fw * scale) + ":" + std::to_string(fh * scale) +
                                ":flags=neighbor -c:v libx264 -crf 21 -preset medium -pix_fmt yuv420p '" + tmpVideo + "'";
        return popen(enc.c_str(), "w");
    };
    FILE* video = nullptr;
    FILE* audio = std::fopen(tmpAudio.c_str(), "wb");
    if (!audio) return 1;
    std::vector<uint32_t> frame;
    int fw = 0, fh = 0;
    std::vector<float> sound(800 * 2);
    int frames = 0, after = 0;
    while (frames < 60 * 240 && after < 60 * 6) {  // the whole race, then six seconds of results
        for (auto& s : seats) s.sys->step();
        for (auto& s : seats) s.sys->render();
        composite(seats, frame, fw, fh);
        if (!video && !(video = encoder(fw, fh))) {
            std::fclose(audio);
            std::printf("record: needs ffmpeg\n");
            return 1;
        }
        std::fwrite(frame.data(), 4, frame.size(), video);
        seats[0].sys->apu.render(sound.data(), 800);
        std::fwrite(sound.data(), sizeof(float), sound.size(), audio);
        if (allDone(seats)) after++;
        frames++;
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    if (video) pclose(video);
    std::fclose(audio);
    const std::string mux = "ffmpeg -loglevel error -y -i '" + tmpVideo + "' -f f32le -ar 48000 -ac 2 -i '" + tmpAudio +
                            "' -c:v copy -c:a aac -b:a 160k -shortest -movflags +faststart -map_metadata -1 '" + mp4Path + "'";
    const int rc = std::system(mux.c_str());
    std::remove(tmpVideo.c_str());
    std::remove(tmpAudio.c_str());
    std::printf("recorded %d-player match: %s (%.1f s)\n", n, mp4Path, frames / 60.0);
    return rc == 0 ? 0 : 1;
}

namespace {
void quadAudio(void* user, Uint8* stream, int len) {
    static_cast<gs::APU*>(user)->render(reinterpret_cast<float*>(stream), len / int(sizeof(float) * 2));
}
}  // namespace

// Text on the quad picture (fw x fh), in the console's 5x7 font: twice size on the 640-wide
// pictures, four times on the S3-64's 1280-wide one.
static void quadText(std::vector<uint32_t>& f, int fw, int fh, int x, int y, const std::string& s, uint32_t ink, bool box) {
    const int k = fw >= 1280 ? 4 : 2;  // pixels per font dot
    const int adv = 6 * k, w = int(s.size()) * adv + 3 * k, h = 10 * k;
    if (box)
        for (int yy = y - k - k / 2; yy < y - k - k / 2 + h; yy++)
            for (int xx = x - k - k / 2; xx < x - k - k / 2 + w; xx++)
                if (xx >= 0 && xx < fw && yy >= 0 && yy < fh) {
                    uint32_t& p = f[size_t(yy) * size_t(fw) + size_t(xx)];
                    p = 0xff000000u | ((p >> 2) & 0x3f3f3fu);  // darkened behind the text
                }
    for (size_t c = 0; c < s.size(); c++) {
        const uint8_t* g = gs::glyph(s[c]);
        if (!g) continue;
        for (int gy = 0; gy < 7; gy++)
            for (int gx = 0; gx < 5; gx++)
                if (g[gy * 5 + gx])
                    for (int d = 0; d < k * k; d++) {
                        const int px = x + int(c) * adv + gx * k + d % k, py = y + gy * k + d / k;
                        if (px >= 0 && px < fw && py >= 0 && py < fh) f[size_t(py) * size_t(fw) + size_t(px)] = ink;
                    }
    }
}

// Seat i's tag in the corner of its quarter.
static void quadLabel(std::vector<uint32_t>& f, int fw, int fh, int i, const std::string& s, bool human) {
    const int qw = fw / 2, qh = fh / 2, k = fw >= 1280 ? 4 : 2;
    quadText(f, fw, fh, (i % 2) * qw + 3 * k, (i / 2) * qh + qh - 11 * k, s, human ? 0xffffd23cu : 0xffb0b4c0u, true);
}

// A prompt across the middle of seat i's quarter.
static void quadHint(std::vector<uint32_t>& f, int fw, int fh, int i, const std::string& s) {
    const int qw = fw / 2, qh = fh / 2, k = fw >= 1280 ? 4 : 2;
    quadText(f, fw, fh, (i % 2) * qw + qw / 2 - int(s.size()) * 3 * k, (i / 2) * qh + qh * 54 / 100, s, 0xffffffffu, true);
}

template <class Cart>
int runQuadT(int n, int stage, bool demo) {
    const bool fullscreen = demo;
    n = std::clamp(n, 2, 4);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) return 1;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* win = SDL_CreateWindow("S3 QUAD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!ren) {
        std::fprintf(stderr, "quad: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);
    SDL_Texture* tex = nullptr;  // made with the first picture, at that machine's size
    int texW = 0, texH = 0;

    // Every seat starts with the autopilot; seat 0 also answers the keyboard.
    std::vector<Seat<Cart>> seats;
    std::vector<std::string> names;
    for (int i = 0; i < n; i++) names.push_back("PLAYER " + std::to_string(i + 1));
    std::vector<float> skill = {1.0f, 0.98f, 0.96f, 0.94f};
    if (!startSession(seats, n, stage, false, names, skill)) return 1;
    for (int i = 0; i < n; i++) seats[size_t(i)].sys->scripted = i == 0;  // player 1: keyboard (or the first pad)
    // The demo starts the cars 2 s apart so they race together (a real rally is 10 s).
    auto gaps = [&] {
        if constexpr (!std::is_same_v<Cart, rally::Rally>)  // (S3 RUN starts as a pack)
            for (auto& s : seats) s.cart->testStartGap(demo ? 2.0f : 10.0f);
    };
    gaps();

    // Drop-in controllers: a pad that is plugged in (any time) joins when START is pressed on it,
    // taking the next seat the computer is driving; unplugged, that seat goes back to the computer.
    struct Pad {
        SDL_GameController* c = nullptr;
        SDL_JoystickID id = -1;
        int seat = -1;  // -1: plugged in, not playing yet
        bool trig[2] = {};
    };
    std::vector<Pad> pads;
    std::vector<int> padOf(size_t(n), -1);  // seat -> pad index
    auto seatHuman = [&](int s) { return s == 0 || padOf[size_t(s)] >= 0; };
    auto release = [&](Pad& p) {
        if (p.seat < 0) return;
        gs::System& sys = *seats[size_t(p.seat)].sys;
        sys.pad = gs::Pad{};
        sys.ctl.connected = false;
        if (p.seat != 0) sys.scripted = false;  // the computer takes the car back
        padOf[size_t(p.seat)] = -1;
        p.seat = -1;
    };

    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = quadAudio;
    want.userdata = &seats[0].sys->apu;
    seats[0].sys->apu.init(48000);
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);

    std::printf("QUAD: %d cars. Keyboard drives player 1; press START on a controller to join. F11 fullscreen, Esc quits.\n", n);
    std::vector<uint32_t> frame;
    bool quit = false;
    int doneFor = 0, nextStage = stage;
    long tick = 0;
    uint64_t last = SDL_GetPerformanceCounter();
    double acc = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
                if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym == SDLK_F11) {
                    const bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    SDL_ShowCursor(fs ? SDL_ENABLE : SDL_DISABLE);
                }
                const int b = gs::System::keyButton(e.key.keysym.sym);
                if (b >= 0) seats[0].sys->pad.keys[b] = e.type == SDL_KEYDOWN;
            }
            if (e.type == SDL_CONTROLLERDEVICEADDED) {
                if (SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which)) {
                    const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
                    bool known = false;
                    for (const Pad& p : pads) known |= p.id == id;
                    if (known) SDL_GameControllerClose(c);
                    else pads.push_back({c, id, -1, {}});
                }
            }
            if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                for (size_t k = 0; k < pads.size(); k++)
                    if (pads[k].id == e.cdevice.which) {
                        release(pads[k]);
                        SDL_GameControllerClose(pads[k].c);
                        pads.erase(pads.begin() + long(k));
                        for (int& p : padOf)
                            if (p > int(k)) p--;
                        break;
                    }
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                for (size_t k = 0; k < pads.size(); k++) {
                    Pad& p = pads[k];
                    if (p.id != e.cbutton.which || p.seat >= 0) continue;
                    // Player 1 if nobody has that pad yet, else the first seat the computer drives.
                    int seat = padOf[0] < 0 ? 0 : -1;
                    for (int s = 1; s < n && seat < 0; s++)
                        if (!seatHuman(s)) seat = s;
                    if (seat < 0) break;  // all four taken
                    p.seat = seat;
                    padOf[size_t(seat)] = int(k);
                    seats[size_t(seat)].sys->scripted = true;
                    seats[size_t(seat)].sys->pad = gs::Pad{};
                }
            }
        }
        for (Pad& p : pads)
            if (p.seat >= 0) gs::System::readController(p.c, seats[size_t(p.seat)].sys->ctl, seats[size_t(p.seat)].sys->pad, p.trig);
        const uint64_t now = SDL_GetPerformanceCounter();
        acc += std::min(0.25, double(now - last) / double(SDL_GetPerformanceFrequency()));
        last = now;
        int steps = 0;
        while (acc >= 1.0 / 60 && steps < 4) {
            for (auto& s : seats) s.sys->step();
            acc -= 1.0 / 60;
            steps++;
            tick++;
            // A few seconds after everyone has finished, race again on the next stage.
            doneFor = allDone(seats) ? doneFor + 1 : 0;
            if (doneFor > 60 * 8) {
                nextStage = (nextStage + 1) % stageCount(seats[0].cart.get());
                startSession(seats, n, nextStage, false, names, skill);
                gaps();
                for (int i = 0; i < n; i++) seats[size_t(i)].sys->scripted = seatHuman(i);
                doneFor = 0;
            }
        }
        if (steps) {
            for (auto& s : seats) s.sys->render();
            int fw = 0, fh = 0;
            composite(seats, frame, fw, fh);
            for (int i = 0; i < n; i++) {
                const bool human = seatHuman(i);
                const std::string who = i == 0 ? (padOf[0] >= 0 ? "PAD" : "KEYS") : human ? "PAD" : "CPU";
                quadLabel(frame, fw, fh, i, "P" + std::to_string(i + 1) + " " + who, human);
                if (!human && (tick / 40) % 2) quadHint(frame, fw, fh, i, "PRESS START TO JOIN");
            }
            if (!tex || fw != texW || fh != texH) {
                if (tex) SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fw, fh);
                texW = fw, texH = fh;
            }
            SDL_UpdateTexture(tex, nullptr, frame.data(), fw * 4);
        }
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        int dw = ww, dh = ww * 3 / 4;
        if (dh > wh) { dh = wh; dw = wh * 4 / 3; }
        SDL_Rect dst{(ww - dw) / 2, (wh - dh) / 2, dw, dh};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (tex) SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_RenderPresent(ren);
    }
    if (dev) SDL_CloseAudioDevice(dev);  // before the consoles (and their samples) go away
    for (Pad& p : pads) SDL_GameControllerClose(p.c);
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    seats.clear();
    SDL_Quit();
    return 0;
}

QuadCart quadCartFor(const std::string& name) {
    return name == "run" ? QuadCart::Run : name == "rally32" ? QuadCart::Rally32 : name == "rally64" ? QuadCart::Rally64 : QuadCart::Rally;
}

// Run F with the template for that cartridge (and the 3D carts on the right machine).
#define S3_QUAD_DISPATCH(cart, F, ...)                                                       \
    do {                                                                                   \
        g_model = (cart) == QuadCart::Rally64 ? g32::Model::S3_64 : g32::Model::S3_32;    \
        switch (cart) {                                                                    \
            case QuadCart::Run: return F<rally::Rally>(__VA_ARGS__);                       \
            case QuadCart::Rally: return F<rc::RallyChamp>(__VA_ARGS__);                   \
            default: return F<rc32::Rally32>(__VA_ARGS__);                                 \
        }                                                                                  \
    } while (0)

int versusTest(int stage, int players, bool discover, QuadCart cart) { S3_QUAD_DISPATCH(cart, versusTestT, stage, players, discover); }
int recordQuad(int players, int stage, const char* mp4Path, QuadCart cart) { S3_QUAD_DISPATCH(cart, recordQuadT, players, stage, mp4Path); }
int runQuad(int players, int stage, QuadCart cart, bool demo) { S3_QUAD_DISPATCH(cart, runQuadT, players, stage, demo); }

namespace {
// A still of the quad screen after `seconds` of racing (all autopilot), labels and all, to FILE.png.
template <class Cart>
int quadShotT(int n, int seconds, const char* png) {
    std::vector<Seat<Cart>> seats;
    std::vector<std::string> names;
    for (int i = 0; i < n; i++) names.push_back("PLAYER " + std::to_string(i + 1));
    if (!startSession(seats, n, 0, false, names, {1.0f, 0.98f, 0.96f, 0.94f})) return 1;
    if constexpr (!std::is_same_v<Cart, rally::Rally>)
        if (std::getenv("S3_QUAD_GAP"))
            for (auto& s : seats) s.cart->testStartGap(float(std::atof(std::getenv("S3_QUAD_GAP"))));
    const bool realtime = std::getenv("S3_QUAD_REALTIME") != nullptr;  // paced like the live window (the network runs in real time)
    for (int f = 0; f < 60 * seconds; f++) {
        for (auto& s : seats) s.sys->step();
        if (realtime) std::this_thread::sleep_for(std::chrono::microseconds(16667));
    }
    if (std::getenv("S3_QUAD_BENCH")) {  // milliseconds to step and draw all four, the median of 120 frames
        std::vector<double> ms;
        std::vector<uint32_t> tmp;
        for (int f = 0; f < 120; f++) {
            const auto t0 = std::chrono::steady_clock::now();
            for (auto& s : seats) s.sys->step();
            for (auto& s : seats) s.sys->render();
            int w = 0, h = 0;
            composite(seats, tmp, w, h);
            ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(ms.begin(), ms.end());
        std::printf("four-way frame: %.2f ms median (budget 16.7 ms)\n", ms[ms.size() / 2]);
    }
    for (auto& s : seats) s.sys->render();
    std::vector<uint32_t> frame;
    int fw = 0, fh = 0;
    composite(seats, frame, fw, fh);
    for (int i = 0; i < n; i++) {
        quadLabel(frame, fw, fh, i, "P" + std::to_string(i + 1) + (i == 0 ? " KEYS" : i == 1 ? " PAD" : " CPU"), i < 2);
        if (i >= 2) quadHint(frame, fw, fh, i, "PRESS START TO JOIN");
    }
    const std::string raw = std::string(png) + ".raw";
    FILE* f = std::fopen(raw.c_str(), "wb");
    if (!f) return 1;
    std::fwrite(frame.data(), 4, frame.size(), f);
    std::fclose(f);
    const std::string cmd = "ffmpeg -v error -y -f rawvideo -pixel_format bgra -video_size " + std::to_string(fw) + "x" + std::to_string(fh) + " -i '" + raw + "' '" + png + "'";
    const int rc = std::system(cmd.c_str());
    std::remove(raw.c_str());
    return rc == 0 ? 0 : 1;
}
}  // namespace

int quadShot(int players, int seconds, const char* png, QuadCart cart) { S3_QUAD_DISPATCH(cart, quadShotT, players, seconds, png); }
