#include "sound.h"

#include <SDL.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace rally {

namespace {

float noteFreq(const std::string& n) {
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    if (n.size() < 2 || n[0] < 'A' || n[0] > 'G') return 0;
    int semi = base[n[0] - 'A'];
    size_t i = 1;
    if (n[i] == '#') { semi++; i++; }
    int oct = std::atoi(n.c_str() + i);
    int midi = 12 * (oct + 1) + semi;
    return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
}

std::vector<float> parse(std::initializer_list<const char*> bars) {
    std::vector<float> out;
    for (const char* b : bars) {
        std::istringstream ss(b);
        std::string t;
        while (ss >> t) out.push_back(t == "." ? 0.0f : t == "-" ? -1.0f : noteFreq(t));
    }
    return out;
}

// Arpeggio bars: 8 notes, played twice per bar.
std::vector<float> arp(std::initializer_list<const char*> chords) {
    std::vector<float> out;
    for (const char* c : chords) {
        std::vector<float> notes;
        std::istringstream ss(c);
        std::string t;
        while (ss >> t) notes.push_back(noteFreq(t));
        for (int r = 0; r < 16; r++) out.push_back(notes[r % notes.size()]);
    }
    return out;
}

std::vector<int> drums(const char* pattern) {
    std::vector<int> out;
    std::istringstream ss(pattern);
    std::string t;
    while (ss >> t) out.push_back(t == "K" ? 1 : t == "S" ? 2 : t == "H" ? 3 : t == "KH" ? 4 : t == "SH" ? 5 : 0);
    return out;
}

gs::FMPatch bassPatch() {
    gs::FMPatch p;
    p.alg = 4;
    p.fb = 0.45f;
    p.op[0] = {1, 0.6f, 0.001f, 0.12f, 0.15f, 0.05f};
    p.op[1] = {1, 1.0f, 0.001f, 0.35f, 0.55f, 0.06f};
    p.op[2] = {2, 0.3f, 0.001f, 0.08f, 0.0f, 0.05f};
    p.op[3] = {0.5f, 0.55f, 0.001f, 0.3f, 0.5f, 0.06f};
    p.vol = 0.2f;
    return p;
}

gs::FMPatch leadPatch() {
    gs::FMPatch p;
    p.alg = 4;
    p.fb = 0.5f;
    p.op[0] = {1, 0.42f, 0.03f, 0.4f, 0.7f, 0.1f};
    p.op[1] = {1, 1.0f, 0.012f, 0.6f, 0.8f, 0.12f};
    p.op[2] = {3, 0.14f, 0.02f, 0.3f, 0.5f, 0.1f, 1.2f};
    p.op[3] = {1, 0.5f, 0.015f, 0.6f, 0.8f, 0.12f, 1.6f};
    p.vol = 0.12f;
    return p;
}

gs::FMPatch arpPatch() {
    gs::FMPatch p;
    p.alg = 4;
    p.op[0] = {3.5f, 0.45f, 0.001f, 0.12f, 0.0f, 0.1f};
    p.op[1] = {1, 1.0f, 0.001f, 0.25f, 0.0f, 0.1f};
    p.op[2] = {7, 0.18f, 0.001f, 0.08f, 0.0f, 0.1f};
    p.op[3] = {2, 0.35f, 0.001f, 0.2f, 0.0f, 0.1f};
    p.vol = 0.05f;
    return p;
}

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

gs::Sample synthDrum(int kind) {
    gs::Sample s;
    s.rate = 22050;
    const int n = kind == 3 ? 900 : 4400;
    s.data.resize(n);
    uint32_t seed = 12345;
    float prev = 0;
    for (int i = 0; i < n; i++) {
        float t = float(i) / s.rate;
        seed = seed * 1664525u + 1013904223u;
        float noise = float(int32_t(seed)) / 2147483648.0f;
        float v = 0;
        if (kind == 1 || kind == 4) {
            float f = 45 + 110 * std::exp(-t * 28);
            v += std::sin(6.2831853f * f * t) * std::exp(-t * 14) * 0.95f;
        }
        if (kind == 2 || kind == 5) v += (noise * 0.7f + std::sin(6.2831853f * 190 * t) * 0.4f) * std::exp(-t * 20) * 0.7f;
        if (kind == 3 || kind == 4 || kind == 5) {
            float hp = noise - prev;
            v += hp * std::exp(-t * 90) * 0.28f;
        }
        prev = noise;
        s.data[i] = v;
    }
    return s;
}

}  // namespace

// ------------------------------------------------------------ music

Music::Music(gs::APU& apu) : apu_(apu) {
    kick_ = synthDrum(1);
    snare_ = synthDrum(2);
    hat_ = synthDrum(3);
    kickHat_ = synthDrum(4);
    snareHat_ = synthDrum(5);

    songs_.resize(SONG_COUNT);
    songs_[SONG_DESERT] = {160,
        parse({"E1 . E2 . E1 . E2 . E1 . E2 . E1 . D2 .", "C2 . C3 . C2 . C3 . C2 . C3 . C2 . B1 .",
               "D2 . D3 . D2 . D3 . D2 . D3 . D2 . C#2 .", "B1 . B2 . B1 . B2 . B1 . B2 . D#2 . F#2 ."}),
        parse({"B4 - E5 - G5 - B5 - A5 - G5 - F#5 - E5 -", "G5 - - - E5 - C5 - E5 - G5 - C6 - B5 -",
               "A5 - - - F#5 - D5 - F#5 - A5 - D6 - C6 -", "B5 - - - - - D#5 - F#5 - - - B4 - - -",
               "E6 - - - D6 - B5 - G5 - - - E5 - G5 -", "C6 - - - B5 - G5 - E5 - - - G5 - C6 -",
               "D6 - - - C6 - A5 - F#5 - A5 - D6 - F#6 -", "F#6 - - - E6 - D#6 - B5 - - - . . . ."}),
        arp({"E4 G4 B4 E5 G5 E5 B4 G4", "C4 E4 G4 C5 E5 C5 G4 E4", "D4 F#4 A4 D5 F#5 D5 A4 F#4", "B3 D#4 F#4 B4 D#5 B4 F#4 D#4"}),
        drums("K . H K S . H . K K H . S . H SH")};
    songs_[SONG_FOREST] = {138,
        parse({"A1 - A2 . A1 - A2 . A1 - A2 . G1 - G2 .", "F1 - F2 . F1 - F2 . F1 - F2 . G1 - G2 .",
               "C2 - C3 . C2 - C3 . C2 - C3 . E2 - E3 .", "G1 - G2 . G1 - G2 . G1 - B1 . D2 - E2 ."}),
        parse({"E5 - - . D5 . C5 . D5 - E5 - A4 - - -", "F5 - - . E5 . D5 . C5 - D5 - C5 - A4 -",
               "G5 - - . E5 . C5 . E5 - G5 - C6 - - -", "B5 - A5 - G5 - D5 - G5 - - - . . . .",
               "A5 - - - G5 - E5 - A5 - - . C6 - B5 -", "A5 - - - F5 - - - C5 - D5 - F5 - - -",
               "G5 - - - E5 - C5 - G5 - A5 - C6 - D6 -", "E6 - - - D6 - B5 - G5 - - - A5 . B5 ."}),
        arp({"A3 C4 E4 A4 C5 A4 E4 C4", "F3 A3 C4 F4 A4 F4 C4 A3", "C4 E4 G4 C5 E5 C5 G4 E4", "G3 B3 D4 G4 B4 G4 D4 B3"}),
        drums("K . H . S . H . K . K H S . H H")};
    songs_[SONG_MOUNTAIN] = {146,
        parse({"D2 . D3 . D2 . D3 . D2 . D3 . D2 . C3 .", "A#1 . A#2 . A#1 . A#2 . A#1 . A#2 . A#1 . A1 .",
               "C2 . C3 . C2 . C3 . C2 . C3 . C2 . E2 .", "A1 . A2 . A1 . A2 . A1 . A2 . C#2 . E2 ."}),
        parse({"A4 - D5 - F5 - A5 - - - G5 - F5 - E5 -", "F5 - - - D5 - A#4 - D5 - F5 - A#5 - - -",
               "G5 - - - E5 - C5 - E5 - G5 - C6 - A#5 -", "A5 - - - - - - - C#5 - E5 - A5 - - -",
               "D6 - - - C6 - A5 - F5 - - - A5 - D6 -", "D6 - C6 - A#5 - - - F5 - D5 - F5 - A#5 -",
               "C6 - - - A#5 - G5 - E5 - G5 - C6 - E6 -", "C#6 - - - - - - - A5 - - - . . . ."}),
        arp({"D4 F4 A4 D5 F5 D5 A4 F4", "A#3 D4 F4 A#4 D5 A#4 F4 D4", "C4 E4 G4 C5 E5 C5 G4 E4", "A3 C#4 E4 A4 C#5 A4 E4 C#4"}),
        drums("K . H . S . H K . K H . S . H S")};
    songs_[SONG_LAKESIDE] = {126,
        parse({"F1 . . F2 . . F1 . F2 . F1 . . F2 E2 .", "E1 . . E2 . . E1 . E2 . E1 . . E2 D2 .",
               "D2 . . D3 . . D2 . D3 . D2 . . D3 C3 .", "G1 . . G2 . . G1 . G2 . G1 . B1 . D2 ."}),
        parse({"A5 - - - G5 - E5 - - - C5 - D5 - E5 -", "G5 - - - - - E5 - D5 - B4 - - - - -",
               "F5 - - - E5 - D5 - C5 - A4 - C5 - D5 -", "D5 - - - - - - - B4 - D5 - G5 - - -",
               "C6 - - - B5 - A5 - G5 - E5 - G5 - A5 -", "B5 - - - G5 - E5 - D5 - E5 - G5 - - -",
               "A5 - - - F5 - D5 - A5 - C6 - D6 - C6 -", "B5 - - - - - - - G5 - - - . . . ."}),
        arp({"F4 A4 C5 E5 A5 E5 C5 A4", "E4 G4 B4 D5 G5 D5 B4 G4", "D4 F4 A4 C5 F5 C5 A4 F4", "G3 B3 D4 F4 B4 F4 D4 B3"}),
        drums("K . H . S . H . K . H K S . H .")};
    songs_[SONG_TITLE] = {150,
        parse({"G1 . G2 . G1 G1 G2 . G1 . G2 . G1 G1 G2 .", "E1 . E2 . E1 E1 E2 . E1 . E2 . E1 E1 E2 .",
               "C2 . C3 . C2 C2 C3 . C2 . C3 . C2 C2 C3 .", "D2 . D3 . D2 D2 D3 . D2 . D3 . A1 . C#2 ."}),
        parse({"D5 - - - G5 - - - A5 - B5 - A5 - G5 -", "E5 - - - - - D5 - E5 - G5 - B4 - - -",
               "C5 - - - E5 - G5 - C6 - B5 - A5 - G5 -", "A5 - - - - - - - F#5 - G5 - A5 - - -",
               "B5 - - - A5 - G5 - D6 - - - B5 - - -", "G5 - - - E5 - G5 - B5 - A5 - G5 - E5 -",
               "E5 - G5 - C6 - - - B5 - A5 - G5 - E5 -", "D5 - - - F#5 - - - A5 - - - D6 - - -"}),
        arp({"G3 B3 D4 G4 B4 G4 D4 B3", "E3 G3 B3 E4 G4 E4 B3 G3", "C4 E4 G4 C5 E5 C5 G4 E4", "D4 F#4 A4 D5 F#5 D5 A4 F#4"}),
        drums("KH . H . SH . H . KH . KH H SH . H H")};
}

void Music::play(int song) {
    song_ = song;
    step_ = 0;
    timer_ = 0;
    apu_.setPatch(2, bassPatch());
    apu_.setPatch(3, leadPatch());
    apu_.setPatch(4, arpPatch());
    apu_.setPan(3, 0.2f);
    apu_.setPan(4, -0.4f);
}

void Music::stop() {
    song_ = -1;
    for (int ch : {2, 3, 4}) apu_.keyOff(ch);
}

void Music::toggle() {
    enabled_ = !enabled_;
    if (!enabled_)
        for (int ch : {2, 3, 4}) apu_.keyOff(ch);
}

void Music::tick() {
    if (song_ < 0 || !enabled_) return;
    const Track& t = songs_[song_];
    timer_ -= 1;
    if (timer_ > 0) return;
    timer_ += 60.0 * 60.0 / (t.bpm * 4);  // frames per 16th note
    const long i = step_++;
    auto voice = [&](int ch, const std::vector<float>& v) {
        float f = v[i % v.size()];
        if (f > 0) apu_.keyOn(ch, f);
        else if (f == 0) apu_.keyOff(ch);
    };
    voice(2, t.bass);
    voice(3, t.lead);
    voice(4, t.arp);
    const gs::Sample* d[] = {nullptr, &kick_, &snare_, &hat_, &kickHat_, &snareHat_};
    int k = t.drums[i % t.drums.size()];
    if (k) apu_.play(1, d[k], 0.55f);
}

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
}

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
            apu_.setPatch(5, bell);
            apu_.keyOn(5, n.freq);
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
    apu_.setPatch(5, p);
    apu_.keyOn(5, hard ? 70 : 110);
    apu_.setFreq(5, 30);
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
