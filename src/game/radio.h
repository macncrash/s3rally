// S3 RALLY - the in-car radio.
//
// Four stations, each playing a rotation of original songs, plus YOUR MUSIC
// (the player's own files). Stations keep
// "broadcasting" while you're tuned elsewhere, so flipping back lands you
// mid-song, like a real radio. Songs are written in a small tracker
// notation (see radio.cpp) and played on FM channels 2-7 plus PCM drums on
// channels 1-3.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "console/apu.h"

namespace rally {

constexpr int NUM_STATIONS = 4;   // built-in stations
constexpr int USER_STATION = NUM_STATIONS;  // "YOUR MUSIC", present when the music folder has tracks

// Songs from the player's own music folder, loaded (and converted if needed)
// on a background thread, a track or two at a time.
class UserMusic {
public:
    ~UserMusic();
    void scan(const std::string& dir, const std::string& cacheDir);
    size_t count() const { return tracks_.size(); }
    std::string title(size_t i) const;
    void request(size_t i);                 // start loading (no-op if loaded or loading)
    const gs::Sample* get(size_t i) const;  // nullptr until ready
    bool failed(size_t i) const;
    void keepOnly(size_t a, size_t b);      // free every other loaded track

private:
    struct Track {
        std::string path;
        std::atomic<int> state{0};  // 0 not loaded, 1 queued, 2 ready, 3 failed
        gs::Sample sample;
    };
    void run();
    bool decode(Track& t);
    std::vector<std::unique_ptr<Track>> tracks_;
    std::string cache_;
    std::thread worker_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<size_t> queue_;
    bool stop_ = false;
};

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
    int voiceFor(int station) const;  // announcer ident, or -1
    void loadUserMusic(const std::string& dir, const std::string& cacheDir);
    size_t userTracks() const { return user_.count(); }
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
    void userTick();
    void userStop();
    size_t userNext() const { return (userTrack_ + 1) % std::max<size_t>(1, user_.count()); }

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
    UserMusic user_;
    size_t userTrack_ = 0;
    double userPos_ = 0;
    bool userPlaying_ = false;
    int userSkips_ = 0;
    gs::Sample kick_, snare_, clap_, snareClap_, hat_, openHat_, crash_, ride_, tomHi_, tomLo_;
};

}  // namespace rally
