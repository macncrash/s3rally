#include "sound.h"

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace rally {

namespace {

constexpr int FX_CH = 8;  // FM channel reserved for effects (the radio owns 2-7)

gs::FMPatch enginePatch() {
    gs::FMPatch p;
    p.alg = 4;
    p.fb = 0.85f;
    p.op[0] = {0.5f, 0.9f, 0.02f, 1, 1, 0.2f};
    p.op[1] = {1, 1.0f, 0.02f, 1, 1, 0.2f};
    p.op[2] = {1, 0.55f, 0.02f, 1, 1, 0.2f, 3};
    p.op[3] = {2, 0.35f, 0.02f, 1, 1, 0.2f};
    p.vol = 0.0f;
    return p;
}

}  // namespace
// ------------------------------------------------------------ voice

CoDriver::~CoDriver() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
}

namespace {
// Quote a string for /bin/sh inside single quotes.
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}
}  // namespace

void CoDriver::loadAsync(const std::string& dir) {
#ifdef __EMSCRIPTEN__
    (void)dir;  // the browser speaks the lines live
#else
    if (worker_.joinable()) return;  // already loading or loaded
    worker_ = std::thread([this, dir]() {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        for (int i = 0; i < V_COUNT && !stop_; i++) {
            std::string path = dir + "v" + std::to_string(i) + ".wav";
            struct stat st;
            if (stat(path.c_str(), &st) != 0) {
                // Announcer lines in a deep voice, pace notes from the co-driver.
                bool announcer = i >= V_CHECKPOINT;
                std::string cmd = std::string("say -v ") + (announcer ? "Ralph -r 170" : "Daniel -r 205") + " -o " + shellQuote(path) +
                                  " --file-format=WAVE --data-format=LEI16@22050 " + shellQuote(VOICE_TEXT[i]) + " 2>/dev/null";
                if (std::system(cmd.c_str()) != 0) continue;
            }
            SDL_AudioSpec spec;
            Uint8* buf = nullptr;
            Uint32 len = 0;
            if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) continue;
            if (spec.format == AUDIO_S16LSB) {
                const int16_t* s = reinterpret_cast<const int16_t*>(buf);
                size_t n = len / 2 / spec.channels;
                samples_[i].rate = spec.freq;
                samples_[i].data.resize(n);
                for (size_t k = 0; k < n; k++) samples_[i].data[k] = s[k * spec.channels] / 32768.0f;
                ok_[i] = true;
                readyCount_++;
            }
            SDL_FreeWAV(buf);
        }
    });
#endif
}

#ifdef __EMSCRIPTEN__
// In a browser there is no `say` command: use the Web Speech API instead.
void CoDriver::say(int id, bool interrupt) {
    if (id < 0 || id >= V_COUNT) return;
    const bool announcer = id >= V_CHECKPOINT;
    EM_ASM(
        {
            if (!window.speechSynthesis) return;
            if ($2) speechSynthesis.cancel();
            const u = new SpeechSynthesisUtterance(UTF8ToString($0));
            u.pitch = $1 ? 0.55 : 1.0;
            u.rate = $1 ? 0.95 : 1.15;
            speechSynthesis.speak(u);
        },
        VOICE_TEXT[id], announcer, interrupt);
}

void CoDriver::tick() {}

bool CoDriver::speaking() { return EM_ASM_INT({ return window.speechSynthesis && speechSynthesis.speaking ? 1 : 0; }) != 0; }
#else
bool CoDriver::speaking() { return apu_.playing(0); }

void CoDriver::say(int id, bool interrupt) {
    if (id < 0 || id >= V_COUNT || !ok_[id]) return;
    if (!interrupt && apu_.playing(0)) {
        pending_ = id;
        return;
    }
    pending_ = -1;
    apu_.play(0, &samples_[id], 0.9f);
}

void CoDriver::tick() {
    if (pending_ >= 0 && !apu_.playing(0)) say(pending_);
}
#endif

// ------------------------------------------------------------ effects

Sfx::Sfx(gs::APU& apu) : apu_(apu) {}

void Sfx::tick() {
    for (auto& n : queue_) {
        if (--n.delay == 0 && n.freq <= 0) {
            apu_.tone(n.freq < 0 ? 1 : 0, 0, 0);  // only the channel that queued it
        } else if (n.delay == 0) {
            gs::FMPatch bell;
            bell.alg = 4;
            bell.op[0] = {3.5f, 0.5f, 0.001f, 0.3f, 0.0f, 0.2f};
            bell.op[1] = {1, 1.0f, 0.001f, 0.6f, 0.0f, 0.3f};
            bell.op[2] = {7, 0.2f, 0.001f, 0.1f, 0.0f, 0.1f};
            bell.op[3] = {2, 0.4f, 0.001f, 0.5f, 0.0f, 0.3f};
            bell.vol = 0.2f;
            apu_.setPatch(FX_CH, bell);
            apu_.keyOn(FX_CH, n.freq);
        }
    }
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Note& n) { return n.delay <= 0; }), queue_.end());
}

void Sfx::engine(float rpm, float throttle, bool on) {
    if (!on) {
        if (engineOn_) apu_.keyOff(0);
        engineOn_ = false;
        return;
    }
    if (!engineOn_) {
        apu_.setPatch(0, enginePatch());
        apu_.keyOn(0, 40, 0);
        engineOn_ = true;
    }
    apu_.setFreq(0, 34 + rpm * 128);
    apu_.setVol(0, 0.1f + throttle * 0.07f);
}

void Sfx::rivalEngine(float freq, float vol) {
    if (vol < 0.005f) {
        if (rivalOn_) apu_.keyOff(1);
        rivalOn_ = false;
        return;
    }
    if (!rivalOn_) {
        gs::FMPatch p = enginePatch();
        p.fb = 0.6f;
        apu_.setPatch(1, p);
        apu_.keyOn(1, freq, 0);
        rivalOn_ = true;
    }
    apu_.setFreq(1, freq);
    apu_.setVol(1, vol);
}

void Sfx::road(float gravel, float skid, float splash) {
    float vol = std::max({gravel * 0.05f, skid * 0.13f, splash * 0.2f});
    float rate = skid > 0.1f ? 9000 + skid * 5000 : splash > 0.1f ? 6000 : 1800 + gravel * 2600;
    apu_.noise(vol, rate);
}

void Sfx::beep(bool high) { apu_.tone(0, high ? 1760 : 880, 0.1f), queue_.push_back({high ? 30 : 10, 0}); }

void Sfx::crash(bool hard) {
    apu_.noiseBurst(hard ? 0.45f : 0.25f, 1200, hard ? 0.35f : 0.12f);
    gs::FMPatch p;
    p.alg = 4;
    p.fb = 0.9f;
    p.op[0] = {1.4f, 0.9f, 0.001f, 0.15f, 0.0f, 0.1f};
    p.op[1] = {1, 1.0f, 0.001f, 0.3f, 0.0f, 0.1f};
    p.op[2] = {0.5f, 0.5f, 0.001f, 0.1f, 0.0f, 0.1f};
    p.op[3] = {1, 0.6f, 0.001f, 0.25f, 0.0f, 0.1f};
    p.vol = hard ? 0.45f : 0.25f;
    apu_.setPatch(FX_CH, p);
    apu_.keyOn(FX_CH, hard ? 70 : 110);
    apu_.setFreq(FX_CH, 30);
}

void Sfx::bump() { crash(false); }

void Sfx::shift() {
    apu_.noiseBurst(0.08f, 3000, 0.04f);
}

void Sfx::checkpoint() {
    const float n[] = {1046.5f, 1318.5f, 1568.0f, 2093.0f};
    for (int i = 0; i < 4; i++) queue_.push_back({1 + i * 5, n[i]});
}

void Sfx::fanfare() {
    const float n[] = {523.3f, 659.3f, 784.0f, 1046.5f, 784.0f, 1046.5f, 1318.5f};
    for (int i = 0; i < 7; i++) queue_.push_back({1 + i * 7, n[i]});
}

void Sfx::menuMove() { apu_.tone(1, 1200, 0.06f), queue_.push_back({4, -1}); }

void Sfx::menuSelect() {
    queue_.push_back({1, 1568.0f});
    queue_.push_back({5, 2093.0f});
}

}  // namespace rally

namespace rally {

// Turbo: a rush of air plus a rising FM whine.
void Sfx::turbo() {
    apu_.noiseBurst(0.35f, 9000, 0.7f);
    gs::FMPatch p;
    p.alg = 4;
    p.fb = 0.7f;
    p.op[0] = {1, 0.6f, 0.01f, 1.2f, 0.4f, 0.3f};
    p.op[1] = {1, 1.0f, 0.02f, 1.5f, 0.3f, 0.4f};
    p.op[2] = {2.01f, 0.3f, 0.01f, 1.0f, 0.3f, 0.3f};
    p.op[3] = {1, 0.5f, 0.02f, 1.5f, 0.3f, 0.4f};
    p.vol = 0.16f;
    p.glide = 0.00006f;  // slow sweep upwards
    apu_.setPatch(FX_CH, p);
    apu_.keyOn(FX_CH, 180);
    apu_.setFreq(FX_CH, 1100);
}

}  // namespace rally
