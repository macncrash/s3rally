// S3 RALLY - sound driver: engine model, effects and the co-driver's voice.
// Channels: FM0 engine, FM1 rival engine, FM2-7 radio (see radio.h), FM8 effects,
// PSG0-1 beeps, NOISE road/skid, PCM0 voice, PCM1-3 radio drums.
#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "console/apu.h"
#include "stages.h"

namespace rally {

class CoDriver {
public:
    explicit CoDriver(gs::APU& apu) : apu_(apu) {
        for (auto& o : ok_) o = false;
    }
    ~CoDriver();
    void loadAsync(const std::string& dir);
    void say(int id, bool interrupt = false);
    void tick();
    bool speaking();  // true while a line is being spoken (the radio ducks)
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
    void turbo();
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
