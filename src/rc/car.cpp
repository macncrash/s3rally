#include "car.h"

#include <algorithm>
#include <cmath>

#include "console/vdp.h"

namespace rc {

namespace {
constexpr float PI = 3.14159265f;
constexpr uint16_t C(int r, int g, int b) { return uint16_t(((r & 15) << 8) | ((g & 15) << 4) | (b & 15)); }
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float sgn(float v) { return v < 0 ? -1.0f : 1.0f; }

// Tyre curve: rises to the peak at slip angle `peak`, then falls off by `fall`.
float tyre(float alpha, float peak, float fall) {
    const float a = std::fabs(alpha) / peak;
    float f;
    if (a < 1) f = std::sin(a * PI / 2);
    else f = std::max(1 - fall, 1 - fall * std::min(1.0f, (a - 1) / 2.5f));
    return sgn(alpha) * f;
}
}  // namespace

// Car palette roles: 1 black, 2 dark grey, 3 body, 4 body shade, 5-6 stripes,
// 7-8 glass, 9-10 lights, 11 metal, 12 tread, 13 body highlight, 14 white, 15 decal.
const CarSpec& carSpec(int i) {
    static const CarSpec specs[NUM_CARS] = {
        {"GS-C  CELICA 4WD", "BALANCED. FORGIVING.", 1230, 420, 4500, 7400, 1.20f, 1.35f, 0.50f, 1850, 0.45f, 1.00f, 0.52f,
         {3.10f, 2.10f, 1.62f, 1.30f, 1.06f, 0.88f}, 5.2f, 0.32f,
         {0, C(1, 1, 1), C(4, 4, 4), C(15, 15, 15), C(11, 12, 13), C(2, 4, 13), C(14, 2, 2), C(1, 2, 4), C(6, 8, 11), C(10, 0, 0),
          C(15, 6, 4), C(9, 9, 9), C(6, 6, 6), C(15, 15, 15), C(15, 15, 15), C(15, 12, 0)}},
        {"GS-D  DELTA INTEGRALE", "MORE POWER. LOVES TO SLIDE.", 1210, 450, 4800, 7600, 1.15f, 1.40f, 0.52f, 1800, 0.40f, 0.97f, 0.54f,
         {3.00f, 2.05f, 1.60f, 1.28f, 1.05f, 0.87f}, 5.3f, 0.32f,
         {0, C(1, 1, 1), C(4, 4, 4), C(14, 2, 2), C(10, 1, 1), C(15, 15, 15), C(15, 12, 0), C(1, 2, 4), C(6, 8, 11), C(10, 0, 0),
          C(15, 6, 4), C(9, 9, 9), C(6, 6, 6), C(15, 7, 6), C(15, 15, 15), C(2, 4, 13)}},
        {"GS-S  STRATOS RWD", "REAR DRIVE LEGEND. HANDFUL.", 980, 330, 5500, 7800, 1.30f, 1.00f, 0.46f, 1300, 0.0f, 0.95f, 0.58f,
         {2.90f, 2.00f, 1.55f, 1.25f, 1.02f, 0.86f}, 5.4f, 0.31f,
         {0, C(1, 1, 1), C(4, 4, 4), C(2, 9, 4), C(1, 6, 3), C(15, 15, 15), C(15, 13, 0), C(1, 2, 4), C(6, 8, 11), C(10, 0, 0),
          C(15, 6, 4), C(9, 9, 9), C(6, 6, 6), C(5, 13, 6), C(15, 15, 15), C(15, 13, 0)}},
    };
    return specs[std::clamp(i, 0, NUM_CARS - 1)];
}

float Car::rnd() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / 16777216.0f;
}

const Segment& Car::seg(const Course& c) const { return c.segs[size_t(segIndex(c))]; }
int Car::segIndex(const Course& c) const { return std::clamp(int(s * U / SEG), 0, c.N - 1); }

float Car::ground(const Course& c, float sAt) const {
    const float z = sAt * U;
    const int i = std::clamp(int(z / SEG), 0, c.N - 1);
    const Segment& g = c.segs[size_t(i)];
    const float t = clampf((z - g.z1) / SEG, 0, 1);
    return (g.y1 + (g.y2 - g.y1) * t) / U;
}

float Car::groundSlope(const Course& c, float sAt) const {
    const int i = std::clamp(int(sAt * U / SEG), 0, c.N - 1);
    const Segment& g = c.segs[size_t(i)];
    return (g.y2 - g.y1) / SEG;
}

float Car::along() const { return u * std::cos(psi) - v * std::sin(psi); }
float Car::speed() const { return std::sqrt(u * u + v * v); }
float Car::drift() const { return std::fabs(u) < 1 ? 0 : std::atan2(v, std::fabs(u)); }

void Car::reset(const Course& c, int seg, int spec) {
    *this = Car();
    specId = spec;
    s = (seg * SEG + SEG * 0.5f) / U;
    y = ground(c, s);
    rng_ = 777u + uint32_t(seg);
}

float Car::engineTorque(float rpmNow) const {
    const CarSpec& cs = carSpec(specId);
    const float p = rpmNow / cs.rpmPeak;
    float t;
    if (p < 1) t = 0.55f + 0.45f * std::sin(p * PI / 2);
    else t = 1 - 0.55f * (rpmNow - cs.rpmPeak) / (cs.rpmMax - cs.rpmPeak) * 0.8f;
    if (rpmNow > cs.rpmMax) t = 0;  // limiter
    return cs.torque * std::max(0.0f, t) * (1 - 0.45f * damage.engine);
}

void Car::step(const Course& c, const CarInput& in, float dt, bool manual) {
    ev = CarEvents{};
    assist_ = in.assist;
    stateT += dt;
    if (state == CarState::Out) {
        u *= 0.95f;
        v *= 0.9f;
        rpm += (900 - rpm) * 0.1f;
        return;
    }
    if (state == CarState::Rolling) {
        // Tumbling to a stop: slide, spin over, lose bits.
        const float spd = speed();
        const float dec = std::min(spd, 9.0f * dt);
        if (spd > 0.01f) {
            u -= u / spd * dec;
            v -= v / spd * dec;
        }
        roll += rollRate * dt;
        rollRate *= std::pow(0.55f, dt);
        r *= std::pow(0.3f, dt);
        psi += r * dt;
        const float ds = along() / std::max(0.2f, 1 - seg(c).kappa * x);
        s += ds * dt;
        x += (u * std::sin(psi) + v * std::cos(psi)) * dt;
        psi -= seg(c).kappa * ds * dt;
        y = ground(c, s);
        rpm += (900 - rpm) * 0.1f;
        if (stateT > 2.4f && speed() < 1.5f) {
            state = CarState::Recovering;
            stateT = 0;
        }
        return;
    }
    if (state == CarState::Recovering) {
        // Marshals and spectators push it back on its wheels and onto the road.
        u = v = r = 0;
        roll += (std::round(roll / (2 * PI)) * 2 * PI - roll) * std::min(1.0f, dt * 3);
        x += (clampf(x, -seg(c).hw * 0.6f, seg(c).hw * 0.6f) - x) * std::min(1.0f, dt * 2);
        psi += (0 - psi) * std::min(1.0f, dt * 2.5f);
        y = ground(c, s);
        vy = 0;
        if (stateT > 2.2f) {
            roll = 0;
            psi = 0;
            state = CarState::Driving;
            stateT = 0;
            airborne = false;
            if (damage.total() >= 0.999f) {
                state = CarState::Out;
                why = "RETIRED";
            }
        }
        return;
    }

    // Gearbox.
    const CarSpec& cs = carSpec(specId);
    if (shiftT > 0) shiftT -= dt;
    if (gear > 0) {
        if (manual) {
            if (in.shiftUp && gear < 6) { gear++; shiftT = 0.10f; ev.shifted = true; }
            if (in.shiftDown && gear > 1) { gear--; shiftT = 0.10f; ev.shifted = true; }
        } else if (shiftT <= 0 && !airborne) {
            const float wheelRpm = std::fabs(u) / cs.wheelR * cs.final * 60 / (2 * PI);
            if (gear < 6 && wheelRpm * cs.ratios[gear - 1] > cs.rpmMax * 0.95f) { gear++; shiftT = 0.22f; ev.shifted = true; }
            else if (gear > 1 && wheelRpm * cs.ratios[gear - 2] < cs.rpmMax * 0.62f) { gear--; shiftT = 0.18f; ev.shifted = true; }
        }
    }
    // Reverse: hold the brake when stopped.
    if (gear > 0 && std::fabs(u) < 0.6f && in.brake > 0.5f && in.throttle < 0.1f) {
        reverseTimer_ += dt;
        if (reverseTimer_ > 0.5f) gear = -1;
    } else if (gear < 0 && (in.throttle > 0.1f)) {
        gear = 1;
        reverseTimer_ = 0;
    } else if (gear > 0) {
        reverseTimer_ = 0;
    }

    const int sub = 8;
    const float h = dt / sub;
    for (int k = 0; k < sub; k++) {
        if (airborne) airStep(c, in, h);
        else driveStep(c, in, h, manual);
        if (state != CarState::Driving) break;
    }
    if (state == CarState::Driving) {
        collide(c, dt);
        edges(c, dt);
    }
    // Stay on the plan: the Frenet frame goes singular at the centre of a bend.
    const float k = seg(c).kappa;
    if (std::fabs(k) > 1e-4f) {
        const float lim = 0.8f / std::fabs(k);
        x = clampf(x, -lim, lim);
    }
    x = clampf(x, -40, 40);
    s = clampf(s, 0, (c.N - 2) * SEG / U);
    // Keep the heading near the road: a spin carries on round, not off the plan.
    while (psi > PI) psi -= 2 * PI;
    while (psi < -PI) psi += 2 * PI;
}

void Car::driveStep(const Course& c, const CarInput& in, float dt, bool manual) {
    (void)manual;
    const CarSpec& cs = carSpec(specId);
    const Segment& g = seg(c);
    const float L = cs.a + cs.b;
    const float m = cs.mass;

    // Surface under the car: the road, the verge or the rough beyond it.
    const float edge = g.hw;
    const float ax_ = std::fabs(x);
    SurfInfo sf = SURF[g.surf];
    float gripK = cs.grip * (1 - 0.25f * damage.suspension) * (1 - 0.2f * damage.tyres);
    float rollRes = sf.roll;
    bool rough = sf.rough;
    if (ax_ > edge) {
        const uint8_t side = x < 0 ? g.left : g.right;
        if (side == gs::GROUND_SNOWWALL) { gripK *= 0.8f; rollRes += 0.25f; }
        else if (ax_ < edge + 1.5f) { gripK *= 0.9f; rollRes += 0.04f; }  // loose verge
        else { gripK *= 0.6f; rollRes += 0.07f; rough = true; }          // grass, stones, ditch
    }
    const float mu = sf.mu * gripK;

    // Steering: faster hands on a keyboard, less lock at speed.
    // Keys are on or off: turn in progressively, let go quickly (self-centring).
    const bool centring = std::fabs(in.steer) < std::fabs(steerIn) || in.steer * steerIn < 0;
    const float rate = in.analog ? 12.0f : centring ? 7.0f : 2.6f;
    steerIn += clampf(in.steer - steerIn, -rate * dt, rate * dt);
    float lock = cs.steerMax / (1 + std::fabs(u) / 32);
    // Steering help (keyboard, or traction help on): full input asks for the tightest turn the tyres
    // can actually hold at this speed, not full lock, so a held key never spins the car.
    if ((!in.analog || in.assist) && std::fabs(u) > 6) {
        const float useful = (cs.a + cs.b) * mu * GRAV * 1.05f / (u * u) + sf.peak * 0.9f;
        lock = std::min(lock, useful);
    }
    float pull = 0;
    if (damage.suspension > 0.2f) pull += (damage.suspension - 0.2f) * 0.06f;
    if (damage.puncture) pull += 0.03f * damage.puncture;
    steerAngle = steerIn * lock + pull;

    // Load on each axle (weight moves forward under braking, back under power).
    const float transfer = 0.7f * m * axPrev_ * cs.h / L;  // (softened: springs and dampers take some)
    float Fzf = m * GRAV * cs.b / L - transfer;
    float Fzr = m * GRAV * cs.a / L + transfer;
    Fzf = std::max(Fzf, 0.15f * m * GRAV);
    Fzr = std::max(Fzr, 0.15f * m * GRAV);

    // Engine and drive.
    const float ratio = gear < 0 ? -3.3f : cs.ratios[std::clamp(gear, 1, 6) - 1];
    const float wheelRpm = std::fabs(u) / cs.wheelR * std::fabs(ratio) * cs.final * 60 / (2 * PI);
    const float thr = gear < 0 ? in.brake : in.throttle;
    throttle = thr;
    float engineRpm = std::max(wheelRpm, gear == 1 || gear < 0 ? 1300 + thr * 3300 * (1 - std::min(1.0f, std::fabs(u) / 12)) : 900.0f);
    float drive = 0;
    if (shiftT <= 0) drive = engineTorque(engineRpm) * thr * ratio * cs.final * 0.88f / cs.wheelR;
    // Brakes (70 % front) and the handbrake (rear, locks it).
    const float brakeIn = gear < 0 ? in.throttle : in.brake;
    const float brakeF = brakeIn * m * GRAV * 1.1f;
    float Fxf = drive * cs.frontDrive, Fxr = drive * (1 - cs.frontDrive);
    const float dirU = u >= 0 ? 1.0f : -1.0f;
    Fxf -= dirU * brakeF * 0.7f;
    Fxr -= dirU * brakeF * 0.3f;
    const bool hb = in.handbrake;
    if (hb) Fxr -= dirU * mu * Fzr * 0.8f;
    const float maxF = mu * Fzf, maxR = mu * Fzr;
    // Traction help: keep the driven wheels from spinning up, which also keeps them gripping sideways.
    if (in.assist && drive > 0) {
        Fxf = std::min(Fxf, 0.75f * maxF + (Fxf - drive * cs.frontDrive));
        Fxr = std::min(Fxr, 0.75f * maxR + (Fxr - drive * (1 - cs.frontDrive)));
    }
    // Traction limits: anything over the grip is wheelspin (or a locked wheel).
    wheelspin = false;
    if (std::fabs(Fxf) > maxF) { if (drive * Fxf > 0) wheelspin = true; Fxf = sgn(Fxf) * maxF * 0.92f; }
    if (std::fabs(Fxr) > maxR) { if (drive * Fxr > 0) wheelspin = true; Fxr = sgn(Fxr) * maxR * 0.92f; }
    if (wheelspin) engineRpm = std::min(cs.rpmMax * 1.02f, engineRpm + 2200 * thr);
    rpm += (engineRpm - rpm) * std::min(1.0f, dt * 12);

    // Lateral grip left over after drive and braking (a friction ellipse: tyres
    // keep more sideways grip than a pure circle would give). The rear has a
    // touch more grip than the front, so the car is stable until provoked.
    float latF = std::sqrt(std::max(0.0f, maxF * maxF - 0.7f * Fxf * Fxf));
    float latR = 1.18f * std::sqrt(std::max(0.0f, maxR * maxR - 0.7f * Fxr * Fxr));
    if (hb) latR *= 0.25f;
    if (damage.puncture) latR *= 0.75f, latF *= 0.85f;

    // Slip angles and tyre forces.
    const float uu = std::max(std::fabs(u), 2.0f) * (u >= 0 ? 1 : -1);
    const float af = std::atan2(v + cs.a * r, std::fabs(uu)) - steerAngle * (u >= 0 ? 1 : -1);
    const float ar = std::atan2(v - cs.b * r, std::fabs(uu));
    const float Fyf = -latF * tyre(af, sf.peak, sf.fall);
    const float Fyr = -latR * tyre(ar, sf.peak, sf.fall);

    // Resistances: rolling (surface), air, and the hill.
    const float slope = groundSlope(c, s);
    const float resist = rollRes * m * GRAV + 0.45f * u * std::fabs(u) * (1 + 0.0f);
    const float hill = -m * GRAV * slope * std::cos(psi);

    const float cosd = std::cos(steerAngle), sind = std::sin(steerAngle);
    float fx = Fxr + Fxf * cosd - Fyf * sind + hill - (std::fabs(u) > 0.05f ? sgn(u) * resist : 0);
    float fy = Fyr + Fyf * cosd + Fxf * sind;
    float mz = cs.a * (Fyf * cosd + Fxf * sind) - cs.b * Fyr;

    float du = fx / m + v * r;
    float dv = fy / m - u * r;
    float dr = mz / cs.inertia;
    // Stability help: when the car yaws much faster than the steering asks, damp it (a spin caught early).
    if (in.assist && std::fabs(u) > 5) {
        const float rKin = u * std::tan(steerAngle) / L;
        const float excess = r - rKin;
        if (std::fabs(excess) > 0.15f) dr -= 3.5f * (excess - (excess > 0 ? 0.15f : -0.15f));
    }
    // At walking pace the tyre model gets stiff: blend to plain kinematics.
    const float slowK = clampf((std::fabs(u) - 1.0f) / 3.0f, 0, 1);
    u += du * dt;
    if (slowK < 1) {
        const float rKin = u * std::tan(steerAngle) / L;
        r += (rKin - r) * (1 - slowK) * std::min(1.0f, dt * 20);
        v *= 1 - (1 - slowK) * std::min(1.0f, dt * 10);
    }
    v += dv * dt * (0.2f + 0.8f * slowK);
    r += dr * dt * (0.2f + 0.8f * slowK);
    if (std::fabs(u) < 0.3f && std::fabs(drive) < 1 && brakeIn > 0.1f) u *= 0.8f;  // held on the brakes
    axPrev_ = clampf(du - v * r, -12, 12);

    // Move along the road.
    const float ds = along() / std::max(0.2f, 1 - g.kappa * x);
    s += ds * dt;
    x += (u * std::sin(psi) + v * std::cos(psi)) * dt;
    psi += (r - g.kappa * ds) * dt;

    // Sliding and sound cues.
    slip = drift();
    skid = clampf((std::fabs(af) + std::fabs(ar)) / (2 * sf.peak) - 0.7f, 0, 1) * clampf(std::fabs(u) / 8, 0, 1);
    if (wheelspin) skid = std::max(skid, 0.5f);

    // Body roll and pitch from the forces (visual only).
    roll += (clampf(-fy / m * 0.012f, -0.12f, 0.12f) - roll) * std::min(1.0f, dt * 6);
    pitch += (clampf(-axPrev_ * 0.006f, -0.06f, 0.06f) + std::atan(slope) - pitch) * std::min(1.0f, dt * 8);

    // Vertical: follow the ground unless it falls away faster than gravity can pull
    // the car down (a crest taken fast, or the lip of a jump): then we fly.
    const float g0 = ground(c, s);
    const float sAhead = groundSlope(c, s + 0.4f), sBack = groundSlope(c, s - 0.4f);
    const float curv = (sAhead - sBack) / 0.8f;  // d2h/ds2
    if (ds > 5 && ds * ds * curv < -GRAV) {
        airborne = true;
        airTime = 0;
        y = g0;
        vy = ds * sBack;
        // The rear wheels leave last, so the nose starts to drop.
        pitch = std::atan(sBack);
        pitchRate = -ds * (std::atan(sBack) - std::atan(sAhead)) / L * 0.15f;
        ev.launched = true;
    } else {
        // Compression in dips (visual bounce), and a hard bottoming-out costs suspension.
        const float lift = ds * ds * curv;
        compress += (clampf(lift / 120, -0.06f, 0.15f) - compress) * std::min(1.0f, dt * 10);
        if (lift > 3.5f * GRAV) damage.suspension = std::min(1.0f, damage.suspension + (lift - 3.5f * GRAV) * dt * 0.004f);
        y = g0;
        vy = ds * slope;
    }

    // Rough roads wear the tyres and the suspension, more when sideways and fast.
    if (rough) {
        const float spd = std::fabs(u);
        damage.tyres = std::min(1.0f, damage.tyres + dt * spd * spd * 0.0000045f * (1 + 4 * std::fabs(slip)));
        if (spd > 22) damage.suspension = std::min(1.0f, damage.suspension + dt * (spd - 22) * 0.00035f);
    }
    if (g.flags & F_BUMPS && std::fabs(u) > 18) compress += (rnd() - 0.5f) * 0.02f;
    // A worn-out tyre can go at any time.
    if (!damage.puncture && damage.tyres > 0.6f && rnd() < dt * (damage.tyres - 0.6f) * 0.25f) {
        damage.puncture = rnd() < 0.5f ? -1 : 1;
        ev.puncture = true;
    }
    // A cooked engine loses more as the radiator fails.
    if (damage.body > 0.6f) damage.engine = std::min(1.0f, damage.engine + dt * 0.002f);
}

void Car::airStep(const Course& c, const CarInput& in, float dt) {
    const CarSpec& cs = carSpec(specId);
    const float ds = along() / std::max(0.2f, 1 - seg(c).kappa * x);
    s += ds * dt;
    x += (u * std::sin(psi) + v * std::cos(psi)) * dt;
    psi += (r - seg(c).kappa * ds) * dt;
    u -= 0.45f * u * std::fabs(u) / cs.mass * dt;
    r *= 1 - 0.2f * dt;
    vy -= GRAV * dt;
    y += vy * dt;
    airTime += dt;
    // In the air the throttle lifts the nose and the brake drops it.
    pitchRate += (in.throttle * 0.9f - in.brake * 1.4f) * dt;
    pitch += pitchRate * dt;
    // Engine revs up with the wheels off the ground.
    rpm += (cs.rpmMax * (0.6f + 0.4f * in.throttle) - rpm) * std::min(1.0f, dt * 4);
    const float g0 = ground(c, s);
    if (y > g0 || (airTime < 0.06f && y > g0 - 0.03f)) return;

    // Landing.
    airborne = false;
    const float slope = groundSlope(c, s);
    const float impact = ds * slope - vy;  // how fast we're meeting the ground, m/s
    const float nose = pitch - std::atan(slope);  // positive: rear lands first (good), negative: nose first
    y = g0;
    vy = ds * slope;
    pitchRate = 0;
    pitch = std::atan(slope);
    compress = clampf(impact * 0.03f, 0, 0.2f);
    ev.landed = impact;
    const float sideways = std::fabs(drift()) + std::fabs(psi) * 0.3f;
    if (impact > 3.5f) {
        const float over = impact - 3.5f;
        damage.suspension = std::min(1.0f, damage.suspension + over * 0.035f * (nose < -0.15f ? 2.0f : 1.0f));
        if (nose < -0.2f) damage.engine = std::min(1.0f, damage.engine + over * 0.02f);  // nose into the ground
        u *= std::max(0.6f, 1 - over * 0.05f);
    }
    if ((impact > 5 && sideways > 0.7f) || impact > 11 || (impact > 6 && nose < -0.45f)) {
        startRoll(speed(), v > 0 ? 1 : -1, impact > 11 ? "HUGE LANDING" : "LANDED WRONG");
    }
}

void Car::startRoll(float spd, int dir, const std::string& reason) {
    state = CarState::Rolling;
    stateT = 0;
    rollRate = dir * clampf(4 + spd * 0.25f, 5, 14);
    r = (rnd() - 0.5f) * 3;
    airborne = false;
    const float hurt = clampf(spd / 45, 0.1f, 0.6f);
    damage.body = std::min(1.0f, damage.body + hurt);
    damage.suspension = std::min(1.0f, damage.suspension + hurt * 0.6f);
    damage.engine = std::min(1.0f, damage.engine + hurt * 0.3f);
    ev.crashed = true;
    why = reason;
}

void Car::collide(const Course& c, float dt) {
    (void)dt;
    const int i = segIndex(c);
    const float halfW = 0.9f;
    for (int k = i; k <= std::min(c.N - 1, i + 1); k++) {
        for (const Placed& o : c.segs[size_t(k)].objs) {
            const ObjInfo& info = OBJ[o.type];
            if (info.hit == 0 || info.w <= 0) continue;
            if (std::fabs(x - o.off) >= halfW + info.w) continue;
            // Each object is dealt with once (it stays "hit" until we are well past it).
            if (std::find(hitList_, hitList_ + 4, &o) != hitList_ + 4) continue;
            hitList_[hitNext_] = &o;
            hitSeg_[hitNext_] = k;
            hitNext_ = (hitNext_ + 1) % 4;
            const float spd = speed();
            if (info.hit == 1) {  // bushes, signs, bales: slow you, rattle you
                u *= 0.85f;
                damage.body = std::min(1.0f, damage.body + spd * 0.002f);
                ev.hit = spd * 0.3f;
                ev.hitSoft = true;
                continue;
            }
            if (info.hit == 3) {  // a snowbank lump: bounce off it
                v = -v * 0.3f - sgn(o.off - x) * 2;
                u *= 0.9f;
                ev.bank = spd;
                continue;
            }
            if (o.type == O_ROCK) {  // clipping a rock: a jolt, maybe a puncture or bent suspension
                ev.rockStrike = true;
                ev.hit = spd * 0.4f;
                vy += std::min(3.0f, spd * 0.08f);
                u *= 0.8f;
                damage.suspension = std::min(1.0f, damage.suspension + spd * 0.006f);
                if (!damage.puncture && rnd() < clampf(spd / 50, 0.1f, 0.6f)) {
                    damage.puncture = x < o.off ? 1 : -1;
                    ev.puncture = true;
                }
                continue;
            }
            // Something solid: a tree, a wall, a boulder.
            ev.hit = spd;
            if (spd > 22) {
                startRoll(spd, x < o.off ? -1 : 1, o.type == O_STONEWALL ? "HIT A WALL" : "BIG CRASH");
            } else {
                damage.body = std::min(1.0f, damage.body + spd * 0.02f);
                damage.suspension = std::min(1.0f, damage.suspension + spd * 0.012f);
                // Stop dead and bounce off it.
                u = -u * 0.15f;
                v = -v * 0.2f;
                r = (rnd() - 0.5f) * 2.5f;
                x += (x < o.off ? -1 : 1) * 0.3f;
            }
            return;
        }
    }
    for (int n = 0; n < 4; n++)
        if (hitList_[n] && std::abs(hitSeg_[n] - i) > 3) hitList_[n] = nullptr;
}

void Car::edges(const Course& c, float dt) {
    (void)dt;
    const Segment& g = seg(c);
    const float over = std::fabs(x) - g.hw;
    if (over <= 0) return;
    const uint8_t side = x < 0 ? g.left : g.right;
    const float lat = u * std::sin(psi) + v * std::cos(psi);  // speed across the road
    const float out = sgn(x) * lat;                           // positive: heading further off
    // Driving help: the soft ground beyond the edge soaks up speed heading away from the road.
    if (assist_ && out > 0 && over > 0.5f && side != gs::GROUND_SNOWWALL) {
        const float k = std::min(1.0f, dt * 2.5f * std::min(1.0f, over / 2));
        const float cut = out * k;
        const float c0 = std::cos(psi), s0 = std::sin(psi);
        u -= sgn(x) * cut * s0;
        v -= sgn(x) * cut * c0;
    }
    if (side == gs::GROUND_SNOWWALL && over > 0.5f) {
        // Snow banks: lean on them, they push you back, they cost you speed.
        x = sgn(x) * (g.hw + 0.5f);
        if (out > 0) {
            const float push = out * 1.3f;
            v -= sgn(x) * push * std::cos(psi);
            u -= std::fabs(push) * 0.25f;
            ev.bank = out;
            if (out > 9) damage.body = std::min(1.0f, damage.body + (out - 9) * 0.02f);
        }
        return;
    }
    if (side == gs::GROUND_DROP && over > 1.2f) {
        ev.offRoad = true;
        startRoll(speed(), x > 0 ? 1 : -1, "OFF THE ROAD");
        penalty += 18;  // hauled back up by spectators
        return;
    }
    if (over > 15) {  // lost in the fields or the forest: marshals point you back to the road
        ev.offRoad = true;
        state = CarState::Recovering;
        stateT = -1;
        u = v = r = 0;
        penalty += 10;
        why = "LOST THE ROAD";
        return;
    }
    if (side == gs::GROUND_WATER && over > 1.5f) {
        ev.offRoad = true;
        state = CarState::Recovering;
        stateT = -3;  // longer: pulled out of the lake
        u = v = r = 0;
        damage.engine = std::min(1.0f, damage.engine + 0.15f);
        penalty += 12;
        why = "IN THE LAKE";
    }
}

}  // namespace rc
