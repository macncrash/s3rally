// S3 RALLY
//
// The road is drawn by the VDP's road generator: each frame this cart
// projects the 3D course (curves, hills, surfaces) into one set of road
// parameters per scanline, and the hardware textures it. Everything that
// stands beside the road is a hardware-scaled sprite.

#include "rally.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace rally {

namespace {

constexpr float DT = 1.0f / 60.0f;
constexpr float HALF = gs::SCREEN_W / 2.0f;
constexpr float HORIZON = 96;
constexpr float VS = gs::SCREEN_H - HORIZON;
constexpr float CAM_H = 1000;
constexpr float DEPTH = 0.84f;
constexpr float PLAYER_Z = CAM_H * DEPTH;
constexpr int DRAW = 300;
constexpr int SPRITE_DRAW = 170;
constexpr float MAX = SEG * 60;
constexpr float CAR_W = 640;
constexpr float CAR_ASPECT = 112.0f / 192.0f;
constexpr float GEAR_TOP[6] = {0, 0.24f, 0.43f, 0.62f, 0.81f, 1.04f};

const char* NAMES[15] = {"R.OKADA", "T.VIREN", "L.MORENO", "S.HALVORSEN", "J.DUBOIS", "P.KOWAL", "E.RINNE", "C.BAPTISTE",
                         "D.MARSH", "N.ISHIDA", "F.CORTI", "B.NYSTROM", "A.PETROV", "H.WEBER", "G.SILVA"};

struct SurfPhys {
    float grip, drag, top;
    bool dusty;
};
const SurfPhys SURF[5] = {{1.0f, 0, 1.0f, true}, {1.3f, 0, 1.03f, false}, {0.85f, 0.05f, 0.95f, true}, {0.7f, 0.02f, 0.96f, true}, {0.75f, 0.5f, 0.72f, false}};

inline float mod(float a, float n) {
    float r = std::fmod(a, n);
    return r < 0 ? r + n : r;
}
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

std::string fmtTime(float t) {
    int m = int(t / 60), s = int(std::fmod(t, 60.0f)), c = int(std::fmod(t * 100, 100.0f));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d'%02d\"%02d", m, s, c);
    return buf;
}

uint16_t lerpColor(uint16_t a, uint16_t b, float t) {
    t = clampf(t, 0, 1);
    auto ch = [&](int sh) { return int(std::lround(((a >> sh) & 15) + (((b >> sh) & 15) - ((a >> sh) & 15)) * t)); };
    return gs::rgb4(ch(8), ch(4), ch(0));
}

std::string ordinal(int n) {
    const char* suf = (n % 100 >= 11 && n % 100 <= 13) ? "TH" : n % 10 == 1 ? "ST" : n % 10 == 2 ? "ND" : n % 10 == 3 ? "RD" : "TH";
    return std::to_string(n) + suf;
}

}  // namespace

// ================================================================ setup

void Rally::init(gs::System& sys) {
    sys_ = &sys;
    vdp_ = &sys.vdp;
    buildArt(*vdp_, art_);
    radio_ = std::make_unique<Radio>(sys.apu);
    if (!sys.headless) {  // the player's own songs become the YOUR MUSIC station
        const std::string dir = sys.dataPath("music/");
        radio_->loadUserMusic(dir, sys.dataPath("music-cache/"));
        std::printf("Your music folder: %s (%zu tracks)\n", dir.c_str(), radio_->userTracks());
    }
    sfx_ = std::make_unique<Sfx>(sys.apu);
    voice_ = std::make_unique<CoDriver>(sys.apu);
    if (!sys.headless || sys.scripted) voice_->loadAsync(sys.dataPath("voice/"));
    loadRecords();
    profile_ = Profile::parse(sys.loadBlob("profile.txt"));
    flakes_.resize(70);
    for (auto& f : flakes_) f = {float(std::rand() % 320), float(std::rand() % 224), 1 + (std::rand() % 3) * 0.5f};
    stage_ = -1;
    toTitle();
}

void Rally::setStage(int s) {
    if (s == stage_ && track_.N) return;
    stage_ = s;
    track_ = buildTrack(s);
    tilesUsed_ = loadStage(*vdp_, art_, s);
}

void Rally::loadRecords() {
    std::istringstream f(sys_->loadBlob("records.txt"));  // a file on desktop, localStorage on the web
    int s;
    float lap, race;
    while (f >> s >> lap >> race)
        if (s >= 0 && s < NUM_STAGES) {
            recLap_[s] = lap;
            recRace_[s] = race;
        }
}

void Rally::saveRecords() {
    if (sys_->headless) return;
    std::ostringstream f;
    for (int s = 0; s < NUM_STAGES; s++) f << s << ' ' << recLap_[s] << ' ' << recRace_[s] << '\n';
    sys_->saveBlob("records.txt", f.str());
}

void Rally::toTitle() {
    versus_.stop();
    if (type_ == GameType::Versus) type_ = GameType::Championship;
    mode_ = Mode::Title;
    t_ = 0;
    startRank_ = 16;
    static int attract = 0;
    startStage(attract++ % NUM_STAGES, true);
    mode_ = Mode::Title;
    dist_ += SEG * 30;
    for (auto& r : rivals_) r.speed = MAX * r.top * 0.8f;
    speed_ = MAX * 0.7f;
    sfx_->engine(0, 0, false);
    champ_.clear();
    startRank_ = 16;
}

void Rally::startStage(int stage, bool withRivals) {
    setStage(stage);
    setCarPalette(*vdp_, PAL_PLAYER, carId_);
    setRivalPalettes(*vdp_);  // a versus race borrows one for the opponent's livery
    const StageDef& def = stageDef(stage);
    withRivals_ = withRivals;
    dist_ = track_.length - 3 * SEG;
    x_ = withRivals ? (startRank_ % 2 ? -0.4f : 0.4f) : 0;
    latV_ = steer_ = speed_ = yaw_ = throttle_ = 0;
    gear_ = 1;
    rpm_ = 0.2f;
    timer_ = def.startTime;
    raceTime_ = lapTime_ = bestLap_ = 0;
    minTimer_ = 99;
    lap_ = extends_ = 0;
    crashCool_ = 0;
    shake_ = 0;
    parts_.clear();
    msg_.clear();
    msgT_ = 0;
    noteShow_ = 0;
    newRecord_ = false;
    drifting_ = offroad_ = inWater_ = false;
    boosts_ = turbo_ ? 3 : 0;
    boostT_ = 0;
    rivals_.clear();
    if (withRivals) {
        int skill = 0;
        for (int slot = 1; slot <= 16; slot++) {
            if (slot == startRank_) continue;
            Rival r;
            r.dist = dist_ + (startRank_ - slot) * 2.0f * SEG;
            r.x = r.lane = slot % 2 ? -0.4f : 0.4f;
            r.top = 0.935f - skill * 0.016f;  // front of the grid is quickest
            r.pal = skill % NUM_RIVAL_PALS;
            r.name = NAMES[skill];
            rivals_.push_back(r);
            skill++;
        }
    }
    rank_ = withRivals ? startRank_ : 1;
}

void Rally::beginCountdown() {
    mode_ = Mode::Countdown;
    t_ = 0;
    dim_ = false;
}

void Rally::say(std::vector<std::string> lines, int frames, int pal) {
    msg_ = std::move(lines);
    msgT_ = frames;
    msgPal_ = pal;
}

void Rally::tuneRadio() {
    int st = radio_->next();
    sfx_->menuMove();
    if (radio_->voiceFor(st) >= 0) voice_->say(radio_->voiceFor(st), true);
}

// Station card: frequency and name, then the song, over a dark backing.
void Rally::drawRadio(int row) {
    if (radio_->cardFrames() <= 0) return;
    const std::string a = radio_->stationLine(), b = radio_->songLine();
    hud(20 - int(a.size()) / 2, row, a, PAL_YELLOW);
    if (!b.empty()) hud(20 - int(b.size()) / 2, row + 1, b, PAL_HUD);
    for (int i = -1; i <= 1; i++) spr(art_.panelWide, HALF + i * 100.0f, (row + 2) * 8.0f + 3, 25, 0, false, 0, 224, true);
}

// ================================================================ frame

void Rally::frame(gs::System& sys) {
    frameNo_++;
    t_++;
    gs::Pad& pad = sys.pad;
    if (pad.pressed(gs::BTN_Z) && mode_ != Mode::Menu && mode_ != Mode::Profile) tuneRadio();  // E is a letter in name entry
    // Secret code: type s3ga then Enter on the title screen or menu.
    const std::string& typed = sys.typed;
    if (typed.size() >= 5 && typed.compare(typed.size() - 5, 5, "s3ga\n") == 0) {
        sys.typed.clear();
        if (mode_ == Mode::Title || mode_ == Mode::Menu) {
            turbo_ = true;
            mode_ = Mode::Secret;
            t_ = 0;
            voice_->say(V_SECRET, true);
            sfx_->fanfare();
        }
    }
    const bool confirm = pad.pressed(gs::BTN_START) || pad.pressed(gs::BTN_C);
    const bool back = pad.pressed(gs::BTN_MODE) || pad.pressed(gs::BTN_B);
    dim_ = false;

    switch (mode_) {
        case Mode::Secret:
            drive(autopilot(), true);
            if (t_ > 60 && (confirm || back)) {
                mode_ = Mode::Title;
                t_ = 31;
            }
            break;
        case Mode::Title:
            drive(autopilot(), true);
            if (t_ > 30 && (pad.pressed(gs::BTN_START) || pad.pressed(gs::BTN_C))) {
                sfx_->menuSelect();
                if (!profile_.valid()) {
                    startProfile(false);  // first time: who are you?
                } else {
                    mode_ = Mode::Menu;
                    menuSel_ = 0;
                    t_ = 0;
                }
            }
            if (t_ > 60 * 45) toTitle();
            break;
        case Mode::Menu:
            dim_ = true;
            drive(autopilot(), true);
            if (pad.pressed(gs::BTN_UP)) { menuSel_ = (menuSel_ + 6) % 7; sfx_->menuMove(); }
            if (pad.pressed(gs::BTN_DOWN)) { menuSel_ = (menuSel_ + 1) % 7; sfx_->menuMove(); }
            if (back) { mode_ = Mode::Title; t_ = 31; }
            else if (confirm && t_ > 5) {
                sfx_->menuSelect();
                switch (menuSel_) {
                    case 0: type_ = GameType::Championship; mode_ = Mode::CarSelect; t_ = 0; break;
                    case 1: type_ = GameType::Practice; mode_ = Mode::StageSelect; t_ = 0; break;
                    case 2: type_ = GameType::TimeAttack; mode_ = Mode::StageSelect; t_ = 0; break;
                    case 3:
                        if (!Versus::available()) {
                            toast_ = "LAN PLAY NEEDS THE DESKTOP VERSION";
                            toastT_ = 180;
                        } else {
                            type_ = GameType::Versus;
                            mode_ = Mode::CarSelect;
                            t_ = 0;
                        }
                        break;
                    case 4: startProfile(true); break;
                    case 5: mode_ = Mode::Controls; ctlSel_ = 0; t_ = 0; break;
                    default: tuneRadio(); break;
                }
            }
            break;
        case Mode::Lobby:
            dim_ = true;
            drive(autopilot(), true);
            updateLobby(confirm, back);
            break;
        case Mode::Controls:
            dim_ = true;
            drive(autopilot(), true);
            updateControls(confirm, back);
            break;
        case Mode::Profile:
            dim_ = true;
            drive(autopilot(), true);
            updateProfile(pad.pressed(gs::BTN_START), pad.pressed(gs::BTN_MODE));  // letters are for typing here
            break;
        case Mode::StageSelect:
            dim_ = true;
            drive(autopilot(), true);
            if (pad.pressed(gs::BTN_LEFT) || pad.pressed(gs::BTN_RIGHT)) {
                int s = (stage_ + (pad.pressed(gs::BTN_LEFT) ? NUM_STAGES - 1 : 1)) % NUM_STAGES;
                startStage(s, true);
                dist_ += SEG * 30;
                speed_ = MAX * 0.7f;
                sfx_->menuMove();
            }
            if (back) { mode_ = Mode::Menu; t_ = 0; }
            else if (confirm && t_ > 5) { mode_ = Mode::CarSelect; t_ = 0; sfx_->menuSelect(); }
            break;
        case Mode::CarSelect:
            dim_ = true;
            drive(autopilot(), true);
            if (pad.pressed(gs::BTN_LEFT) || pad.pressed(gs::BTN_RIGHT)) { carId_ = (carId_ + 1) % NUM_CARS; sfx_->menuMove(); }
            if (pad.pressed(gs::BTN_UP) || pad.pressed(gs::BTN_DOWN)) { manual_ = !manual_; sfx_->menuMove(); }
            if (back) { mode_ = type_ == GameType::Championship || type_ == GameType::Versus ? Mode::Menu : Mode::StageSelect; t_ = 0; }
            else if (confirm && t_ > 5 && type_ == GameType::Versus) {
                sfx_->menuSelect();
                mode_ = Mode::Lobby;
                lobbyStep_ = lobbySel_ = 0;
                t_ = 0;
            } else if (confirm && t_ > 5) {
                sfx_->menuSelect();
                startRank_ = 16;
                champ_.clear();
                startStage(type_ == GameType::Championship ? 0 : stage_, type_ != GameType::TimeAttack);
                mode_ = Mode::Intro;
                t_ = 0;
            }
            break;
        case Mode::Intro:
            if (t_ > 150 || (t_ > 30 && confirm)) beginCountdown();
            break;
        case Mode::Countdown: {
            Input in = readPad();
            throttle_ = in.throttle;
            rpm_ += ((0.2f + in.throttle * 0.75f) - rpm_) * 0.15f;
            if (t_ == 1) { sfx_->beep(false); voice_->say(V_THREE, true); }
            if (t_ == 61) { sfx_->beep(false); voice_->say(V_TWO, true); }
            if (t_ == 121) { sfx_->beep(false); voice_->say(V_ONE, true); }
            if (t_ == 181) {
                sfx_->beep(true);
                voice_->say(V_GO, true);
                mode_ = Mode::Race;
                t_ = 0;
                say({"GO!"}, 50);
            }
            break;
        }
        case Mode::Race:
            if (pad.pressed(gs::BTN_START) && type_ != GameType::Versus) {  // no pausing a network race
                mode_ = Mode::Pause;
                sys.apu.setMaster(0.25f);
                break;
            }
            if (timed()) timer_ -= DT;
            raceTime_ += DT;
            lapTime_ += DT;
            minTimer_ = std::min(minTimer_, timer_);
            if (timed() && timer_ <= 5 && std::ceil(timer_) != std::ceil(timer_ + DT)) sfx_->beep(false);
            if (turbo_ && pad.pressed(gs::BTN_TURBO) && boosts_ > 0 && boostT_ == 0) {
                boosts_--;
                boostT_ = 150;
                shake_ = 6;
                sfx_->turbo();
                sys.rumble(0.3f, 1.0f, 500);
                voice_->say(V_TURBO, true);
                say({"TURBO!"}, 50, PAL_RED);
            }
            if (boostT_ > 0) boostT_--;
            drive(readPad(), false);
            if (timed() && timer_ <= 0 && mode_ == Mode::Race) gameOver();
            break;
        case Mode::Pause:
            if (pad.pressed(gs::BTN_START)) {
                mode_ = Mode::Race;
                sys.apu.setMaster(0.8f);
            } else if (pad.pressed(gs::BTN_MODE)) {
                sys.apu.setMaster(0.8f);
                toTitle();
            }
            break;
        case Mode::Over:
            drive(Input{0, 0, 0.4f}, false);
            if (t_ > 330 || (t_ > 90 && confirm)) toTitle();
            break;
        case Mode::Finish:
            drive(autopilot(), false);
            if (t_ > 210) {
                mode_ = Mode::Result;
                t_ = 0;
            }
            break;
        case Mode::Result:
            dim_ = true;
            drive(autopilot(), false);
            if (t_ > 60 && confirm) afterResult();
            break;
        case Mode::Ending:
            dim_ = true;
            drive(autopilot(), true);
            if (t_ % 20 == 0) {
                for (int i = 0; i < 24; i++) {
                    float a = i * 0.2618f, sp = 1.5f + (std::rand() % 100) / 60.0f;
                    parts_.push_back({float(40 + std::rand() % 240), float(30 + std::rand() % 70), std::cos(a) * sp, std::sin(a) * sp, 3, 0, 0, 50, 3});
                }
            }
            if ((t_ > 120 && confirm) || t_ > 60 * 20) toTitle();
            break;
    }

    updateVersus();
    if (mode_ != Mode::Pause) {
        updateRivals();
        updateParticles();
    }
    padFeedback();
    if (toastT_ > 0) toastT_--;
    updateSound();
    radio_->duck(voice_->speaking());  // turn the radio down while the co-driver talks
    radio_->tick();
    sfx_->tick();
    voice_->tick();
    if (msgT_ > 0 && --msgT_ == 0) msg_.clear();
    if (noteShow_ > 0) noteShow_--;
    render();
}

void Rally::finishStage() {
    mode_ = Mode::Finish;
    t_ = 0;
    if (withRivals_) {  // recount now: a pass on the line this frame must count
        rank_ = 1;
        for (const Rival& r : rivals_)
            if (r.dist > dist_) rank_++;
    }
    const bool win = rank_ == 1 && withRivals_;
    say({win ? "YOU WIN!" : "FINISH!"}, 200);
    voice_->say(win ? V_YOU_WIN : V_FINISH, true);
    sfx_->fanfare();
    newRecord_ = false;
    if (bestLap_ > 0 && (recLap_[stage_] == 0 || bestLap_ < recLap_[stage_])) {
        recLap_[stage_] = bestLap_;
        newRecord_ = true;
    }
    if (recRace_[stage_] == 0 || raceTime_ < recRace_[stage_]) {
        recRace_[stage_] = raceTime_;
        newRecord_ = true;
    }
    saveRecords();
    champ_.push_back({stage_, withRivals_ ? rank_ : 1, raceTime_, bestLap_});
}

void Rally::gameOver() {
    mode_ = Mode::Over;
    t_ = 0;
    timer_ = 0;
    say({"GAME OVER"}, 330, PAL_RED);
    voice_->say(V_GAME_OVER, true);
}

void Rally::afterResult() {
    if (type_ == GameType::Versus) {
        versus_.stop();
        toTitle();
        return;
    }
    if (type_ == GameType::Championship) {
        startRank_ = rank_;
        if (stage_ < 2 || (stage_ == 2 && rank_ == 1)) {
            startStage(stage_ + 1, true);
            mode_ = Mode::Intro;
            t_ = 0;
            return;
        }
        mode_ = Mode::Ending;
        t_ = 0;
        voice_->say(rank_ == 1 ? V_CONGRATS : V_FINISH, true);
        sfx_->fanfare();
            return;
    }
    toTitle();
}

// ================================================================ driving

const Segment& Rally::segAt(float z) const { return track_.segs[size_t(mod(z, track_.length) / SEG) % track_.segs.size()]; }

Input Rally::readPad() {
    if (!sys_ || (sys_->headless && !sys_->scripted)) return autopilot();
    const gs::Pad& p = sys_->pad;
    Input in;
    in.steer = p.axisX;
    in.analog = p.axisX != 0;
    if (!in.analog) in.steer = (p.down(gs::BTN_RIGHT) ? 1.0f : 0.0f) - (p.down(gs::BTN_LEFT) ? 1.0f : 0.0f);
    in.throttle = std::max(p.down(gs::BTN_C) || p.down(gs::BTN_UP) ? 1.0f : 0.0f, p.accel);
    in.brake = std::max(p.down(gs::BTN_B) || p.down(gs::BTN_DOWN) ? 1.0f : 0.0f, p.brake);
    in.shiftUp = p.pressed(gs::BTN_Y);
    in.shiftDown = p.pressed(gs::BTN_X) || p.pressed(gs::BTN_A);
    return in;
}

Input Rally::autopilot() {
    const Segment& here = segAt(dist_);
    float worst = 0, lineCurve = 0;
    for (int k = 2; k <= 16; k += 2) {
        float c = segAt(dist_ + k * SEG).curve;
        if (std::fabs(c) > std::fabs(worst)) worst = c;
        if (k == 8) lineCurve = c;
    }
    float target = clampf(lineCurve * 0.1f, -0.5f, 0.5f);
    for (const Rival& r : rivals_) {
        float dz = mod(r.dist - dist_, track_.length);
        if (dz < 5 * SEG && std::fabs(r.x - target) < 0.6f) target = r.x > 0 ? r.x - 0.7f : r.x + 0.7f;
    }
    const float pct = speed_ / MAX;
    Input in;
    in.steer = clampf((target - x_) * 2.5f + pct * here.curve * 0.34f, -1, 1);
    float safe = std::fabs(worst) > 0.5f ? std::min(1.05f, 0.98f / (std::fabs(worst) * 0.3f)) : 1.05f;
    if (here.surface == FORD) safe = 0.6f;
    in.throttle = pct < safe ? 1.0f : 0.0f;
    in.brake = pct > safe + 0.1f ? 1.0f : 0.0f;
    return in;
}

void Rally::drive(const Input& in, bool attract) {
    const Segment& seg = segAt(dist_);
    const CarSpec& car = carSpec(carId_);
    const SurfPhys& sp = SURF[seg.surface];
    const float pct = speed_ / MAX;

    steer_ += (in.steer - steer_) * std::min(1.0f, DT * (in.analog ? 20.0f : 8.0f));
    throttle_ = in.throttle;
    offroad_ = std::fabs(x_) > 1.12f;
    const bool waterSide = (x_ > 1.2f && seg.right == 1) || (x_ < -1.2f && seg.left == 1);
    inWater_ = seg.surface == FORD || waterSide;

    drifting_ = std::fabs(steer_) > 0.65f && pct > 0.5f && std::fabs(seg.curve) > 1.2f &&
                (steer_ > 0) == (seg.curve > 0) && !offroad_;

    // Lateral: steering asks for a slip velocity; the tyres deliver it at a rate set by grip.
    float grip = sp.grip * car.grip * (offroad_ ? 0.55f : 1.0f);
    float target = steer_ * 2.0f * pct;
    latV_ += (target - latV_) * std::min(1.0f, DT * 6.5f * grip);
    const float cent = drifting_ ? (seg.surface == TARMAC ? 0.27f : 0.24f) : 0.32f;
    x_ += latV_ * DT - DT * 2 * pct * pct * seg.curve * cent;

    // The car's visual yaw: steering, plus the tail stepping out in a slide.
    float yawTarget = steer_ * 1.3f + (drifting_ ? (seg.curve > 0 ? 2.2f : -2.2f) : 0) + (target - latV_) * 0.8f;
    yaw_ += (clampf(yawTarget, -4, 4) - yaw_) * 0.18f;

    // Gearbox.
    if (manual_ && !attract && mode_ == Mode::Race) {
        if (in.shiftUp && gear_ < 5) { gear_++; sfx_->shift(); }
        if (in.shiftDown && gear_ > 1) { gear_--; sfx_->shift(); }
    } else {
        float r = pct / GEAR_TOP[gear_];
        if (r > 0.94f && gear_ < 5) { gear_++; if (!attract) sfx_->shift(); }
        else if (gear_ > 1 && pct < GEAR_TOP[gear_ - 1] * 0.62f) gear_--;
    }
    const float gearLow = GEAR_TOP[gear_ - 1] * 0.5f;
    const float rpmT = clampf((pct - gearLow) / (GEAR_TOP[gear_] - gearLow), 0, 1.05f);
    rpm_ += (0.22f + 0.78f * rpmT - rpm_) * 0.3f;
    // Torque curve: strong through the mid-range, gone at the limiter.
    float torque = rpmT < 0.25f ? 0.55f + rpmT * 1.8f : rpmT < 0.95f ? 1.0f : std::max(0.0f, 1 - (rpmT - 0.95f) * 20);
    if (manual_ && gear_ == 1 && pct < 0.05f) torque = 1;

    const bool boosting = boostT_ > 0 && mode_ == Mode::Race;
    const float top = MAX * car.top * sp.top * (boosting ? 1.4f : 1.0f);
    if (in.brake > 0) speed_ -= MAX * 1.1f * DT * in.brake;
    else if (in.throttle > 0) speed_ += (MAX / 4.0f) * car.accel * DT * (1 - 0.45f * pct) * torque * in.throttle;
    else speed_ -= MAX * 0.18f * DT;
    if (speed_ > top) speed_ -= (speed_ - top) * 2 * DT;
    speed_ -= speed_ * sp.drag * DT;
    if (drifting_) speed_ -= MAX * 0.06f * DT;
    if (offroad_ && speed_ > MAX * 0.45f) speed_ -= MAX * 0.9f * DT;
    if (inWater_ && speed_ > MAX * 0.4f) speed_ -= MAX * 1.1f * DT;
    if (boosting && speed_ < top) speed_ += MAX * 0.8f * DT;  // the turbo shove

    // Scenery collisions.
    if (crashCool_ > 0) crashCool_--;
    for (const Segment* s : {&seg, &segAt(dist_ + SEG)}) {
        for (const Obj& o : s->objs) {
            const ObjInfo& info = OBJ[o.type];
            if (info.w <= 0 || std::fabs(x_ - o.off) >= 0.32f + info.w) continue;
            if (info.solid && crashCool_ == 0 && !attract) {
                bool hard = speed_ > MAX * 0.3f;
                bool soft = o.type == O_TIRES || o.type == O_BALE;
                speed_ = std::min(speed_, MAX * (soft ? 0.3f : 0.08f));
                x_ = o.off - (o.off > 0 ? 1 : -1) * (0.34f + info.w);
                latV_ = 0;
                crashCool_ = 30;
                shake_ = hard ? 14.0f : 5.0f;
                sfx_->crash(hard && !soft);
                if (hard && !soft) sys_->rumble(0.9f, 0.9f, 350);
                else sys_->rumble(0.5f, 0.6f, 180);
                for (int i = 0; i < 10; i++)
                    parts_.push_back({HALF + (std::rand() % 80 - 40), 200, (std::rand() % 100 - 50) / 25.0f, -(std::rand() % 100) / 40.0f, 10, 0.6f, 0, 30, 1});
            } else if (!info.solid) {
                speed_ -= MAX * 0.5f * DT;
            }
        }
    }

    // Rival contact.
    for (Rival& r : rivals_) {
        float dz = mod(r.dist - dist_, track_.length);
        if (dz > track_.length / 2) dz -= track_.length;
        if (std::fabs(dz) < 400 && std::fabs(r.x - x_) < 0.58f) {
            // A remote car is steered by its own player: only our car reacts to the contact.
            if (dz > 0 && speed_ > r.speed) {
                speed_ = r.speed * 0.85f;
                if (!r.remote) r.speed = std::min(MAX, r.speed + MAX * 0.05f);
            } else if (dz <= 0 && r.speed > speed_) {
                if (!r.remote) r.speed = speed_ * 0.9f;
                speed_ += MAX * 0.03f;
            }
            float push = (x_ >= r.x ? 1.0f : -1.0f) * 0.04f;
            x_ += push;
            if (!r.remote) r.x -= push;
            if (crashCool_ == 0 && !attract) {
                sfx_->bump();
                sys_->rumble(0.4f, 0.4f, 120);
                shake_ = 4;
                crashCool_ = 12;
            }
        }
    }

    speed_ = clampf(speed_, 0, MAX * 1.5f);
    x_ = clampf(x_, -3.2f, 3.2f);
    const float prev = dist_;
    dist_ += speed_ * DT;
    checkCrossings(prev, dist_, attract);
    // Keep distances small so float precision never degrades on long sessions.
    if (dist_ > 8 * track_.length) {
        const float shift = std::floor(dist_ / track_.length - 2) * track_.length;
        dist_ -= shift;
        for (Rival& r : rivals_) r.dist -= shift;
    }

    const float c = seg.curve * (speed_ / MAX);
    skyX_ -= c * 0.7f;
    nearX_ -= c * 1.6f;
    cloudX_ -= c * 0.3f + 0.06f;
    if (shake_ > 0) shake_ -= 1;

    // Particles from the wheels.
    const float p = speed_ / MAX;
    if (!attract || mode_ == Mode::Title) {
        const bool heavy = drifting_ || offroad_;
        if ((sp.dusty || offroad_) && p > 0.2f && frameNo_ % (heavy ? 2 : 4) == 0) {
            for (int side : {-1, 1}) {
                if (!heavy && (std::rand() & 1)) continue;
                parts_.push_back({HALF + side * 36 - steer_ * 8 + (std::rand() % 7 - 3), 214, side * (0.3f + (std::rand() % 60) / 100.0f) - latV_ * 3,
                                  -0.4f - (std::rand() % 50) / 100.0f, 10 + p * 8, 0.9f + (heavy ? 0.5f : 0.0f), 0, heavy ? 30 : 18, 0});
            }
        }
        if (drifting_ && frameNo_ % 3 == 0)
            parts_.push_back({HALF + (steer_ > 0 ? -44 : 44), 212, (steer_ > 0 ? -1.5f : 1.5f), -1.2f, 14, 0.3f, 0, 18, 1});
        if (inWater_ && p > 0.1f && frameNo_ % 2 == 0)
            for (int side : {-1, 1})
                parts_.push_back({HALF + side * 40, 220, side * 1.2f, -0.6f, 26 + p * 30, 1.2f, 0, 16, 2});
    }
}

void Rally::checkCrossings(float a, float b, bool attract) {
    const int N = track_.N;
    for (long k = long(std::floor(a / SEG)) + 1; k <= long(std::floor(b / SEG)); k++) {
        const int s = int(((k % N) + N) % N);
        if (attract || (mode_ != Mode::Race && mode_ != Mode::Countdown)) continue;
        // Pace notes from the co-driver.
        for (const PaceNote& n : track_.notes)
            if (n.seg == s) {
                note_ = n;
                noteShow_ = 150;
                voice_->say(n.voice);
            }
        if (mode_ != Mode::Race) continue;
        auto cp = std::find(track_.checkpoints.begin(), track_.checkpoints.end(), s);
        if (cp == track_.checkpoints.end()) continue;
        const StageDef& def = stageDef(stage_);
        if (cp == track_.checkpoints.begin()) {
            lap_++;
            if (lap_ > 1 && (bestLap_ == 0 || lapTime_ < bestLap_)) bestLap_ = lapTime_;
            lapTime_ = 0;
            if (lap_ > def.laps) {
                finishStage();
                return;
            }
            if (lap_ == 1) continue;
        }
        extends_++;
        if (timed()) timer_ += def.extend - std::min(3, extends_ / 2);
        sfx_->checkpoint();
        if (lap_ == def.laps && cp == track_.checkpoints.begin()) {
            say({"FINAL LAP", "EXTENDED PLAY!"}, 150);
            voice_->say(V_FINAL_LAP, true);
        } else {
            say({"CHECKPOINT", "EXTENDED PLAY!"}, 150);
            voice_->say(V_EXTENDED, true);
        }
    }
}

void Rally::updateRivals() {
    if (rivals_.empty()) return;
    const bool moving = mode_ != Mode::Countdown && mode_ != Mode::Intro;
    for (Rival& r : rivals_) {
        if (r.remote) continue;  // positioned by updateVersus()
        const Segment& seg = segAt(r.dist);
        float worst = 0;
        for (int k = 2; k <= 10; k += 4) worst = std::max(worst, std::fabs(segAt(r.dist + k * SEG).curve));
        float target = moving ? MAX * r.top * SURF[seg.surface].top * (1 - std::min(0.34f, worst * 0.05f)) : 0;
        if (seg.surface == FORD) target = std::min(target, MAX * 0.55f);
        r.speed += clampf(target - r.speed, -MAX * 0.6f * DT, MAX * 0.17f * DT);
        auto blocked = [&](float x, float z, float sp) {
            float dz = mod(z - r.dist, track_.length);
            return dz > 0 && dz < 4 * SEG && sp < r.speed && std::fabs(x - r.lane) < 0.55f;
        };
        bool b = blocked(x_, dist_, speed_);
        for (const Rival& o : rivals_)
            if (&o != &r && blocked(o.x, o.dist, o.speed)) b = true;
        if (b) r.lane = r.lane > 0 ? -0.45f : 0.45f;
        float want = clampf(r.lane + seg.curve * 0.05f, -0.75f, 0.75f);
        r.x += (want - r.x) * DT * 1.5f;
        r.dist += r.speed * DT;
    }
    if (mode_ == Mode::Race || mode_ == Mode::Countdown) {
        int ahead = 0;
        for (const Rival& r : rivals_)
            if (r.dist > dist_) ahead++;
        rank_ = ahead + 1;
    }
}

void Rally::updateParticles() {
    for (Particle& p : parts_) {
        p.x += p.vx;
        p.y += p.vy;
        p.size += p.grow;
        if (p.kind == 3) p.vy += 0.03f;
        else p.vy *= 0.97f;
        p.life++;
    }
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.life >= p.max; }), parts_.end());
    if (parts_.size() > 60) parts_.erase(parts_.begin(), parts_.end() - 60);
    if (stageDef(stage_).weather == 1)
        for (Flake& f : flakes_) {
            f.y += 0.6f + f.s * 0.8f + speed_ / MAX * 1.5f;
            f.x += (f.x - HALF) / HALF * speed_ / MAX * 2.5f * f.s - steer_ * speed_ / MAX * 1.5f;
            if (f.y > 224 || f.x < -4 || f.x > 324) {
                f.x = float(std::rand() % 320);
                f.y = -4 - float(std::rand() % 20);
            }
        }
}

void Rally::updateSound() {
    const bool racing = mode_ == Mode::Countdown || mode_ == Mode::Race || mode_ == Mode::Finish || mode_ == Mode::Over;
    if (!racing) {
        sfx_->engine(0, 0, false);
        sfx_->rivalEngine(0, 0);
        sfx_->road(0, 0, 0);
        return;
    }
    const float p = speed_ / MAX;
    sfx_->engine(rpm_, throttle_, true);
    sfx_->road(p * (offroad_ ? 1.6f : SURF[segAt(dist_).surface].dusty ? 0.9f : 0.4f), drifting_ ? 0.5f + p * 0.5f : 0,
               inWater_ ? p : 0);
    // The nearest rival's engine, with a touch of Doppler.
    float best = 1e9f, freq = 0, vol = 0;
    for (const Rival& r : rivals_) {
        float dz = mod(r.dist - dist_, track_.length);
        if (dz > track_.length / 2) dz -= track_.length;
        if (std::fabs(dz) < best) {
            best = std::fabs(dz);
            float rel = (r.speed - speed_) / MAX * (dz > 0 ? -1 : 1);
            freq = (40 + r.speed / MAX * 120) * (1 + rel * 0.25f);
            vol = 0.07f * std::max(0.0f, 1 - std::fabs(dz) / (6 * SEG));
        }
    }
    sfx_->rivalEngine(freq, vol);
}

// ================================================================ rendering

int Rally::fogFor(float dz) const {
    const StageDef& d = stageDef(stage_);
    float s = dz / SEG;
    return int(clampf((s - d.fogNear) / (d.fogFar - d.fogNear) * 16, 0, 16));
}

void Rally::project(Proj& p, float wy, float wz, float camX, float camY, float camZ) const {
    p.z = wz - camZ;
    p.s = DEPTH / p.z;
    p.x = HALF + p.s * -camX * HALF;
    p.y = HORIZON - p.s * (wy - camY) * VS;
    p.w = p.s * ROADW * HALF;
}

void Rally::spr(const gs::Mipped& m, float cx, float bottom, float h, int pal, bool flip, int fog, int clipY, bool shadow) {
    if (h < 1 || fog >= 16) return;
    float w = h * m.w / m.h;
    gs::Sprite s;
    s.x = int16_t(std::lround(cx - w / 2));
    s.y = int16_t(std::lround(bottom - h));
    s.w = int16_t(std::max(1L, std::lround(w)));
    s.h = int16_t(std::max(1L, std::lround(h)));
    s.img = m.pick(h);
    s.pal = uint8_t(pal);
    s.fog = uint8_t(fog);
    s.hflip = flip;
    s.shadow = shadow;
    s.clipY = int16_t(clipY);
    vdp_->sprite(s);
}

void Rally::render() {
    gs::VDP& v = *vdp_;
    const StageDef& def = stageDef(stage_);
    const auto& segs = track_.segs;
    const int N = track_.N;
    const float position = mod(dist_ - PLAYER_Z, track_.length);
    const Segment& base = segs[size_t(position / SEG) % N];
    const float basePct = mod(position, SEG) / SEG;
    const Segment& pseg = segAt(dist_);
    const float ppct = mod(dist_, SEG) / SEG;
    const float camY = pseg.y1 + (pseg.y2 - pseg.y1) * ppct + CAM_H;
    const float camX = x_ * ROADW;
    const float shake = shake_ > 0 ? (float(std::rand() % 100) / 100.0f - 0.5f) * shake_ : 0;

    for (auto& r : v.road) r.on = false;
    float x = 0, dx = -base.curve * basePct;
    int maxy = gs::SCREEN_H;
    for (int n = 0; n < DRAW; n++) {
        Segment& seg = const_cast<Segment&>(segs[size_t(base.i + n) % N]);
        const float camZ = position - (seg.i < base.i ? track_.length : 0);
        project(seg.p1, seg.y1, seg.z1, camX - x, camY, camZ);
        project(seg.p2, seg.y2, seg.z2, camX - x - dx, camY, camZ);
        x += dx;
        dx += seg.curve;
        seg.clip = float(maxy);
        seg.vis = -1;
        const Proj &p1 = seg.p1, &p2 = seg.p2;
        if (p1.z <= DEPTH) continue;
        seg.vis = int(frameNo_ & 0x7fffffff);
        if (p2.y >= maxy || p2.y >= p1.y) continue;
        const int y0 = std::max(0, int(std::ceil(p2.y)));
        const int y1 = std::min(maxy, int(std::ceil(p1.y)));
        const float inv = 1.0f / (p1.y - p2.y);
        const uint8_t pal = seg.surface == TARMAC ? PAL_TARMAC : (seg.surface == SNOW || seg.surface == FORD) ? PAL_ALT : PAL_ROAD;
        const uint8_t style = seg.surface == TARMAC ? 1 : seg.surface == FORD ? 2 : seg.surface == SNOW ? 3 : 0;
        for (int y = y0; y < y1; y++) {
            const float t = (p1.y - y - 0.5f) * inv;
            gs::RoadLine& r = v.road[y];
            r.on = true;
            r.cx = p1.x + (p2.x - p1.x) * t + shake;
            r.hw = p1.w + (p2.w - p1.w) * t;
            r.v = seg.z1 + SEG * t;
            r.pal = pal;
            r.band = seg.band;
            r.style = style;
            r.left = seg.left;
            r.right = seg.right;
            lineZ_[y] = p1.z + (p2.z - p1.z) * t;
        }
        if (y0 < maxy) maxy = y0;
    }
    for (int y = maxy + 1; y < gs::SCREEN_H; y++)
        if (!v.road[y].on) {
            v.road[y] = v.road[y - 1];
            lineZ_[y] = lineZ_[y - 1];
        }
    horizon_ = maxy;

    // Sky, fog and the parallax backdrop.
    const int dimFog = dim_ ? 9 : 0;
    const int bvs = BACKDROP_BASE - maxy;
    for (int y = 0; y < gs::SCREEN_H; y++) {
        if (y < maxy) {
            v.lineBackdrop[y] = lerpColor(def.skyTop, def.skyHorizon, float(y) / (HORIZON + 24));
            int near = std::max(0, 14 - (maxy - y));  // thicker haze right at the horizon
            v.lineFog[y] = uint8_t(std::max({def.backdropFog, std::min(near, 12), dimFog}));
        } else {
            v.lineFog[y] = uint8_t(std::max(fogFor(lineZ_[y]), dimFog));
        }
        v.B.vscroll[y] = int16_t(bvs);
        v.A.vscroll[y] = int16_t(bvs);
        v.B.hscroll[y] = int16_t(std::lround((y + bvs < CLOUD_ROWS ? cloudX_ : skyX_) + shake));
        v.A.hscroll[y] = int16_t(std::lround(nearX_ + shake));
    }
    // The secret screen hides the backdrop planes so the rainbow bars fill the sky.
    v.A.enabled = v.B.enabled = mode_ != Mode::Secret;
    if (mode_ == Mode::Secret) {
        // Demo-scene raster tricks: rainbow sky bars and a road that wobbles like jelly.
        for (int y = 0; y < gs::SCREEN_H; y++) {
            const float h = std::fmod(y * 2.5f + t_ * 3.0f, 360.0f) / 60.0f;
            const int i = int(h);
            const float f = h - i;
            const int q = int(15 * (1 - f)), u = int(15 * f);
            const int rgb[6][3] = {{15, u, 0}, {q, 15, 0}, {0, 15, u}, {0, q, 15}, {u, 0, 15}, {15, 0, q}};
            if (y < maxy) v.lineBackdrop[y] = gs::rgb4(rgb[i % 6][0], rgb[i % 6][1], rgb[i % 6][2]);
            v.lineFog[y] = 0;
            if (v.road[y].on) v.road[y].cx += std::sin(y * 0.09f + t_ * 0.12f) * (y - maxy) * 0.25f;
        }
    }
    if (dim_) v.setFogColor(gs::rgb4(0, 0, 1));
    else v.setFogColor(def.fog);

    v.clearSprites();
    v.HUD.clear();
    drawHud();
    drawMenus();
    drawWorld(base.i, basePct, position);
}

void Rally::drawWorld(int baseIndex, float basePct, float position) {
    const auto& segs = track_.segs;
    const int N = track_.N;
    const int vis = int(frameNo_ & 0x7fffffff);
    const int dimFog = dim_ ? 9 : 0;
    items_.clear();
    for (int n = 1; n < SPRITE_DRAW; n++) {
        const Segment& seg = segs[size_t(baseIndex + n) % N];
        if (seg.vis != vis) continue;
        const float d = (n - basePct) * SEG;
        for (const Obj& o : seg.objs) items_.push_back({d, &seg, &o, nullptr});
    }
    for (const Rival& r : rivals_) {
        float dz = mod(r.dist - position, track_.length);
        if (dz < SEG * 0.5f || dz > SPRITE_DRAW * SEG) continue;
        items_.push_back({dz, nullptr, nullptr, &r});
    }
    const bool showPlayer = mode_ != Mode::CarSelect && mode_ != Mode::Secret;
    if (showPlayer) items_.push_back({PLAYER_Z, nullptr, nullptr, nullptr});
    std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.d < b.d; });

    struct Shadow { float x, y, w; int clip; };
    std::vector<Shadow> shadows;

    for (const Item& it : items_) {
        if (it.obj) {
            const Segment& seg = *it.seg;
            const Proj& p = seg.p1;
            if (p.y > seg.clip + 2) continue;
            const int fog = std::max(fogFor(p.z), dimFog);
            const ObjInfo& info = OBJ[it.obj->type];
            const gs::Mipped& m = art_.obj[it.obj->type];
            const int pal = info.scene ? PAL_SCENE : PAL_COMMON;
            const int clip = int(seg.clip);
            if (it.obj->type == O_ARCH_START || it.obj->type == O_ARCH_CP) {
                float w = p.w * 2.7f;
                spr(m, p.x, p.y, w * m.h / m.w, pal, false, fog, clip);
                continue;
            }
            const float h = info.h * p.s * VS;
            if (h < 2) continue;
            const float w = h * m.w / m.h * (HALF / VS);
            const float xs = p.x + p.w * it.obj->off;
            if (xs + w < -40 || xs - w > gs::SCREEN_W + 40) continue;
            gs::Mipped mm = m;
            mm.w = int(m.w * HALF / VS);  // world proportions
            spr(mm, xs, p.y, h, pal, it.obj->flip, fog, clip);
            if (info.shadow && fog < 12) shadows.push_back({xs + w * 0.18f, p.y + 2, w * 1.1f, clip});
        } else if (it.car) {
            const Rival& r = *it.car;
            const Segment& seg = segAt(r.dist);
            if (seg.vis != vis) continue;
            const float t = mod(r.dist, SEG) / SEG;
            const Proj &p1 = seg.p1, &p2 = seg.p2;
            const float sy = p1.y + (p2.y - p1.y) * t;
            if (sy > seg.clip + 3) continue;
            const float sx = p1.x + (p2.x - p1.x) * t;
            const float sw = p1.w + (p2.w - p1.w) * t;
            const float s = p1.s + (p2.s - p1.s) * t;
            const float z = p1.z + (p2.z - p1.z) * t;
            const int fog = std::max(fogFor(z), dimFog);
            const float w = CAR_W * s * HALF;
            const float cx = sx + sw * r.x;
            const int frame = std::min(4, int(std::fabs(seg.curve) / 1.6f));
            const float bob = (r.speed > 0 && ((frameNo_ + int(r.top * 97)) % 6) < 3) ? std::max(1.0f, w / 60) : 0;
            spr(art_.car[frame], cx, sy - bob, w * CAR_ASPECT, PAL_RIVAL + r.pal, seg.curve < 0, fog, int(seg.clip));
            if (r.remote && fog < 12 && !versus_.peerName.empty() && w > 12)  // name tag over the other player's car
                text(versus_.peerName, cx, sy - w * CAR_ASPECT - 12, std::clamp(w / 90.0f, 0.5f, 0.9f), PAL_YELLOW);
            if (SURF[seg.surface].dusty && r.speed > MAX * 0.3f && fog < 14)
                spr(art_.puff, cx + ((frameNo_ / 3) % 2 ? w * 0.3f : -w * 0.3f), sy - w * 0.05f, w * 0.55f, PAL_FX, false, fog + 2, int(seg.clip));
            shadows.push_back({cx, sy + w * 0.03f, w * 1.1f, int(seg.clip)});
        } else {
            // The player.
            const float w = CAR_W * (DEPTH / PLAYER_Z) * HALF;
            const float pct = speed_ / MAX;
            float bob = 0;
            if (offroad_ && pct > 0.05f) bob = float(std::rand() % 3);
            else if (pct > 0.05f && frameNo_ % 8 < 4) bob = 1;
            const int frame = std::min(4, int(std::fabs(yaw_) + 0.35f));
            if (boostT_ > 0) {  // exhaust flames, drawn over the car
                const float h = w * CAR_ASPECT;
                const float ex = HALF + (yaw_ < 0 ? -1 : 1) * 0.26f * w;
                const float size = 26 + float(std::rand() % 14) + std::min(boostT_, 30) * 0.4f;
                spr(art_.flame, ex, 222 - bob - 0.16f * h + size / 2, size, PAL_YELLOW, (frameNo_ & 2) != 0, 0);
            }
            spr(art_.car[frame], HALF, 222 - bob, w * CAR_ASPECT, PAL_PLAYER, yaw_ < 0, dimFog);
            shadows.push_back({HALF, 225, w * 1.12f, 224});
            for (const Particle& p : parts_) {
                const gs::Mipped& m = p.kind == 1 ? art_.spray : p.kind == 2 ? art_.splash : art_.puff;
                int fog = p.kind == 3 ? 0 : std::min(15, dimFog + p.life * 10 / std::max(1, p.max));
                if (p.kind == 3) {
                    spr(art_.flake, p.x, p.y, 3, PAL_YELLOW, false, 0);
                    continue;
                }
                spr(m, p.x, p.y + p.size / 2, p.size, PAL_FX, p.vx < 0, fog);
            }
        }
    }
    for (const Shadow& s : shadows) spr(art_.shadow, s.x, s.y + s.w * 0.07f, s.w * 0.25f, 0, false, 0, s.clip, true);
}

// ================================================================ HUD

void Rally::text(const std::string& s, float x, float y, float scale, int pal, int align) {
    const float adv = 11 * scale;
    float w = s.size() * adv;
    if (align == 0) x -= w / 2;
    else if (align > 0) x -= w;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c < 32 || c >= 128) continue;
        const gs::Mipped& g = art_.glyph[c - 32];
        spr(g, x + i * adv + g.w * scale / 2, y + g.h * scale, g.h * scale, pal, false, 0);
    }
}

void Rally::hud(int col, int row, const std::string& s, int pal) {
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c <= 32 || c >= 128) continue;
        vdp_->HUD.set(col + int(i), row, gs::entry(art_.fontTile[c - 32], pal));
    }
}

void Rally::drawMap() {
    uint8_t* px = vdp_->rom() + art_.map.off;
    const int W = art_.map.w;
    std::fill(px, px + W * art_.map.h, uint8_t(0));
    auto dot = [&](float fx, float fy, int r, int c) {
        int cx = 4 + int(fx * (W - 9)), cy = 4 + int(fy * (W - 9));
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++)
                if (x >= 0 && y >= 0 && x < W && y < W) px[y * W + x] = uint8_t(c);
    };
    for (int i = 0; i < track_.N; i += 2) dot(track_.map[i].first, track_.map[i].second, 1, 8);
    for (int i = 0; i < track_.N; i += 2) dot(track_.map[i].first, track_.map[i].second, 0, 2);
    dot(track_.map[0].first, track_.map[0].second, 1, 1);
    for (const Rival& r : rivals_) {
        auto& p = track_.map[size_t(mod(r.dist, track_.length) / SEG) % track_.N];
        dot(p.first, p.second, 1, 3);
    }
    auto& p = track_.map[size_t(mod(dist_, track_.length) / SEG) % track_.N];
    dot(p.first, p.second, 2, 4);
    gs::Mipped m;
    m.w = m.h = W;
    m.lv[0] = m.lv[1] = m.lv[2] = art_.map;
    spr(m, 44, 216, float(W), PAL_HUD, false, 0);
    spr(art_.panel, 44, 218, float(W + 4), 0, false, 0, 224, true);
}

void Rally::drawHud() {
    gs::VDP& v = *vdp_;
    const StageDef& def = stageDef(stage_);
    // Snow is drawn in front of the world.
    if (def.weather == 1 && mode_ != Mode::Menu)
        for (const Flake& f : flakes_) spr(art_.flake, f.x, f.y, 2 + f.s, PAL_FX, false, dim_ ? 9 : 0);

    if (!msg_.empty() && mode_ != Mode::Result && mode_ != Mode::Ending) {
        for (size_t i = 0; i < msg_.size(); i++) text(msg_[i], HALF, 58 + i * 22.0f, i == 0 ? 1.7f : 1.25f, msgPal_);
    }
    if (toastT_ > 0 && !toast_.empty()) {  // notices: controller connected, errors
        const std::string t = toast_.substr(0, 38);
        hud(20 - int(t.size()) / 2, 26, t, PAL_YELLOW);
        for (int i = -1; i <= 1; i++) spr(art_.panelWide, HALF + i * 100.0f, 28 * 8.0f + 3, 25, 0, false, 0, 224, true);
    }
    drawTurboFx();
    const bool racing = mode_ == Mode::Countdown || mode_ == Mode::Race || mode_ == Mode::Pause || mode_ == Mode::Over ||
                        mode_ == Mode::Finish;
    if (!racing) {
        if (mode_ == Mode::Title || mode_ == Mode::Secret || mode_ == Mode::StageSelect || mode_ == Mode::Intro) drawRadio(23);
        return;
    }
    drawRadio(12);
    if (turbo_) {  // boosts left
        hud(33, 6, "TURBO", PAL_RED);
        if (boosts_ > 0) text(std::string(size_t(boosts_), '>'), 312, 56, 1.2f, PAL_YELLOW, 1);
    }

    if (mode_ == Mode::Countdown && t_ <= 180) {
        int n = 3 - (t_ - 1) / 60;
        if (n >= 1 && n <= 3) text(std::to_string(n), HALF, 58, 4 - ((t_ - 1) % 60) / 60.0f, PAL_YELLOW);
    }
    if (mode_ == Mode::Pause) {
        text("PAUSE", HALF, 70, 2, PAL_HUD);
        hud(11, 15, "START  RESUME", PAL_HUD);
        hud(11, 17, "MODE   QUIT", PAL_HUD);
        hud(20 - int(std::strlen(S3_VERSION_STRING)) / 2, 20, S3_VERSION_STRING, PAL_HUD);
    }

    // Top row.
    if (timed()) {
        hud(18, 1, "TIME", PAL_YELLOW);
        const int secs = std::max(0, int(std::ceil(timer_)));
        const bool low = secs <= 10 && mode_ == Mode::Race;
        if (!low || frameNo_ % 30 < 20) text(std::to_string(secs), HALF, 14, 2.2f, low ? PAL_RED : PAL_YELLOW);
    } else {
        const char* label = type_ == GameType::Versus ? "HEAD TO HEAD" : "TIME ATTACK";
        hud(20 - int(std::strlen(label)) / 2, 1, label, PAL_YELLOW);
        text(fmtTime(raceTime_), HALF, 14, 1.3f, PAL_HUD);
    }
    hud(2, 1, "LAP", PAL_YELLOW);
    text(std::to_string(std::clamp(lap_, 1, def.laps)) + "/" + std::to_string(def.laps), 12, 14, 1.5f, PAL_HUD, -1);
    if (withRivals_) {
        hud(34, 1, "POS", PAL_YELLOW);
        std::string r = std::to_string(rank_);
        text(r, 302, 12, 2, PAL_HUD, 1);
        hud(36, 4, "/" + std::to_string(rivals_.size() + 1), PAL_HUD);
    }
    hud(2, 5, fmtTime(lapTime_), PAL_HUD);

    // Pace note.
    if (noteShow_ > 0 && mode_ == Mode::Race) {
        const gs::Mipped& icon = art_.pace[note_.icon];
        spr(icon, HALF, 88, 40, PAL_YELLOW, note_.dir < 0, 0);
        hud(20 - int(note_.text.size()) / 2, 11, note_.text, PAL_YELLOW);
    }

    // Speed, gear and rev counter.
    const int kmh = int(std::lround(speed_ / MAX * 200));
    text(std::to_string(kmh), 276, 188, 2, PAL_HUD, 1);
    hud(35, 27, "KM/H", PAL_YELLOW);
    text(std::to_string(gear_), 314, 188, 1.5f, PAL_YELLOW, 1);
    hud(37, 22, manual_ ? "MT" : "AT", PAL_HUD);
    const int segsLit = int(rpm_ * 14);
    for (int i = 0; i < 14; i++) {
        int kind = i >= segsLit ? 3 : i < 9 ? 0 : i < 12 ? 1 : 2;
        spr(art_.rpm[kind], 192 + i * 7.0f, 184, 8 + i * 0.7f, PAL_HUD, false, 0);
    }
    spr(art_.panelWide, 262, 228, 42, 0, false, 0, 224, true);
    drawMap();
    (void)v;
}

void Rally::drawMenus() {
    const StageDef& def = stageDef(stage_);
    switch (mode_) {
        case Mode::Title: {
            spr(art_.logo, HALF, 118, 104, PAL_LOGO, false, 0);
            if (turbo_ && frameNo_ % 40 < 30) hud(1, 27, "TURBO ENABLED", PAL_RED);
            if (frameNo_ % 60 < 40) text("PRESS START", HALF, 152, 1.5f, PAL_YELLOW);
            hud(39 - int(std::strlen(S3_VERSION_STRING)), 27, S3_VERSION_STRING, PAL_HUD);
            if (radio_->cardFrames() <= 0) {  // the station card uses this space while it shows
                hud(10, 25, "(C) 2026 MACNCRASH", PAL_HUD);
                hud(12, 26, "S3-16 SYSTEM", PAL_HUD);
            }
            break;
        }
        case Mode::Secret:
            drawSecret();
            break;
        case Mode::Lobby:
            drawLobby();
            break;
        case Mode::Controls:
            drawControls();
            break;
        case Mode::Profile:
            drawProfile();
            break;
        case Mode::Menu: {
            spr(art_.logo, HALF, 70, 56, PAL_LOGO, false, 0);
            const char* items[7] = {"CHAMPIONSHIP", "PRACTICE", "TIME ATTACK", "HEAD TO HEAD", "PROFILE", "CONTROLS",
                                    radio_->station() < 0 ? "RADIO  OFF" : "RADIO  ON"};
            for (int i = 0; i < 7; i++) text(items[i], HALF, 78 + i * 17.0f, 1.0f, i == menuSel_ ? PAL_YELLOW : PAL_HUD);
            text(">", 64, 78 + menuSel_ * 17.0f, 1.0f, PAL_YELLOW, -1);
            const std::string who = "YOUR NAME: " + profile_.name;
            const char* help[7] = {"3 STAGES AND A SECRET ONE.", "RACE ANY STAGE.", "SOLO. NO TIME LIMIT.",
                                   Versus::available() ? "RACE A FRIEND ON YOUR NETWORK." : "LAN PLAY NEEDS THE DESKTOP VERSION.",
                                   who.c_str(), "SEE AND REMAP BUTTONS.", "CHANGE STATION. TAB IN GAME."};
            hud(39 - int(std::strlen(S3_VERSION_STRING)), 27, S3_VERSION_STRING, PAL_HUD);
            hud(20 - int(std::string(help[menuSel_]).size()) / 2, 25, help[menuSel_], PAL_HUD);
            break;
        }
        case Mode::StageSelect: {
            text("SELECT STAGE", HALF, 20, 1.5f, PAL_YELLOW);
            text(std::string("< ") + def.name + " >", HALF, 80, 2, PAL_HUD);
            hud(20 - int(std::string(def.subtitle).size()) / 2, 15, def.subtitle, PAL_YELLOW);
            hud(13, 18, std::to_string(def.laps) + " LAPS", PAL_HUD);
            if (recLap_[stage_] > 0) {
                hud(9, 20, "BEST LAP  " + fmtTime(recLap_[stage_]), PAL_HUD);
                hud(9, 21, "BEST RACE " + fmtTime(recRace_[stage_]), PAL_HUD);
            }
            break;
        }
        case Mode::CarSelect: {
            const CarSpec& c = carSpec(carId_);
            setCarPalette(*vdp_, PAL_PLAYER, carId_);
            text("SELECT CAR", HALF, 16, 1.5f, PAL_YELLOW);
            const int f = int(frameNo_ / 12) % 8;
            const int frame = f < 5 ? f : 8 - f;
            spr(art_.car[frame], HALF, 140, 100, PAL_PLAYER, (frameNo_ / 96) % 2 == 1, 0);
            hud(20 - int(std::string(c.name).size()) / 2, 19, c.name, PAL_YELLOW);
            hud(20 - int(std::string(c.drive).size()) / 2, 20, c.drive, PAL_HUD);
            auto bar = [&](int row, const char* label, float val) {
                hud(8, row, label, PAL_HUD);
                std::string b;
                int n = int(std::lround((val - 0.85f) * 40));
                for (int i = 0; i < 12; i++) b += i < n ? '#' : '-';
                hud(17, row, b, PAL_YELLOW);
            };
            bar(22, "POWER", c.accel);
            bar(23, "SPEED", c.top);
            bar(24, "GRIP", c.grip);
            hud(10, 26, manual_ ? "< MANUAL (Q/W) >" : "< AUTOMATIC >", PAL_HUD);
            text("<", 40, 110, 2, PAL_YELLOW);
            text(">", 280, 110, 2, PAL_YELLOW);
            break;
        }
        case Mode::Intro: {
            text(type_ == GameType::Championship && stage_ == 3 ? "SPECIAL STAGE" : "STAGE", HALF, 40, 1.5f, PAL_YELLOW);
            text(def.name, HALF, 70, 3, PAL_HUD);
            hud(20 - int(std::string(def.subtitle).size()) / 2, 14, def.subtitle, PAL_YELLOW);
            hud(12, 17, std::to_string(def.laps) + " LAPS    START " + ordinal(withRivals_ ? startRank_ : 1), PAL_HUD);
            break;
        }
        case Mode::Result: {
            if (type_ == GameType::Versus) {
                text(rank_ == 1 ? "YOU WIN!" : "YOU LOSE", HALF, 30, 2, rank_ == 1 ? PAL_YELLOW : PAL_RED);
                const CarState& p = versus_.peer;
                std::string me = profile_.name, them = versus_.peerName.empty() ? std::string("OPPONENT") : versus_.peerName;
                me.resize(13, ' ');
                them.resize(13, ' ');
                hud(7, 11, me + fmtTime(raceTime_), PAL_YELLOW);
                hud(7, 12, them + (opponentLeft_ && !p.finished ? std::string("LEFT") : p.finished ? fmtTime(p.time) : std::string("RACING...")), PAL_HUD);
            } else {
                text(withRivals_ ? (rank_ == 1 ? "YOU WIN!" : "STAGE CLEAR") : "FINISH", HALF, 30, 2, PAL_YELLOW);
                if (withRivals_) text("POSITION " + ordinal(rank_), HALF, 70, 1.5f, PAL_HUD);
            }
            hud(9, 14, "RACE TIME " + fmtTime(raceTime_), PAL_HUD);
            hud(9, 16, "BEST LAP  " + fmtTime(bestLap_), PAL_HUD);
            if (newRecord_ && frameNo_ % 40 < 28) text("NEW RECORD!", HALF, 146, 1.3f, PAL_RED);
            if (t_ > 60 && frameNo_ % 60 < 40) hud(13, 24, "PRESS START", PAL_YELLOW);
            break;
        }
        case Mode::Ending: {
            bool champ = rank_ == 1;
            text(champ ? "CHAMPION!" : "WELL DRIVEN", HALF, 24, 2.2f, PAL_YELLOW);
            float total = 0;
            for (size_t i = 0; i < champ_.size(); i++) {
                const StageResult& r = champ_[i];
                total += r.time;
                hud(6, 9 + int(i) * 2, std::string(stageDef(r.stage).name), PAL_YELLOW);
                hud(17, 9 + int(i) * 2, ordinal(r.position), PAL_HUD);
                hud(23, 9 + int(i) * 2, fmtTime(r.time), PAL_HUD);
            }
            hud(6, 19, "TOTAL", PAL_YELLOW);
            hud(23, 19, fmtTime(total), PAL_HUD);
            if (t_ > 120 && frameNo_ % 60 < 40) hud(13, 24, "PRESS START", PAL_YELLOW);
            break;
        }
        default:
            break;
    }
}

void Rally::drawTurboFx() {
    if (boostT_ <= 0 || mode_ != Mode::Race) return;
    // Speed streaks rushing past the edges of the screen.
    for (int i = 0; i < 12; i++) {
        const float side = (i & 1) ? 1.0f : -1.0f;
        const float x = HALF + side * (95 + float(std::rand() % 70));
        const float y = 60 + float(std::rand() % 140);
        spr(art_.streak, x, y, 16 + float(std::rand() % 30), PAL_HUD, false, 5);
    }
}

// The easter egg screen: unlocked by typing s3ga + Enter on the title.
void Rally::drawSecret() {
    static const int cycle[3] = {PAL_YELLOW, PAL_RED, PAL_HUD};
    text("SECRET!", HALF, 6, 3, cycle[(t_ / 6) % 3]);
    const char* lines[] = {"S3-16 DEVELOPER MODE", "", "TURBO BOOST UNLOCKED", "PRESS SPACE IN A RACE", "3 BOOSTS PER STAGE", "",
                           "THANKS FOR PLAYING"};
    for (int i = 0; i < 7; i++) hud(20 - int(std::strlen(lines[i])) / 2, 7 + i, lines[i], i == 2 ? PAL_YELLOW : PAL_HUD);
    // A car spinning on the jelly road.
    const int f = (t_ / 5) % 10;
    const int frame = f < 5 ? f : 9 - f;
    setCarPalette(*vdp_, PAL_PLAYER, carId_);
    spr(art_.car[frame], HALF, 186, 60, PAL_PLAYER, (t_ / 50) % 2 == 1, 0);
    // Sine-wave scroller.
    static const std::string msg =
        "     GREETINGS FROM MACNCRASH ... KEEP IT SIDEWAYS ... THE BLADE ROCKS 88.1 ... NEON FM NEVER SLEEPS ... "
        "KOOL KEEPS ROLLING ... HOLD THE STEERING INTO THE BEND ... DON'T CUT! ... GAME OVER YEAH! ...     ";
    const float adv = 16;
    const float off = std::fmod(t_ * 2.0f, msg.size() * adv);
    for (size_t i = 0; i < msg.size(); i++) {
        const float x = gs::SCREEN_W - off + i * adv;
        if (x < -14 || x > gs::SCREEN_W + 2 || msg[i] == ' ') continue;
        const float y = 196 + std::sin(x * 0.035f + t_ * 0.12f) * 7;
        text(std::string(1, msg[i]), x, y, 1.2f, cycle[(i / 4) % 3], -1);
    }
    if (t_ > 60 && frameNo_ % 60 < 40) hud(14, 14, "PRESS START", PAL_YELLOW);
}

// ================================================================ head to head

// Lobby steps: 0 choose host/join, 1 host picks a stage, 2 host waits,
// 3 join browses games, 4 joining, 5 connected and about to start.
void Rally::updateLobby(bool confirm, bool back) {
    versus_.myName = profile_.name;  // who we are to the other player
    versus_.myId = profile_.id;
    const gs::Pad& pad = sys_->pad;
    auto fail = [&](const char* why) {
        versus_.stop();
        toast_ = why;
        toastT_ = 200;
        lobbyStep_ = 0;
        t_ = 0;
    };
    switch (lobbyStep_) {
        case 0:
            if (pad.pressed(gs::BTN_UP) || pad.pressed(gs::BTN_DOWN)) { lobbySel_ ^= 1; sfx_->menuMove(); }
            if (back) { mode_ = Mode::Menu; t_ = 0; }
            else if (confirm && t_ > 5) {
                sfx_->menuSelect();
                t_ = 0;
                if (lobbySel_ == 0) lobbyStep_ = 1;
                else if (versus_.search()) { lobbyStep_ = 3; lobbySel_ = 0; }
                else fail("COULD NOT OPEN THE NETWORK");
            }
            break;
        case 1:
            if (pad.pressed(gs::BTN_LEFT) || pad.pressed(gs::BTN_RIGHT)) {
                int s = (stage_ + (pad.pressed(gs::BTN_LEFT) ? NUM_STAGES - 1 : 1)) % NUM_STAGES;
                startStage(s, true);
                dist_ += SEG * 30;
                speed_ = MAX * 0.7f;
                sfx_->menuMove();
            }
            if (back) { lobbyStep_ = 0; t_ = 0; }
            else if (confirm && t_ > 5) {
                sfx_->menuSelect();
                if (versus_.host(stage_, carId_)) { lobbyStep_ = 2; t_ = 0; }
                else fail("COULD NOT OPEN THE NETWORK");
            }
            break;
        case 2:
            if (back) { versus_.stop(); lobbyStep_ = 1; t_ = 0; }
            else if (versus_.connected()) { sfx_->checkpoint(); lobbyStep_ = 5; t_ = 0; }
            break;
        case 3: {
            const int n = int(versus_.hosts.size());
            if (n) {
                if (pad.pressed(gs::BTN_UP)) { lobbySel_ = (lobbySel_ + n - 1) % n; sfx_->menuMove(); }
                if (pad.pressed(gs::BTN_DOWN)) { lobbySel_ = (lobbySel_ + 1) % n; sfx_->menuMove(); }
                lobbySel_ = std::min(lobbySel_, n - 1);
            }
            if (back) { versus_.stop(); lobbyStep_ = 0; t_ = 0; }
            else if (confirm && t_ > 5 && n) {
                sfx_->menuSelect();
                versus_.join(versus_.hosts[size_t(lobbySel_)].addr, carId_);
                lobbyStep_ = 4;
                t_ = 0;
            }
            break;
        }
        case 4:
            if (back) { versus_.stop(); lobbyStep_ = 0; t_ = 0; }
            else if (versus_.connected()) { sfx_->checkpoint(); lobbyStep_ = 5; t_ = 0; }
            else if (versus_.phase == Versus::Phase::Lost) fail("NO ANSWER FROM THAT GAME");
            break;
        case 5:
            if (versus_.phase == Versus::Phase::Lost) fail("OPPONENT LEFT");
            else if (versus_.isHost && t_ == 90) {  // give both players a moment to see who they're racing
                versus_.sendGo();
                startVersusRace();
            } else if (!versus_.isHost && versus_.takeGo()) {
                startVersusRace();
            }
            break;
    }
}

void Rally::startVersusRace() {
    type_ = GameType::Versus;
    startStage(versus_.stage, false);  // no AI field: just the two of you
    withRivals_ = true;
    x_ = versus_.isHost ? -0.4f : 0.4f;
    Rival r;
    r.remote = true;
    r.dist = dist_;
    r.x = r.lane = -x_;
    r.top = 1;
    r.pal = NUM_RIVAL_PALS - 1;
    r.name = "OPPONENT";
    rivals_.push_back(r);
    setCarPalette(*vdp_, PAL_RIVAL + r.pal, versus_.peer.car);  // their car, their livery
    rank_ = versus_.isHost ? 1 : 2;
    opponentLeft_ = false;
    beginCountdown();
}

// Every frame: pump the network, send our car, place theirs.
void Rally::updateVersus() {
    if (versus_.phase == Versus::Phase::Idle) return;
    versus_.tick();
    if (versus_.connected()) {
        CarState me;
        me.dist = dist_;
        me.x = x_;
        me.speed = speed_;
        me.yaw = yaw_;
        me.time = raceTime_;
        me.lap = lap_;
        me.car = carId_;
        me.finished = mode_ == Mode::Finish || mode_ == Mode::Result;
        versus_.sendState(me);
    }
    if (type_ != GameType::Versus || rivals_.empty() || !rivals_[0].remote) return;
    Rival& r = rivals_[0];
    const CarState& p = versus_.peer;
    if (versus_.peerSeen && versus_.phase == Versus::Phase::Ready) {
        // Extrapolate from the last update so the car keeps moving between packets.
        const int age = std::min(versus_.framesSincePeer, 30);
        r.dist = p.dist + p.speed * DT * age;
        r.x = p.x;
        r.speed = p.speed;
    }
    if (versus_.phase == Versus::Phase::Lost && !opponentLeft_) {
        opponentLeft_ = true;
        say({"OPPONENT LEFT"}, 150, PAL_RED);
    }
}

void Rally::drawLobby() {
    text("HEAD TO HEAD", HALF, 16, 1.5f, PAL_YELLOW);
    const std::string ip = gs::Link::localAddress();
    switch (lobbyStep_) {
        case 0:
            text("HOST A GAME", HALF, 84, 1.3f, lobbySel_ == 0 ? PAL_YELLOW : PAL_HUD);
            text("JOIN A GAME", HALF, 110, 1.3f, lobbySel_ == 1 ? PAL_YELLOW : PAL_HUD);
            text(">", 64, 84 + lobbySel_ * 26.0f, 1.3f, PAL_YELLOW, -1);
            hud(7, 22, "BOTH PLAYERS ON THE SAME NETWORK", PAL_HUD);
            break;
        case 1: {
            const StageDef& def = stageDef(stage_);
            hud(14, 7, "CHOOSE STAGE", PAL_HUD);
            text(std::string("< ") + def.name + " >", HALF, 80, 2, PAL_HUD);
            hud(20 - int(std::strlen(def.subtitle)) / 2, 15, def.subtitle, PAL_YELLOW);
            break;
        }
        case 2:
            hud(10, 8, "WAITING FOR A RIVAL", frameNo_ % 60 < 40 ? PAL_YELLOW : PAL_HUD);
            hud(20 - int(profile_.name.size() + 5) / 2, 5, "HOST " + profile_.name, PAL_HUD);
            hud(20 - int(std::strlen(stageDef(stage_).name)) / 2, 11, stageDef(stage_).name, PAL_HUD);
            if (!ip.empty()) hud(20 - int(ip.size() + 9) / 2, 14, "YOUR IP  " + ip, PAL_HUD);
            hud(8, 22, "ON THE OTHER MACHINE CHOOSE", PAL_HUD);
            hud(8, 23, "HEAD TO HEAD - JOIN A GAME", PAL_YELLOW);
            break;
        case 3: {
            hud(11, 6, "GAMES ON YOUR NETWORK", PAL_HUD);
            if (versus_.hosts.empty()) hud(12, 12, "SEARCHING...", frameNo_ % 60 < 40 ? PAL_YELLOW : PAL_HUD);
            for (size_t i = 0; i < versus_.hosts.size() && i < 8; i++) {
                const HostInfo& h = versus_.hosts[i];
                std::string who = h.name.empty() ? h.addr.str() : h.name;
                who.resize(13, ' ');
                std::string line = who + stageDef(h.stage).name;
                if (h.version != S3_VERSION) line += "  V" + h.version;
                hud(6, 9 + int(i) * 2, (int(i) == lobbySel_ ? "> " : "  ") + line, int(i) == lobbySel_ ? PAL_YELLOW : PAL_HUD);
            }
            break;
        }
        case 4:
            hud(13, 12, "CONNECTING...", frameNo_ % 60 < 40 ? PAL_YELLOW : PAL_HUD);
            break;
        case 5:
            text("RIVAL FOUND!", HALF, 54, 1.5f, PAL_YELLOW);
            text(profile_.name + " VS " + (versus_.peerName.empty() ? "?" : versus_.peerName), HALF, 84, 1.0f, PAL_HUD);
            hud(20 - int(std::strlen(stageDef(versus_.stage).name)) / 2, 14, stageDef(versus_.stage).name, PAL_HUD);
            hud(20 - int(std::strlen(carSpec(versus_.peer.car).name)) / 2, 16, carSpec(versus_.peer.car).name, PAL_HUD);
            break;
    }
    if (lobbyStep_ != 5) hud(8, 26, "ENTER SELECT   ESC BACK", PAL_HUD);
}

// ================================================================ profile

// Steps: 0 enter a name, 1 agree to the ID, 2 getting the ID, 3 welcome.
void Rally::startProfile(bool fromMenu) {
    mode_ = Mode::Profile;
    profFromMenu_ = fromMenu;
    profStep_ = 0;
    profSel_ = 0;
    nameEdit_ = profile_.name;
    pendingChar_ = 'A';
    sys_->typed.clear();
    t_ = 0;
}

void Rally::updateProfile(bool confirm, bool back) {
    const gs::Pad& pad = sys_->pad;
    auto leave = [&] {
        mode_ = profile_.valid() ? Mode::Menu : Mode::Title;
        t_ = mode_ == Mode::Title ? 31 : 0;
    };
    switch (profStep_) {
        case 0: {
            // Keyboard: just type. Controller: up/down picks a letter, right adds it, left deletes.
            static const std::string charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.!";
            for (char c : sys_->typed) {
                if (c == '\b') {
                    if (!nameEdit_.empty()) nameEdit_.pop_back();
                } else if (char n = nameChar(c); n && nameEdit_.size() < PROFILE_NAME_MAX
                                                    && !(n == ' ' && (nameEdit_.empty() || nameEdit_.back() == ' '))) {
                    nameEdit_ += n;
                }
            }
            sys_->typed.clear();
            size_t at = charset.find(pendingChar_);
            if (at == std::string::npos) at = 0;
            if (pad.pressed(gs::BTN_UP)) pendingChar_ = charset[(at + charset.size() - 1) % charset.size()];
            if (pad.pressed(gs::BTN_DOWN)) pendingChar_ = charset[(at + 1) % charset.size()];
            if (pad.pressed(gs::BTN_RIGHT) && nameEdit_.size() < PROFILE_NAME_MAX) nameEdit_ += pendingChar_;
            if (pad.pressed(gs::BTN_LEFT) && !nameEdit_.empty()) nameEdit_.pop_back();
            while (!nameEdit_.empty() && nameEdit_.back() == ' ' && confirm) nameEdit_.pop_back();
            if (back) {
                leave();
            } else if (confirm && t_ > 5 && !nameEdit_.empty()) {
                sfx_->menuSelect();
                if (profile_.valid()) {  // renaming: the ID stays the same
                    profile_.name = nameEdit_;
                    sys_->saveBlob("profile.txt", profile_.serialize());
                    profStep_ = 3;
                } else {
                    profStep_ = 1;
                }
                t_ = 0;
            }
            break;
        }
        case 1:
            if (pad.pressed(gs::BTN_UP)) { profSel_ = (profSel_ + 2) % 3; sfx_->menuMove(); }
            if (pad.pressed(gs::BTN_DOWN)) { profSel_ = (profSel_ + 1) % 3; sfx_->menuMove(); }
            if (back || (confirm && t_ > 5 && profSel_ == 2)) {
                profStep_ = 0;
                t_ = 0;
            } else if (confirm && t_ > 5) {
                sfx_->menuSelect();
                fetch_ = std::make_unique<IdFetcher>();
                fetch_->start(profSel_ == 0);  // 0: ask urandom.ai, 1: offline ID
                profStep_ = 2;
                t_ = 0;
            }
            break;
        case 2:
            if (fetch_ && fetch_->done()) {
                profile_.name = nameEdit_;
                profile_.id = fetch_->id();
                profile_.idSource = fetch_->source();
                profile_.created = utcNow();
                sys_->saveBlob("profile.txt", profile_.serialize());
                fetch_.reset();
                sfx_->checkpoint();
                profStep_ = 3;
                t_ = 0;
            }
            break;
        default:
            if ((confirm || back) && t_ > 20) {
                mode_ = Mode::Menu;
                menuSel_ = 0;
                t_ = 0;
            }
            break;
    }
}

void Rally::drawProfile() {
    text("PLAYER PROFILE", HALF, 12, 1.5f, PAL_YELLOW);
    switch (profStep_) {
        case 0: {
            hud(20 - 9, 7, profFromMenu_ ? "CHANGE YOUR NAME" : "ENTER YOUR NAME", PAL_HUD);
            std::string shown = nameEdit_;
            if (nameEdit_.size() < PROFILE_NAME_MAX && frameNo_ % 40 < 26) shown += sys_->ctl.connected ? pendingChar_ : '_';
            text(shown.empty() ? " " : shown, HALF, 84, 2, PAL_YELLOW);
            hud(9, 17, "UP TO 12 LETTERS OR NUMBERS", PAL_HUD);
            hud(6, 19, "NAMES DON'T HAVE TO BE UNIQUE", PAL_HUD);
            if (sys_->ctl.connected) hud(4, 22, "PAD: UP/DOWN LETTER  RIGHT ADD  LEFT DEL", PAL_HUD);
            hud(6, 24, "TYPE YOUR NAME, THEN PRESS ENTER", PAL_HUD);
            hud(15, 26, "ESC BACK", PAL_HUD);
            break;
        }
        case 1: {
            hud(20 - int(nameEdit_.size() + 6) / 2, 6, "NAME: " + nameEdit_, PAL_YELLOW);
            const char* lines[] = {"YOU GET A UNIQUE PLAYER ID. IT STAYS", "THE SAME EVEN IF YOU CHANGE YOUR NAME.",
                                   "", "THE ID COMES FROM URANDOM.AI. ONLY A", "RANDOM ID IS REQUESTED: YOUR NAME IS",
                                   "NEVER SENT. YOUR NAME AND ID ARE SAVED", "ON THIS DEVICE AND SHOWN ONLY TO",
                                   "PLAYERS YOU RACE."};
            for (int i = 0; i < 8; i++) hud(1, 8 + i, lines[i], PAL_HUD);
            const char* opts[] = {"AGREE - GET MY ID", "USE AN OFFLINE ID", "BACK"};
            for (int i = 0; i < 3; i++) {
                hud(9, 19 + i * 2, i == profSel_ ? ">" : " ", PAL_YELLOW);
                hud(11, 19 + i * 2, opts[i], i == profSel_ ? PAL_YELLOW : PAL_HUD);
            }
            break;
        }
        case 2:
            hud(10, 12, profSel_ == 0 ? "CONTACTING URANDOM.AI" : "MAKING YOUR ID", frameNo_ % 40 < 26 ? PAL_YELLOW : PAL_HUD);
            break;
        default: {
            text("WELCOME", HALF, 50, 1.5f, PAL_HUD);
            text(profile_.name, HALF, 78, 2, PAL_YELLOW);
            hud(20 - 10, 15, "PLAYER ID " + profile_.shortId(), PAL_HUD);
            const std::string src = profile_.idSource == "urandom.ai" ? "ISSUED BY URANDOM.AI"
                                                                      : "OFFLINE ID (FROM THIS DEVICE)";
            hud(20 - int(src.size()) / 2, 17, src, PAL_HUD);
            if (t_ > 20 && frameNo_ % 60 < 40) hud(14, 24, "PRESS START", PAL_YELLOW);
            break;
        }
    }
}

// ================================================================ controls

namespace {
struct ActionDef {
    const char* label;
    gs::Button button;
    const char* keys;
};
const ActionDef ACTIONS[] = {
    {"ACCELERATE", gs::BTN_C, "C  UP"},       {"BRAKE", gs::BTN_B, "X  DOWN"},     {"TURBO", gs::BTN_TURBO, "SPACE"},
    {"RADIO", gs::BTN_Z, "TAB  E"},           {"SHIFT UP", gs::BTN_Y, "W"},        {"SHIFT DOWN", gs::BTN_X, "Q"},
    {"START/PAUSE", gs::BTN_START, "ENTER"}, {"BACK", gs::BTN_MODE, "ESC"},
};
constexpr int NUM_ACTIONS = int(sizeof ACTIONS / sizeof ACTIONS[0]);
constexpr int CTL_ROWS = NUM_ACTIONS + 2;  // + reset + done
bool isDpad(int phys) { return phys >= 11 && phys <= 14; }
}  // namespace

void Rally::updateControls(bool confirm, bool back) {
    gs::Controller& c = sys_->ctl;
    const gs::Pad& pad = sys_->pad;
    if (rebinding_) {
        if (c.lastPressed >= 0 && !isDpad(c.lastPressed)) {
            // Bind it. If that input did something else, that action gets our old input (a swap).
            const int phys = c.lastPressed, btn = ACTIONS[ctlSel_].button;
            const int displaced = c.map[phys];
            bool gaveAway = false;
            for (int i = 0; i < gs::PHYS_COUNT; i++) {
                // Triggers stay put: they are also the analog throttle and brake.
                if (i == phys || isDpad(i) || i == gs::PHYS_LTRIGGER || i == gs::PHYS_RTRIGGER || c.map[i] != btn) continue;
                c.map[i] = (!gaveAway && displaced >= 0 && displaced != btn) ? int8_t(displaced) : int8_t(-1);
                gaveAway = true;
            }
            c.map[phys] = int8_t(btn);
            sys_->saveBlob("controls.txt", c.serialize());
            sfx_->menuSelect();
            rebinding_ = false;
        } else if ((back && !c.anyDown) || t_ > 60 * 6 || !c.connected) {
            rebinding_ = false;  // cancelled with Esc, timed out, or unplugged
        }
        return;
    }
    if (c.suppress && !c.anyDown) c.suppress = false;  // let go of the button just bound
    if (pad.pressed(gs::BTN_UP)) { ctlSel_ = (ctlSel_ + CTL_ROWS - 1) % CTL_ROWS; sfx_->menuMove(); }
    if (pad.pressed(gs::BTN_DOWN)) { ctlSel_ = (ctlSel_ + 1) % CTL_ROWS; sfx_->menuMove(); }
    if (back) { mode_ = Mode::Menu; t_ = 0; return; }
    if (!confirm || t_ < 6) return;
    sfx_->menuSelect();
    if (ctlSel_ == NUM_ACTIONS) {
        c.resetMap();
        sys_->saveBlob("controls.txt", c.serialize());
        toast_ = "CONTROLLER BUTTONS RESET";
        toastT_ = 120;
    } else if (ctlSel_ == NUM_ACTIONS + 1) {
        mode_ = Mode::Menu;
        t_ = 0;
    } else if (!c.connected) {
        toast_ = "CONNECT A CONTROLLER TO REMAP IT";
        toastT_ = 150;
    } else {
        rebinding_ = true;
        c.lastPressed = -1;
        c.suppress = true;
        t_ = 0;
    }
}

void Rally::drawControls() {
    const gs::Controller& c = sys_->ctl;
    text("CONTROLS", HALF, 8, 1.5f, PAL_YELLOW);
    std::string pad = "NO CONTROLLER - KEYBOARD ONLY";
    if (c.connected) {
        const char* kind = c.type == gs::PAD_PS5 ? "DUALSENSE (PS5)" : c.type == gs::PAD_PS4 ? "DUALSHOCK 4 (PS4)"
                           : c.type == gs::PAD_XBOX ? "XBOX CONTROLLER" : c.type == gs::PAD_SWITCH ? "SWITCH PRO" : nullptr;
        std::string name = kind ? kind : c.name;
        for (auto& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        pad = name.substr(0, 26) + " CONNECTED";
    }
    hud(20 - int(pad.size()) / 2, 4, pad, c.connected ? PAL_YELLOW : PAL_HUD);
    hud(2, 6, "ACTION", PAL_YELLOW);
    hud(15, 6, "KEYBOARD", PAL_YELLOW);
    hud(27, 6, "CONTROLLER", PAL_YELLOW);
    for (int a = 0; a < NUM_ACTIONS; a++) {
        const int row = 8 + a * 2;
        const bool sel = a == ctlSel_;
        hud(1, row, sel ? ">" : " ", PAL_YELLOW);
        hud(2, row, ACTIONS[a].label, sel ? PAL_YELLOW : PAL_HUD);
        hud(15, row, ACTIONS[a].keys, PAL_HUD);
        std::string b;
        if (sel && rebinding_) {
            b = frameNo_ % 40 < 28 ? "PRESS A BUTTON" : "";
        } else if (c.connected) {
            for (int i = 0; i < gs::PHYS_COUNT; i++)
                if (c.map[i] == ACTIONS[a].button && !isDpad(i)) b += (b.empty() ? "" : " ") + std::string(c.physName(i));
            if (b.empty()) b = "-";
        } else {
            b = "-";
        }
        hud(27, row, b.substr(0, 13), sel ? PAL_YELLOW : PAL_HUD);
    }
    const int r = 8 + NUM_ACTIONS * 2;
    hud(1, r, ctlSel_ == NUM_ACTIONS ? ">" : " ", PAL_YELLOW);
    hud(2, r, "RESET CONTROLLER BUTTONS", ctlSel_ == NUM_ACTIONS ? PAL_YELLOW : PAL_HUD);
    hud(1, r + 1, ctlSel_ == NUM_ACTIONS + 1 ? ">" : " ", PAL_YELLOW);
    hud(2, r + 1, "DONE", ctlSel_ == NUM_ACTIONS + 1 ? PAL_YELLOW : PAL_HUD);
    hud(39 - int(std::strlen(S3_VERSION_STRING)), 27, S3_VERSION_STRING, PAL_HUD);
}

// Controller notices, rumble on rough ground and the light bar.
void Rally::padFeedback() {
    gs::Controller& c = sys_->ctl;
    if (c.events != padEvents_) {
        padEvents_ = c.events;
        const char* kind = c.type == gs::PAD_PS5 ? "DUALSENSE (PS5)" : c.type == gs::PAD_PS4 ? "DUALSHOCK 4"
                           : c.type == gs::PAD_XBOX ? "XBOX CONTROLLER" : c.type == gs::PAD_SWITCH ? "SWITCH PRO" : "CONTROLLER";
        toast_ = c.connected ? std::string(kind) + " CONNECTED" : "CONTROLLER DISCONNECTED";
        toastT_ = 180;
        ledColor_ = -1;
    }
    if (!c.connected) return;
    const bool racing = mode_ == Mode::Race;
    if (racing && (offroad_ || inWater_) && speed_ > MAX * 0.2f && frameNo_ % 6 == 0) sys_->rumble(0.25f, 0.05f, 110);
    // Light bar: the car's body colour, red while the turbo burns.
    const uint16_t body = carSpec(carId_).livery[3];
    const int want = boostT_ > 0 ? 0xf00 : body;
    if (want != ledColor_) {
        ledColor_ = want;
        sys_->setLight(((want >> 8) & 15) * 17, ((want >> 4) & 15) * 17, (want & 15) * 17);
    }
}

bool Rally::testHost(int stage, uint16_t port) {
    carId_ = 0;
    versus_.myName = profile_.name;
    versus_.myId = profile_.id;
    if (!versus_.host(stage, carId_, port)) return false;
    type_ = GameType::Versus;
    mode_ = Mode::Lobby;
    lobbyStep_ = 2;
    t_ = 0;
    return true;
}

bool Rally::testJoin(const std::string& ip, uint16_t port, int car) {
    versus_.myName = profile_.name;
    versus_.myId = profile_.id;
    if (ip == "discover") {  // find the host through its LAN broadcast, like the lobby does
        carId_ = car;
        type_ = GameType::Versus;
        mode_ = Mode::Lobby;
        lobbyStep_ = 3;
        lobbySel_ = 0;
        t_ = 0;
        return versus_.search();
    }
    gs::NetAddr a;
    if (!gs::NetAddr::parse(ip, port, &a)) return false;
    carId_ = car;
    type_ = GameType::Versus;
    versus_.join(a, carId_);
    mode_ = Mode::Lobby;
    lobbyStep_ = 4;
    t_ = 0;
    return versus_.phase == Versus::Phase::Joining;
}

Rally::VersusReport Rally::versusReport() const {
    return {mode_ == Mode::Finish || mode_ == Mode::Result, versus_.peerSeen, versus_.peer.finished, versus_.peerName, versus_.peerId,
            rank_, raceTime_, versus_.peer.time};
}

// ================================================================ headless

Rally::SimReport Rally::simulateStage(int stage, std::vector<std::string>* shots, const std::string& shotDir) {
    type_ = GameType::Championship;
    startRank_ = 16;
    carId_ = 0;
    manual_ = false;
    startStage(stage, true);
    beginCountdown();
    SimReport rep{stage, false, 16, 0, 0, 99, 0};
    int frames = 0;
    while ((mode_ == Mode::Countdown || mode_ == Mode::Race) && frames < 60 * 600) {
        frame(*sys_);
        frames++;
        if (shots && (frames == 200 || frames == 1500 || frames == 3000)) {
            sys_->render();
            std::string p = shotDir + "/stage" + std::to_string(stage) + "-" + std::to_string(frames) + ".png";
            if (sys_->saveScreenshot(p)) shots->push_back(p);
        }
    }
    rep.finished = mode_ == Mode::Finish;
    rep.position = rank_;
    rep.raceTime = raceTime_;
    rep.bestLap = bestLap_;
    rep.minTimer = minTimer_;
    rep.timeLeft = timer_;
    return rep;
}

}  // namespace rally
