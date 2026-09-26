// (3) RALLY - replays that prove a stage time.
//
// The game runs on a fixed 1/60 s step, so a stage time follows from the
// driver's inputs. A replay holds those inputs (quantized, so the live car and
// the replay see exactly the same numbers) and, once a second, a snapshot of
// the whole car. The score server runs `s3 --verify-run`, which replays each
// second from its snapshot and checks it lands on the next one: the physics
// is checked a second at a time, so the last-bit differences between
// platforms' maths libraries never build up into a false alarm.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "car.h"
#include "course.h"

namespace rc {

// One frame of driver input, as recorded (and as the car actually receives it).
struct RunInput {
    int16_t steer = 0;        // -32767..32767
    uint8_t throttle = 0, brake = 0;
    uint8_t flags = 0;        // handbrake, shift up, shift down, analog, assist
    bool operator==(const RunInput& o) const {
        return steer == o.steer && throttle == o.throttle && brake == o.brake && flags == o.flags;
    }
};
RunInput quantize(const CarInput& in);
CarInput expand(const RunInput& q);

// The stage rule the game applies after every step: a car beached off the road
// making no progress for ten seconds is pushed back out, at +5 s. True if it was.
bool pushOutRule(Car& car, const Course& c, int& noProgress, float& progressS);

struct RunCheckpoint {
    CarSnapshot car;
    int32_t noProgress = 0;
    float progressS = 0;
};

struct Replay {
    static constexpr int EVERY = 60;              // a checkpoint every second
    static constexpr int MAX_FRAMES = 60 * 60 * 20;  // twenty minutes
    std::string game = "rally";
    std::string build;
    int stage = 0, spec = 0;
    bool manual = false;
    Damage damage;                                // the car's condition at the start
    float claimed = 0;                            // stage time + penalties, seconds
    std::vector<RunCheckpoint> checkpoints;       // before frames 0, 60, 120, ...
    std::vector<RunInput> inputs;                 // one per frame of the stage

    std::string encode() const;
    static bool decode(const std::string& bytes, Replay& out, std::string& why);
};

// Records a stage as it is driven. The game calls begin() at GO, frame() before
// each step with the quantized input, and finish() when the stage is done.
class RunRecorder {
public:
    void begin(const std::string& game, const std::string& build, int stage, const Car& car, const Course& c, bool manual);
    void frame(const RunInput& in, const Car& car, const Course& c, int noProgress, float progressS);
    void finish(float total) { rep_.claimed = total, done_ = true; }
    void clear() { rep_ = Replay(), active_ = done_ = false; }
    bool active() const { return active_ && !done_; }
    bool done() const { return done_; }
    const Replay& replay() const { return rep_; }

private:
    Replay rep_;
    bool active_ = false, done_ = false;
};

enum class Verdict { Accepted, Review, Rejected };
struct VerifyResult {
    Verdict verdict = Verdict::Rejected;
    float time = 0;        // the stage time the replay produces
    std::string reason;
};
VerifyResult verifyReplay(const Replay& rep);

std::string base64Encode(const std::string& in);
bool base64Decode(const std::string& in, std::string& out);

}  // namespace rc
