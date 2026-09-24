// S3 RALLY - sound driver: FM music sequencer, PCM drums, engine model,
// effects and the co-driver's voice.
// Channels: FM0 engine, FM1 rival engine, FM2 bass, FM3 lead, FM4 arpeggio,
// FM5 effects, PSG0-1 beeps, NOISE road/skid, PCM0 voice, PCM1 drums.
#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "console/apu.h"
#include "stages.h"

namespace rally {

enum Song { SONG_DESERT, SONG_FOREST, SONG_MOUNTAIN, SONG_LAKESIDE, SONG_TITLE, SONG_COUNT };

class Music {
public:
    explicit Music(gs::APU& apu);
    void play(int song);
    void stop();
    void toggle();
    bool enabled() const { return enabled_; }
    void tick();

private:
    struct Track {
        float bpm;
        std::vector<float> bass, lead, arp;
        std::vector<int> drums;
    };
    gs::APU& apu_;
    std::vector<Track> songs_;
    gs::Sample kick_, snare_, hat_, kickHat_, snareHat_;
    int song_ = -1;
    bool enabled_ = true;
    long step_ = 0;
    double timer_ = 0;
};

class CoDriver {
public:
    explicit CoDriver(gs::APU& apu) : apu_(apu) {
        for (auto& o : ok_) o = false;
    }
    ~CoDriver();
    void loadAsync(const std::string& dir);
    void say(int id, bool interrupt = false);
    void tick();
    int ready() const { return readyCount_; }

private:
    gs::APU& apu_;
    gs::Sample samples_[V_COUNT];
    std::atomic<bool> ok_[V_COUNT];
    std::atomic<int> readyCount_{0};
    std::thread worker_;
    std::atomic<bool> stop_{false};
    int pending_ = -1;
};

class Sfx {
public:
    explicit Sfx(gs::APU& apu);
    void tick();
    void engine(float rpm, float throttle, bool on);
    void rivalEngine(float freq, float vol);
    void road(float gravel, float skid, float splash);
    void beep(bool high);
    void crash(bool hard);
    void bump();
    void shift();
    void checkpoint();
    void fanfare();
    void menuMove();
    void menuSelect();

private:
    gs::APU& apu_;
    bool engineOn_ = false;
    bool rivalOn_ = false;
    struct Note {
        int delay;
        float freq;  // > 0: bell note on FM5; 0: silence PSG0; -1: silence PSG1
    };
    std::vector<Note> queue_;
};

}  // namespace rally
