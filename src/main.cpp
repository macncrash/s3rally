// S3-16 with the S3 RALLY cartridge inserted.
//
//   s3                 play (window, sound, keyboard or gamepad)
//   s3 --sim           headless: autopilot drives every stage, prints a report
//   s3 --sim --shots D also saves screenshots into directory D

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "console/system.h"
#include "game/rally.h"

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
static int record(const std::string& videoPath, const std::string& audioPath, int raceSeconds) {
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
    const Press script[] = {{330, gs::BTN_START}, {420, gs::BTN_DOWN}, {470, gs::BTN_UP}, {530, gs::BTN_START},
                            {600, gs::BTN_RIGHT}, {700, gs::BTN_START}, {760, gs::BTN_START}};
    std::vector<float> lag(9, 0.0f);
    int raceFrames = 0, g = 0;
    bool held = false;
    while (raceFrames < raceSeconds * 60 && g < 60 * 240) {
        for (int i = 0; i < gs::BTN_COUNT; i++) sys.pad.keys[i] = false;
        for (const Press& p : script)
            if (g >= p.frame && g < p.frame + 4) hold(p.b, true);
        if (cart.racing() || (g > 770 && !cart.finished())) {
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

int main(int argc, char** argv) {
    bool sim = false;
    const char* shots = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--sim")) sim = true;
        else if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!std::strcmp(argv[i], "--record") && i + 2 < argc) return record(argv[i + 1], argv[i + 2], 50);
    }
    if (sim) return simulate(shots);
    gs::System sys;
    rally::Rally cart;
    return sys.run(cart);
}
