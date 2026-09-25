#include "multi.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "console/system.h"
#include "game/rally.h"
#include "rc/game.h"

namespace {

constexpr uint16_t TEST_PORT = 47117;
constexpr uint16_t TEST_DISCOVERY = 47216;  // private, so nothing else on 47016 interferes
const char* DEMO_NAMES[4] = {"RADRACER", "NEON FOX", "BIG RED", "SIDEWAYS"};

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

// Boot n consoles; seat 0 hosts, the others join it over UDP.
template <class Cart>
bool startSession(std::vector<Seat<Cart>>& seats, int n, int stage, bool discover, const std::vector<std::string>& names,
                  const std::vector<float>& skill) {
    for (int i = 0; i < n; i++) {
        if (int(seats.size()) <= i) {
            Seat<Cart> s;
            s.sys = std::make_unique<gs::System>(true);
            s.cart = std::make_unique<Cart>();
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

// Paste each console's 320x224 screen into a 2x2 grid with thin dividers.
template <class Cart>
void composite(const std::vector<Seat<Cart>>& seats, std::vector<uint32_t>& out) {
    const int W = gs::SCREEN_W, H = gs::SCREEN_H;
    out.assign(size_t(W * 2) * H * 2, 0xff101010u);
    for (size_t i = 0; i < seats.size() && i < 4; i++) {
        const int ox = int(i % 2) * W, oy = int(i / 2) * H;
        for (int y = 0; y < H; y++) std::memcpy(&out[size_t(oy + y) * W * 2 + ox], &seats[i].sys->fb[size_t(y) * W], W * 4);
    }
    for (int y = 0; y < H * 2; y++) out[size_t(y) * W * 2 + W - 1] = out[size_t(y) * W * 2 + W] = 0xff000000u;
    for (int x = 0; x < W * 2; x++) out[size_t(H - 1) * W * 2 + x] = out[size_t(H) * W * 2 + x] = 0xff000000u;
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
    const std::string enc = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s 640x448 -r 60 -i - "
                            "-vf scale=1280:896:flags=neighbor -c:v libx264 -crf 21 -preset medium -pix_fmt yuv420p '" + tmpVideo + "'";
    FILE* video = popen(enc.c_str(), "w");
    FILE* audio = std::fopen(tmpAudio.c_str(), "wb");
    if (!video || !audio) {
        if (video) pclose(video);
        if (audio) std::fclose(audio);
        std::printf("record: needs ffmpeg\n");
        return 1;
    }
    std::vector<uint32_t> frame;
    std::vector<float> sound(800 * 2);
    int frames = 0, after = 0;
    while (frames < 60 * 240 && after < 60 * 6) {  // the whole race, then six seconds of results
        for (auto& s : seats) s.sys->step();
        for (auto& s : seats) s.sys->render();
        composite(seats, frame);
        std::fwrite(frame.data(), 4, frame.size(), video);
        seats[0].sys->apu.render(sound.data(), 800);
        std::fwrite(sound.data(), sizeof(float), sound.size(), audio);
        if (allDone(seats)) after++;
        frames++;
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    pclose(video);
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

template <class Cart>
int runQuadT(int n, int stage) {
    n = std::clamp(n, 2, 4);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) return 1;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* win = SDL_CreateWindow("S3-16 QUAD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!ren) {
        std::fprintf(stderr, "quad: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 448);

    // Controllers in order: pad k drives seat k (seat 0 also has the keyboard).
    std::vector<SDL_GameController*> pads;
    for (int i = 0; i < SDL_NumJoysticks() && int(pads.size()) < n; i++)
        if (SDL_IsGameController(i))
            if (SDL_GameController* c = SDL_GameControllerOpen(i)) pads.push_back(c);

    std::vector<Seat<Cart>> seats;
    std::vector<std::string> names;
    for (int i = 0; i < n; i++) names.push_back(i == 0 || i < int(pads.size()) ? "PLAYER " + std::to_string(i + 1) : DEMO_NAMES[i]);
    std::vector<float> skill = {1.0f, 0.98f, 0.96f, 0.94f};
    if (!startSession(seats, n, stage, false, names, skill)) return 1;
    for (int i = 0; i < n; i++) {
        seats[size_t(i)].sys->scripted = i == 0 || i < int(pads.size());  // humans use their pads; the rest are AI
        seats[size_t(i)].sys->ctl.connected = i < int(pads.size());
    }
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

    std::printf("QUAD: %d players, %zu controller(s). Keyboard drives player 1. Esc quits.\n", n, pads.size());
    std::vector<uint32_t> frame;
    bool trig[4][2] = {};
    bool quit = false;
    int doneFor = 0, nextStage = stage;
    uint64_t last = SDL_GetPerformanceCounter();
    double acc = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
                const int b = gs::System::keyButton(e.key.keysym.sym);
                if (b >= 0) seats[0].sys->pad.keys[b] = e.type == SDL_KEYDOWN;
            }
        }
        for (size_t i = 0; i < pads.size(); i++) gs::System::readController(pads[i], seats[i].sys->ctl, seats[i].sys->pad, trig[i]);
        const uint64_t now = SDL_GetPerformanceCounter();
        acc += std::min(0.25, double(now - last) / double(SDL_GetPerformanceFrequency()));
        last = now;
        int steps = 0;
        while (acc >= 1.0 / 60 && steps < 4) {
            for (auto& s : seats) s.sys->step();
            acc -= 1.0 / 60;
            steps++;
            // A few seconds after everyone has finished, race again on the next stage.
            doneFor = allDone(seats) ? doneFor + 1 : 0;
            if (doneFor > 60 * 8) {
                nextStage = (nextStage + 1) % stageCount(seats[0].cart.get());
                startSession(seats, n, nextStage, false, names, skill);
                doneFor = 0;
            }
        }
        if (steps) {
            for (auto& s : seats) s.sys->render();
            composite(seats, frame);
            SDL_UpdateTexture(tex, nullptr, frame.data(), 640 * 4);
        }
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        int dw = ww, dh = ww * 3 / 4;
        if (dh > wh) { dh = wh; dw = wh * 4 / 3; }
        SDL_Rect dst{(ww - dw) / 2, (wh - dh) / 2, dw, dh};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_RenderPresent(ren);
    }
    if (dev) SDL_CloseAudioDevice(dev);  // before the consoles (and their samples) go away
    for (auto* p : pads) SDL_GameControllerClose(p);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    seats.clear();
    SDL_Quit();
    return 0;
}

int versusTest(int stage, int players, bool discover, bool champ) {
    return champ ? versusTestT<rc::RallyChamp>(stage, players, discover) : versusTestT<rally::Rally>(stage, players, discover);
}
int recordQuad(int players, int stage, const char* mp4Path, bool champ) {
    return champ ? recordQuadT<rc::RallyChamp>(players, stage, mp4Path) : recordQuadT<rally::Rally>(players, stage, mp4Path);
}
int runQuad(int players, int stage, bool champ) {
    return champ ? runQuadT<rc::RallyChamp>(players, stage) : runQuadT<rally::Rally>(players, stage);
}
