// S3 RALLY - the in-car radio.
//
// Three stations, each playing a rotation of original songs. Stations keep
// "broadcasting" while you're tuned elsewhere, so flipping back lands you
// mid-song, like a real radio. Songs are written in a small tracker
// notation (see radio.cpp) and played on FM channels 2-7 plus PCM drums on
// channels 1-3.
#pragma once
#include <string>
#include <vector>

#include "console/apu.h"

namespace rally {

constexpr int NUM_STATIONS = 3;

struct Step {
    enum Type : uint8_t { None, Hold, Off, Note, Chug } type = None;
    char chord = 0;  // 0 = patch default, else M m 5 6 7
    float f = 0, f2 = 0;  // note, optional bend target
};

struct SongTrack {
    std::string name;
    gs::FMPatch patch;
    float pan = 0;
    std::vector<Step> steps;
};

struct Song {
    std::string title, artist;
    float bpm = 120;
    int stepsPerBeat = 4;
    float echoTime = 0.3f, echoFb = 0.3f, echoWet = 0.25f;
    std::vector<SongTrack> tracks;
    std::vector<std::string> drums;  // one token per step
    int steps = 0;
    float stepFrames() const { return 3600.0f / (bpm * stepsPerBeat); }
    float frames() const { return steps * stepFrames(); }
};

struct Station {
    const char* freq;
    const char* name;
    const char* genre;
    int voice;  // ident read by the announcer
    std::vector<int> songs;
    int song = 0;
    int step = 0;
    long leftAt = 0;  // frame we tuned away
};

class Radio {
public:
    explicit Radio(gs::APU& apu);
    void tick();
    int next();  // tune to the next station (or off); returns the new station, -1 = off
    void tuneTo(int station);
    int station() const { return station_; }
    void duck(bool on);

    // For the HUD.
    int cardFrames() const { return card_; }
    std::string stationLine() const;
    std::string songLine() const;

private:
    void startSong(bool fromTop);
    void doStep();
    void release();
    void playDrums(const std::string& tok);

    gs::APU& apu_;
    std::vector<Song> songs_;
    Station stations_[NUM_STATIONS];
    int station_ = 0;
    long frame_ = 0;
    double timer_ = 0;
    int gap_ = 0, static_ = 0, card_ = 0;
    bool ducked_ = false;
    struct Live {
        float cur = 0;
        char chord = 0;
        int offIn = 0, bendIn = 0;
        float bendTo = 0;
    } live_[6];
    gs::Sample kick_, snare_, clap_, snareClap_, hat_, openHat_, crash_, ride_, tomHi_, tomLo_;
};

}  // namespace rally
