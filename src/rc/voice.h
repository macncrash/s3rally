// (3) RALLY - the co-driver's voice.
//
// Every phrase in PHRASE_TEXT is recorded once as a PCM sample (on macOS the
// `say` command makes them the first time the game runs; they are kept in the
// save folder). A pace note is a queue of phrases played back to back, the
// way a co-driver reads: "left four, over crest, into right two".
// In a browser the whole call is spoken by the Web Speech API instead.
#pragma once
#include <atomic>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "console/apu.h"
#include "course.h"

namespace rc {

class Voice {
public:
    explicit Voice(gs::APU& apu);
    ~Voice();
    void loadAsync(const std::string& dir);
    void loadRange(const std::string& dir, int first, int step);
    void call(const std::vector<int>& phrases, bool urgent = false);  // queue a call
    void say(int phrase, bool urgent = false) { call({phrase}, urgent); }
    void tick();
    void clear();
    bool speaking();
    int ready() const { return ready_; }
    size_t queued() const { return queue_.size(); }

private:
    gs::APU& apu_;
    gs::Sample samples_[P_COUNT];
    std::atomic<bool> ok_[P_COUNT];
    std::atomic<int> ready_{0};
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::deque<int> queue_;
    int gap_ = 0;
};

}  // namespace rc
