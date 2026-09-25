// A trailer for S3 RALLY CHAMPIONSHIP, filmed on the console itself: the
// boot logo, the multi-cart menu, a stage start with the co-driver reading the
// first notes, then cuts across the five rallies in different views. The
// autopilot drives; the picture and sound go straight to ffmpeg.

#include "trailer.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "console/system.h"
#include "multicart.h"
#include "rc/game.h"

int recordRally(const char* mp4Path) {
    auto sys = std::make_unique<gs::System>(true);
    sys->scripted = true;  // voices load, as they would for a player
    sys->apu.init(48000);
    auto menu = std::make_unique<MultiCart>();
    auto champ = std::make_unique<rc::RallyChamp>();

    // Boot the cartridge once so the co-driver's voice is recorded before filming.
    sys->bootCart(*champ);
    for (int i = 0; i < 1200 && champ->voiceReady() < rc::P_COUNT; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::printf("trailer: co-driver ready (%d phrases)\n", champ->voiceReady());

    const std::string tmpVideo = std::string(mp4Path) + ".video.mp4", tmpAudio = std::string(mp4Path) + ".audio.raw";
    const std::string enc = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s 320x224 -r 60 -i - "
                            "-vf scale=960:672:flags=neighbor -c:v libx264 -crf 22 -preset medium -pix_fmt yuv420p '" + tmpVideo + "'";
    FILE* video = popen(enc.c_str(), "w");
    FILE* audio = std::fopen(tmpAudio.c_str(), "wb");
    if (!video || !audio) {
        if (video) pclose(video);
        if (audio) std::fclose(audio);
        std::printf("trailer: needs ffmpeg\n");
        return 1;
    }
    std::vector<float> sound(800 * 2);
    int frames = 0;
    auto film = [&](int n) {
        for (int i = 0; i < n; i++) {
            sys->step();
            sys->render();
            std::fwrite(sys->fb, 4, gs::SCREEN_W * gs::SCREEN_H, video);
            sys->apu.render(sound.data(), 800);
            std::fwrite(sound.data(), sizeof(float), sound.size(), audio);
            frames++;
        }
    };
    auto press = [&](gs::Button b) {
        sys->pad.keys[b] = true;
        film(4);
        sys->pad.keys[b] = false;
    };

    // 1. Power on: the boot logo and chime, then the multi-cart menu.
    sys->powerOn(*menu);
    film(175);
    film(140);
    // 2. The Rally Championship title (booted the same way START would).
    sys->bootCart(*champ);
    champ->testProfile("MACNCRASH");
    champ->testAutopilot(true);
    champ->testBotSkill(1.08f);  // a little braver than the tests, for the camera
    for (int i = 0; i < 100 && champ->voiceReady() < rc::P_COUNT; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    film(260);
    // 3. Finland, the jumps stage: on the line, the countdown, and away.
    champ->demoStart(2, rc::View::Chase, 1);
    film(480 + 60 * 22);
    // 4. Norway at night, from the driver's seat.
    champ->demoStage(5, rc::View::Cockpit, 520, 0);
    film(60 * 12);
    // 5. Australia: red dust, the rear-drive car sideways.
    champ->demoStage(9, rc::View::Chase, 700, 2);
    film(60 * 12);
    // 6. Italy: stone walls and hairpins.
    champ->demoStage(7, rc::View::Chase, 380, 1);
    film(60 * 10);
    // 7. Cyprus: the mountain road above the drop, from further back.
    champ->demoStage(12, rc::View::Far, 820, 1);
    film(60 * 12);
    // 8. Back to the title.
    press(gs::BTN_MODE);
    champ->testAutopilot(false);
    champ->demoTitle();
    film(60 * 5);

    pclose(video);
    std::fclose(audio);
    const std::string mux = "ffmpeg -loglevel error -y -i '" + tmpVideo + "' -f f32le -ar 48000 -ac 2 -i '" + tmpAudio +
                            "' -c:v copy -c:a aac -b:a 160k -shortest -movflags +faststart -map_metadata -1 '" + mp4Path + "'";
    const int rc = std::system(mux.c_str());
    std::remove(tmpVideo.c_str());
    std::remove(tmpAudio.c_str());
    std::printf("trailer: %s (%.1f s)\n", mp4Path, frames / 60.0);
    return rc == 0 ? 0 : 1;
}
