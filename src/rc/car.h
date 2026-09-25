// (3) RALLY - the car.
//
// A two-axle ("bicycle") vehicle model driven along the stage in road
// coordinates: distance s along the road, x across it, heading psi relative
// to it. Tyres make lateral force from slip angle up to a peak that depends
// on the surface, and share that grip with drive and braking (the friction
// circle), so power slides, lift-off oversteer, handbrake turns and
// understeer all fall out of the physics. The car can leave the ground over
// crests and jumps, land badly, hit things, roll, and wear out.
#pragma once
#include <cstdint>
#include <string>

#include "course.h"

namespace rc {

struct CarSpec {
    const char* name;
    const char* blurb;
    float mass;          // kg
    float torque;        // peak Nm
    float rpmPeak, rpmMax;
    float a, b, h;       // CG to front axle, to rear axle, CG height (m)
    float inertia;       // yaw inertia kg m^2
    float frontDrive;    // share of drive to the front axle (0 = rear drive)
    float grip;          // tyre factor
    float steerMax;      // radians at the wheels
    float ratios[6];
    float final, wheelR;
    uint16_t livery[16];
};
constexpr int NUM_CARS = 3;
const CarSpec& carSpec(int i);

struct CarInput {
    float steer = 0, throttle = 0, brake = 0;
    bool handbrake = false, shiftUp = false, shiftDown = false;
    bool analog = false;
    bool assist = true;  // traction help: drive is held below what the tyres can take
};

struct Damage {
    float engine = 0, suspension = 0, tyres = 0, body = 0;  // 0 fine .. 1 wrecked
    int puncture = 0;                                        // 0 none, -1 left, 1 right
    float total() const { return (engine + suspension + tyres + body) / 4; }
};

// Things that happened this frame (for sound, rumble, messages and the co-driver).
struct CarEvents {
    float landed = 0;     // landing impact m/s (0 if none)
    float hit = 0;        // impact speed with scenery m/s
    bool hitSoft = false;
    bool rockStrike = false, puncture = false;
    bool crashed = false; // started a roll or went off the road
    bool offRoad = false; // went over a drop or into the lake (recovered)
    bool launched = false;
    bool shifted = false;
    float bank = 0;       // pushed back off a snowbank
};

enum class CarState { Driving, Rolling, Recovering, Out };

class Car {
public:
    void reset(const Course& c, int seg, int spec);
    void step(const Course& c, const CarInput& in, float dt, bool manual);
    // Marshals or spectators put the car back on the road (costs the penalty, in seconds).
    void recover(const std::string& reason, float seconds) {
        state = CarState::Recovering;
        stateT = 0;
        u = v = r = 0;
        penalty += seconds;
        why = reason;
    }

    // Road position and motion
    float s = 0;        // metres along the road
    float x = 0;        // metres right of centre
    float psi = 0;      // heading relative to the road, radians (positive = pointing right)
    float u = 0, v = 0; // forward and sideways speed in the car's frame, m/s
    float r = 0;        // yaw rate rad/s (positive = turning right)
    float y = 0, vy = 0;  // height above sea (m) and vertical speed
    bool airborne = false;
    float airTime = 0;
    float pitch = 0, pitchRate = 0;  // nose up positive, radians
    float roll = 0, rollRate = 0;    // body roll / rolling over, radians
    float steerAngle = 0;            // front wheel angle now
    float steerIn = 0;               // smoothed steering input -1..1

    // Engine
    int gear = 1;       // -1 reverse, 1..6
    float rpm = 900;
    float throttle = 0;
    float shiftT = 0;   // seconds left in a gear change
    bool wheelspin = false;
    float slip = 0;     // how sideways the car is (radians of drift, for sound and dust)
    float skid = 0;     // 0..1 tyres scrubbing
    float compress = 0; // suspension compression (visual), metres

    Damage damage;
    CarState state = CarState::Driving;
    float stateT = 0;
    float penalty = 0;  // seconds lost to recoveries (added by the game)
    std::string why;    // what happened, for the screen
    CarEvents ev;
    int specId = 0;

    float speed() const;          // m/s over the ground along the car's path
    float kmh() const { return speed() * 3.6f; }
    float along() const;          // ds/dt, m/s
    float ground(const Course& c, float sAt) const;
    float groundSlope(const Course& c, float sAt) const;
    const Segment& seg(const Course& c) const;
    int segIndex(const Course& c) const;
    float drift() const;          // slide angle between heading and travel, radians

private:
    void airStep(const Course& c, const CarInput& in, float dt);
    void driveStep(const Course& c, const CarInput& in, float dt, bool manual);
    void collide(const Course& c, float dt);
    void edges(const Course& c, float dt);
    void startRoll(float speed, int dir, const std::string& reason);
    float engineTorque(float rpmNow) const;
    float reverseTimer_ = 0;
    float axPrev_ = 0;  // last longitudinal acceleration (weight transfer)
    uint32_t rng_ = 12345;
    float rnd();
    const Placed* hitList_[4] = {};  // objects already hit (so each counts once)
    int hitSeg_[4] = {};
    int hitNext_ = 0;
    float pullSide_ = 1;  // which way bent suspension pulls
    bool assist_ = true;  // this frame's driving help setting
};

}  // namespace rc
