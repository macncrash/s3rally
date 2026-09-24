// S3-16 with the S3 RALLY cartridge inserted.
//
//   s3                 play (window, sound, keyboard or gamepad)
//   s3 --sim           headless: autopilot drives every stage, prints a report
//   s3 --sim --shots D also saves screenshots into directory D
//   s3 --quad [N]      N consoles (2-4) racing each other over the network, split screen
//   s3 --record-quad F [N]  film an N-player autopilot match to F.mp4 (needs ffmpeg)
//   s3 --versus-test [STAGE] [--players N] [--discover]  headless multiplayer test

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "console/system.h"
#include "game/radio.h"
#include "game/rally.h"
#include "multi.h"
#include "version.h"

static int simulate(const char* shotDir) {
    gs::System sys(true);
    rally::Rally cart;
    auto t0 = std::chrono::steady_clock::now();
    sys.bootCart(cart);
    double bootMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("S3-16 headless simulation\n");
    std::printf("boot %.0f ms, sprite ROM %.2f MB / %d MB\n", bootMs, sys.vdp.romUsed() / 1048576.0,
                gs::SPRITE_ROM_SIZE >> 20);

    // Audio: render the title music offline and check levels are sane.
    {
        sys.apu.init(48000);
        std::vector<float> buf(800 * 2);
        double sum = 0, peak = 0;
        long n = 0;
        bool bad = false;
        for (int f = 0; f < 60 * 4; f++) {
            sys.step();
            sys.apu.render(buf.data(), 800);
            for (float v : buf) {
                if (!std::isfinite(v)) bad = true;
                sum += double(v) * v;
                peak = std::max(peak, double(std::fabs(v)));
                n++;
            }
        }
        std::printf("audio (title music, 4 s): rms %.3f peak %.3f %s\n", std::sqrt(sum / n), peak, bad ? "NaN!" : "ok");
    }

    // Render cost on the title screen.
    for (int i = 0; i < 30; i++) sys.step();
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 120; i++) {
        sys.step();
        sys.render();
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 120;
    std::printf("frame (logic + VDP render) %.2f ms  (budget 16.7 ms)\n\n", ms);

    std::vector<std::string> shots;
    int failures = 0;
    std::printf("%-10s %-8s %-6s %-10s %-10s %-10s %s\n", "STAGE", "RESULT", "POS", "TIME", "BEST LAP", "MIN CLOCK", "TILES");
    for (int s = 0; s < rally::NUM_STAGES; s++) {
        auto r = cart.simulateStage(s, shotDir ? &shots : nullptr, shotDir ? shotDir : "");
        std::printf("%-10s %-8s %-6d %-10.1f %-10.2f %-10.1f %d/%d\n", rally::stageDef(s).name, r.finished ? "FINISH" : "TIMEOUT",
                    r.position, r.raceTime, r.bestLap, r.minTimer, cart.tilesUsed(), gs::NUM_TILES);
        if (!r.finished) failures++;
    }
    if (shotDir) {
        sys.bootCart(cart);
        for (int i = 0; i < 200; i++) sys.step();
        sys.render();
        std::string p = std::string(shotDir) + "/title.png";
        if (sys.saveScreenshot(p)) shots.push_back(p);
        for (auto& s : shots) std::printf("screenshot %s\n", s.c_str());
    }
    std::printf("\n%s\n", failures ? "SOME STAGES NOT FINISHED" : "ALL STAGES FINISHED");
    return failures ? 1 : 0;
}

// Demo recorder: a scripted "player" works the real pad (menus, car select,
// then a race with human-style tapped steering and reaction lag). Every frame
// is written as raw BGRA and every 1/60 s of audio as raw stereo float, for
// muxing with ffmpeg.
static int record(const std::string& videoPath, const std::string& audioPath, int raceSeconds, bool secret) {
    gs::System sys(true);
    sys.scripted = true;
    sys.apu.init(48000);
    rally::Rally cart;
    sys.powerOn(cart);
    FILE* vf = std::fopen(videoPath.c_str(), "wb");
    FILE* af = std::fopen(audioPath.c_str(), "wb");
    if (!vf || !af) {
        if (vf) std::fclose(vf);
        if (af) std::fclose(af);
        std::fprintf(stderr, "cannot open output files\n");
        return 1;
    }
    std::vector<float> audio(800 * 2);
    auto hold = [&](gs::Button b, bool on) { sys.pad.keys[b] = on; };
    // Menu script: {frame, button} presses, each held for 4 frames.
    struct Press { int frame; gs::Button b; };
    const Press script[] = {{640, gs::BTN_START}, {700, gs::BTN_START}, {790, gs::BTN_DOWN}, {840, gs::BTN_UP},
                            {900, gs::BTN_START}, {970, gs::BTN_RIGHT}, {1070, gs::BTN_START}, {1130, gs::BTN_START},
                            {1750, gs::BTN_TURBO}, {2150, gs::BTN_Z}, {2450, gs::BTN_TURBO}, {2850, gs::BTN_Z},
                            {3300, gs::BTN_TURBO}};  // secret screen, then radio flips and turbo boosts mid-race
    const int shift = secret ? 0 : 310;  // without the secret screen everything happens sooner
    std::vector<float> lag(9, 0.0f);
    int raceFrames = 0, g = 0;
    bool held = false;
    while (raceFrames < raceSeconds * 60 && g < 60 * 240) {
        for (int i = 0; i < gs::BTN_COUNT; i++) sys.pad.keys[i] = false;
        for (const Press& p : script) {
            if (!secret && p.frame == 640) continue;  // that press only leaves the secret screen
            const int at = p.frame - shift;
            if (g >= at && g < at + 4) hold(p.b, true);
        }
        if (secret && g == 300) sys.typed = "s3ga\n";  // type the secret code on the title screen
        if (cart.racing() || (g > 1140 - shift && !cart.finished())) {
            // Human-ish driving: see the road, react ~8 frames later, tap the keys.
            rally::Input in = cart.botInput();
            lag.push_back(in.steer);
            float s = lag.front();
            lag.erase(lag.begin());
            float thresh = held ? 0.18f : 0.42f;  // hysteresis, like a thumb on a D-pad
            held = std::fabs(s) > thresh;
            hold(gs::BTN_RIGHT, held && s > 0);
            hold(gs::BTN_LEFT, held && s < 0);
            hold(gs::BTN_C, in.brake == 0 && (in.throttle > 0 || (g / 7) % 5 != 0));
            hold(gs::BTN_B, in.brake > 0);
            if (cart.racing()) raceFrames++;
        }
        sys.step();
        sys.render();
        std::fwrite(sys.fb, 4, gs::SCREEN_W * gs::SCREEN_H, vf);
        sys.apu.render(audio.data(), 800);
        std::fwrite(audio.data(), sizeof(float), audio.size(), af);
        g++;
        if (g == 200) std::this_thread::sleep_for(std::chrono::seconds(2));  // let voice samples load
    }
    std::fclose(vf);
    std::fclose(af);
    std::printf("recorded %d frames (%.1f s)\n", g, g / 60.0);
    return 0;
}

// Render each radio station offline: report levels and optionally write WAVs.
static void radioCheck(const char* wavDir, int seconds) {
    for (int s = 0; s < rally::NUM_STATIONS; s++) {
        gs::APU apu;
        apu.init(48000);
        rally::Radio radio(apu);
        radio.tuneTo(s);
        std::vector<float> buf(800 * 2), all;
        double sum = 0, peak = 0;
        long n = 0, clip = 0;
        for (int f = 0; f < 60 * seconds; f++) {
            radio.tick();
            apu.render(buf.data(), 800);
            for (float v : buf) {
                sum += double(v) * v;
                peak = std::max(peak, double(std::fabs(v)));
                if (std::fabs(v) > 0.97f) clip++;
                n++;
            }
            if (wavDir) all.insert(all.end(), buf.begin(), buf.end());
        }
        std::printf("radio %-28s rms %.3f peak %.3f clipped %.3f%%\n", radio.stationLine().c_str(), std::sqrt(sum / n), peak,
                    100.0 * clip / n);
        if (wavDir) {
            std::string p = std::string(wavDir) + "/station" + std::to_string(s) + ".wav";
            FILE* f = std::fopen(p.c_str(), "wb");
            if (!f) continue;
            auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
            auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
            uint32_t bytes = uint32_t(all.size() * 2);
            std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
            u32(16); u16(1); u16(2); u32(48000); u32(48000 * 4); u16(4); u16(16);
            std::fwrite("data", 1, 4, f); u32(bytes);
            for (float v : all) u16(uint16_t(int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767))));
            std::fclose(f);
        }
    }
}

// YOUR MUSIC: point the radio at a folder, tune in, check tracks load, play
// in stereo and advance.
static int musicTest(const char* dir) {
    gs::APU apu;
    apu.init(48000);
    rally::Radio radio(apu);
    radio.loadUserMusic(std::string(dir) + "/", std::string(dir) + "/.cache/");
    std::printf("music test: %zu tracks in %s\n", radio.userTracks(), dir);
    if (!radio.userTracks()) return 1;
    while (radio.station() != rally::USER_STATION && radio.next() != -1) {}
    if (radio.station() != rally::USER_STATION) radio.next();
    std::vector<float> buf(800 * 2);
    std::string last;
    int changes = 0;
    double sum = 0;
    long n = 0;
    for (int f = 0; f < 60 * 120 && changes < 3; f++) {
        radio.tick();
        apu.render(buf.data(), 800);
        for (float v : buf) sum += double(v) * v, n++;
        const std::string line = radio.songLine();
        if (line != last && line.rfind("LOADING", 0) != 0) {
            std::printf("  now playing: %s\n", line.c_str());
            last = line;
            changes++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const double rms = std::sqrt(sum / std::max(1L, n));
    std::printf("  rms %.3f  %s\n", rms, rms > 0.01 && changes >= 2 ? "MUSIC OK" : "MUSIC FAILED");
    return rms > 0.01 && changes >= 2 ? 0 : 1;
}

int main(int argc, char** argv) {
    bool sim = false;
    const char* shots = nullptr;
    const char* radioWav = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--sim")) sim = true;
        else if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!std::strcmp(argv[i], "--radio") && i + 1 < argc) radioWav = argv[++i];
        else if (!std::strcmp(argv[i], "--versus-test")) {
            const int st = i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])) ? std::atoi(argv[i + 1]) : 0;
            bool disc = false;
            int players = 2;
            for (int k = 1; k < argc; k++) {
                disc |= !std::strcmp(argv[k], "--discover");
                if (!std::strcmp(argv[k], "--players") && k + 1 < argc) players = std::atoi(argv[k + 1]);
            }
            return versusTest(st, players, disc);
        }
        else if (!std::strcmp(argv[i], "--quad")) {
            const int players = i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])) ? std::atoi(argv[i + 1]) : 4;
            return runQuad(players, 0);
        }
        else if (!std::strcmp(argv[i], "--record-quad") && i + 1 < argc) {
            const int players = i + 2 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 2][0])) ? std::atoi(argv[i + 2]) : 4;
            return recordQuad(players, 0, argv[i + 1]);
        }
        else if (!std::strcmp(argv[i], "--music-test") && i + 1 < argc) return musicTest(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--music-dir")) {
            gs::System s(true);
            std::printf("%s\n", s.dataPath("music/").c_str());
            return 0;
        }
        else if (!std::strcmp(argv[i], "--version")) {
            std::printf("S3 RALLY %s\n", S3_VERSION_STRING);
            return 0;
        }
        else if (!std::strcmp(argv[i], "--record") && i + 2 < argc) {
            bool secret = false;
            for (int k = 1; k < argc; k++) secret |= !std::strcmp(argv[k], "--secret");
            return record(argv[i + 1], argv[i + 2], 50, secret);
        }
    }
    if (radioWav) {
        radioCheck(radioWav, 150);
        return 0;
    }
    if (sim) {
        radioCheck(nullptr, 30);
        return simulate(shots);
    }
    // On the heap: the console carries a 286 KB framebuffer, far bigger than a
    // WebAssembly stack. (Declared so the cart outlives the system board.)
    auto cart = std::make_unique<rally::Rally>();
    auto sys = std::make_unique<gs::System>();
    return sys->run(*cart);
}
