// (3) RALLY - flow: menus, rallies, service, results.

#include "game.h"
#include "version.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace rc {

namespace {
constexpr float DT = 1.0f / 60.0f;
constexpr float HALF = gs::SCREEN_W / 2.0f;

// The field. Fictional crews; their home rallies make them quicker there.
struct CrewDef {
    const char* name;
    const char* nat;
    int home;  // venue index or -1
};
const CrewDef CREWS[15] = {
    {"K.HAKALA", "FIN", 0}, {"M.LINDQVIST", "SWE", 1}, {"J.ROUSSEL", "FRA", 2}, {"T.ARAI", "JPN", -1},
    {"C.MORENO", "ESP", 4}, {"P.NYGAARD", "NOR", 1}, {"A.BIANCHI", "ITA", 2}, {"R.WALSH", "AUS", 3},
    {"S.GEORGIOU", "CYP", 4}, {"D.MCRORY", "GBR", -1}, {"E.TAMM", "EST", 0}, {"L.VERMEULEN", "BEL", -1},
    {"H.KRAUSE", "GER", -1}, {"G.FERREYRA", "ARG", 3}, {"O.SANDVIK", "NOR", 1},
};
const int POINTS[10] = {25, 18, 15, 12, 10, 8, 6, 4, 2, 1};

}  // namespace

std::string fmtTime(float t);  // drive.cpp

RallyChamp::RallyChamp() = default;
RallyChamp::~RallyChamp() { versus_.stop(); }

void RallyChamp::init(gs::System& sys) {
    sys_ = &sys;
    vdp_ = &sys.vdp;
    buildArt(*vdp_, art_);
    radio_ = std::make_unique<rally::Radio>(sys.apu);
    if (!sys.headless) radio_->loadUserMusic(sys.dataPath("music/"), sys.dataPath("music-cache/"));
    sfx_ = std::make_unique<rally::Sfx>(sys.apu);
    voice_ = std::make_unique<Voice>(sys.apu);
    if (!sys.headless || sys.scripted) voice_->loadAsync(sys.dataPath("codriver/"));
    profile_ = rally::Profile::parse(sys.loadBlob("profile.txt"));
    score_ = std::make_unique<gs::ScoreClient>(sys, "rally");
    std::memcpy(versus_.magic, "GSC1", 5);  // our own sessions, apart from S3 RUN's
    loadRecords();
    setupCrews();
    flakes_.resize(90);
    for (auto& f : flakes_) f = {float(std::rand() % 320), float(std::rand() % 224), 1 + (std::rand() % 3) * 0.5f};
    toTitle();
}

float RallyChamp::romUsedMB() const { return vdp_ ? vdp_->romUsed() / 1048576.0f : 0; }

void RallyChamp::testProfile(const std::string& name) {
    profile_.name = name;
    profile_.id = rally::localUuid();
    profile_.idSource = "local";
}

void RallyChamp::setupCrews() {
    crews_.clear();
    for (int i = 0; i < 15; i++) {
        Crew c;
        c.name = CREWS[i].name;
        c.nat = CREWS[i].nat;
        c.pace = 1.0f + i * 0.0075f;
        for (int v = 0; v < NUM_VENUES; v++) c.home[v] = CREWS[i].home == v ? 0.985f : 1.0f;
        c.livery = i % NUM_LIVERIES;
        crews_.push_back(c);
    }
}

void RallyChamp::loadRecords() {
    std::istringstream f(sys_->loadBlob("rc-records.txt"));
    int s;
    float t;
    while (f >> s >> t)
        if (s >= 0 && s < NUM_STAGES) best_[s] = t;
    for (int k = 0; k < NUM_STAGES; k++) {
        std::istringstream g(sys_->loadBlob("rc-ghost-" + std::to_string(k) + ".txt"));
        float v;
        ghost_[k].clear();
        while (g >> v) ghost_[k].push_back(v);
        if (ghost_[k].size() % 3) ghost_[k].clear();
    }
}

void RallyChamp::saveRecords() {
    if (sys_->headless) return;
    std::ostringstream f;
    for (int s = 0; s < NUM_STAGES; s++)
        if (best_[s] > 0) f << s << ' ' << best_[s] << '\n';
    sys_->saveBlob("rc-records.txt", f.str());
}

void RallyChamp::say(std::vector<std::string> lines, int frames, int pal) {
    msg_ = std::move(lines);
    msgT_ = frames;
    msgPal_ = pal;
}

void RallyChamp::tuneRadio() {
    radio_->next();
    sfx_->menuMove();
}

// ================================================================ flow

void RallyChamp::toTitle() {
    versus_.stop();
    mode_ = Mode::Title;
    t_ = 0;
    attract_ = true;
    static int attract = 0;
    const int st = (attract++ * 4) % NUM_STAGES;  // a different venue each time round
    loadStage(st);
    // Start the demo run part way in, already at speed.
    car_.s += 120;
    car_.u = std::min(25.0f, botV_[size_t(car_.segIndex(course_))] * 0.9f);
    started_ = true;
    game_ = Game::Championship;
}

void RallyChamp::startRally(int v) {
    venue_ = v;
    stageNo_ = 0;
    myTotal_ = 0;
    superRally_ = false;
    for (float& t : myTime_) t = 0;
    for (float& p : myPenalty_) p = 0;
    for (Crew& c : crews_) {
        c.out = false;
        c.total = 0;
        for (float& t : c.time) t = 0;
    }
    // First stage: seeded order, fastest first; we go eighth.
    startOrder_.clear();
    for (int i = 0; i < 15; i++) startOrder_.push_back(i);
    startOrder_.insert(startOrder_.begin() + 7, -1);
    car_.damage = Damage{};
    mode_ = Mode::RallyIntro;
    t_ = 0;
    attract_ = false;
    loadStage(v * STAGES_PER_VENUE);
}

void RallyChamp::loadStage(int st) {
    stage_ = st;
    course_ = buildCourse(st);
    venue_ = course_.venue;
    stageNo_ = st % STAGES_PER_VENUE;
    const int tod = (venue_ == 1 && stageNo_ == 2) ? 2 : (venue_ == 3 && stageNo_ == 2) ? 1 : 0;
    if (venue_ != loadedVenue_ || tod != loadedTod_) {
        tilesUsed_ = loadVenue(*vdp_, art_, venue_, tod);
        loadedVenue_ = venue_;
        loadedTod_ = tod;
    }
    setCarPalette(*vdp_, PAL_PLAYER, carId_);
    const Damage keep = car_.damage;
    car_.reset(course_, course_.startSeg, carId_);
    if (!attract_ && game_ != Game::TimeAttack && game_ != Game::Online) car_.damage = keep;  // the rally goes on in the same car
    profile_v = speedProfile(course_, 1.0f, 7.0f, 8.0f, 55.0f);
    botV_ = speedProfile(course_, 0.66f, 6.0f, 5.5f, 50.0f);
    profileT_.assign(size_t(course_.N), 0.0f);
    const float segM = SEG / U;
    for (int i = course_.startSeg + 1; i < course_.N; i++)
        profileT_[size_t(i)] = profileT_[size_t(i - 1)] + segM / std::max(profile_v[size_t(i - 1)], 1.0f);
    stageTime_ = 0;
    started_ = finished_ = false;
    splitsDone_ = 0;
    nextNote_ = 0;
    dirt_ = 0;
    wiperT_ = 0;
    camY_ = car_.y + 2;
    camPsi_ = camPitch_ = shake_ = 0;
    jumps_ = crashes_ = hardLandings_ = 0;
    airTotal_ = topSpeed_ = 0;
    parts_.clear();
    clouds_.clear();
    others_.clear();
    msg_.clear();
    msgT_ = 0;
    splitShow_ = 0;
    carBehindWarned_ = 0;
    ghostRec_.clear();
    newRecord_ = false;
    voice_->clear();
    if (!attract_) {
        if (game_ == Game::Championship || game_ == Game::Rally) {
            crewTimes(st);
            planStageField();
        }
    }
}

void RallyChamp::beginStart() {
    mode_ = Mode::Start;
    if (game_ != Game::Online) startGo_ = 480;
    t_ = 0;
    sfx_->engine(0.15f, 0, true);
}

void RallyChamp::retire() {
    rec_.clear();
    // Out of the stage. In a rally you can rejoin next stage with a penalty (Super Rally).
    finished_ = true;
    myPenalty_[stage_] += 300;
    myTime_[stage_] = std::max(stageTime_, course_.idealTime * 1.6f);
    superRally_ = true;
    voice_->say(P_WE_ARE_OUT, true);
    say({"RETIRED", "SUPER RALLY: +5:00"}, 240, PAL_RED);
    mode_ = Mode::Finish;
    t_ = 0;
}

void RallyChamp::finishStage() {
    finished_ = true;
    const float total = stageTime_ + car_.penalty;
    if (rec_.active()) rec_.finish(total);
    myTime_[stage_] = total;
    mode_ = Mode::Finish;
    t_ = 0;
    sfx_->fanfare();
    // A personal best on this stage can go on the online board (if there is one, and the player hasn't said never).
    const bool best = best_[stage_] == 0 || total < best_[stage_];
    offerUpload_ = best && rec_.done() && score_ && score_->enabled() && score_->upload() != gs::ScoreClient::Upload::Never &&
                   (score_->registered() || !score_->declined());
    if (score_ && score_->registered()) score_->play(true, total);
    if (best && game_ != Game::TimeAttack) {  // time attack keeps its own record (and ghost) below
        best_[stage_] = total;
        saveRecords();
    }
    if (game_ == Game::TimeAttack) {
        newRecord_ = best_[stage_] == 0 || total < best_[stage_];
        if (newRecord_) {
            best_[stage_] = total;
            ghost_[stage_] = ghostRec_;
            saveRecords();
            if (!sys_->headless) {
                std::ostringstream g;
                for (float v : ghostRec_) g << v << ' ';
                sys_->saveBlob("rc-ghost-" + std::to_string(stage_) + ".txt", g.str());
            }
        }
        voice_->say(newRecord_ ? P_WELL_DONE : P_GOOD_STAGE);
        say({fmtTime(total), newRecord_ ? "NEW RECORD!" : "BEST " + fmtTime(best_[stage_])}, 230);
        return;
    }
    // Where did that put us?
    int rank = 1;
    for (const Crew& c : crews_)
        if (!c.out && c.time[stage_] > 0 && c.time[stage_] < total) rank++;
    voice_->say(rank <= 3 ? P_WELL_DONE : rank <= 8 ? P_GOOD_STAGE : P_OK_WE_LOST_TIME);
    if (game_ != Game::Online) say({fmtTime(total), "STAGE " + std::to_string(rank) + (rank == 1 ? "ST" : rank == 2 ? "ND" : rank == 3 ? "RD" : "TH")}, 230);
}

void RallyChamp::afterResult() {
    if (game_ == Game::TimeAttack) {
        mode_ = Mode::Pick;
        t_ = 0;
        return;
    }
    if (game_ == Game::Online) {
        versus_.stop();
        toTitle();
        return;
    }
    // Totals so far.
    myTotal_ = 0;
    for (int k = 0; k <= stageNo_; k++) myTotal_ += myTime_[venue_ * 3 + k] + myPenalty_[venue_ * 3 + k];
    for (Crew& c : crews_) {
        c.total = 0;
        for (int k = 0; k <= stageNo_; k++) c.total += c.time[venue_ * 3 + k];
    }
    // The next stage runs in rally order: the leader first, ten seconds apart.
    {
        std::vector<std::pair<float, int>> order = {{myTotal_, -1}};
        for (int i = 0; i < 15; i++)
            if (!crews_[size_t(i)].out) order.push_back({crews_[size_t(i)].total, i});
        std::sort(order.begin(), order.end());
        startOrder_.clear();
        for (auto& o : order) startOrder_.push_back(o.second);
    }
    if (stageNo_ < STAGES_PER_VENUE - 1) {
        // Service before the next stage.
        mode_ = Mode::Service;
        t_ = 0;
        serviceSel_ = 0;
        serviceMinutes_ = 15;
        for (bool& r : repair_) r = false;
        if (superRally_) {  // rejoining: the car is rebuilt
            car_.damage = Damage{};
            superRally_ = false;
        }
        return;
    }
    mode_ = Mode::Standings;
    t_ = 0;
    // Rally result and championship points.
    std::vector<std::pair<float, int>> order;
    order.push_back({myTotal_, -1});
    for (int i = 0; i < 15; i++) order.push_back({crews_[size_t(i)].out ? 1e9f : crews_[size_t(i)].total, i});
    std::sort(order.begin(), order.end());
    for (size_t p = 0; p < order.size() && p < 10; p++) {
        if (order[p].second < 0) myPoints_ += POINTS[p];
        else crews_[size_t(order[p].second)].points += POINTS[p];
    }
    for (size_t p = 0; p < order.size(); p++)
        if (order[p].second < 0) rallyRank_ = int(p) + 1;
    sfx_->fanfare();
}

void RallyChamp::afterStandings() {
    if (game_ == Game::Championship && venue_ < NUM_VENUES - 1) {
        startRally(venue_ + 1);
        return;
    }
    if (game_ == Game::Championship) {
        mode_ = Mode::Podium;
        t_ = 0;
        sfx_->fanfare();
        return;
    }
    toTitle();
}

// Service: fix what you can in the time you have. Over time costs ten seconds a minute.

void RallyChamp::updateService(bool confirm, bool back) {
    const gs::Pad& pad = sys_->pad;
    if (pad.pressed(gs::BTN_UP)) { serviceSel_ = (serviceSel_ + 4) % 5; sfx_->menuMove(); }
    if (pad.pressed(gs::BTN_DOWN)) { serviceSel_ = (serviceSel_ + 1) % 5; sfx_->menuMove(); }
    (void)back;
    if (!confirm || t_ < 10) return;
    sfx_->menuSelect();
    if (serviceSel_ < 4) {
        repair_[serviceSel_] = !repair_[serviceSel_];
        return;
    }
    int used = 0;
    for (int i = 0; i < 4; i++)
        if (repair_[i]) used += REPAIR_MINUTES[i];
    Damage& d = car_.damage;
    if (repair_[0]) d.engine = 0;
    if (repair_[1]) d.suspension = 0;
    if (repair_[2]) { d.tyres = 0; d.puncture = 0; }
    if (repair_[3]) d.body = 0;
    const int late = std::max(0, used - serviceMinutes_);
    if (late > 0) myPenalty_[stage_] += late * 10.0f;
    loadStage(stage_ + 1);
    mode_ = Mode::StageIntro;
    t_ = 0;
}

// ================================================================ frame

void RallyChamp::frame(gs::System& sys) {
    frameNo_++;
    t_++;
    gs::Pad& pad = sys.pad;
    if (pad.pressed(gs::BTN_Z) && mode_ != Mode::Profile && mode_ != Mode::Lobby) tuneRadio();
    const bool confirm = pad.pressed(gs::BTN_START) || pad.pressed(gs::BTN_C);
    const bool back = pad.pressed(gs::BTN_MODE) || pad.pressed(gs::BTN_B);
    updateMenus(confirm, back);
    updateOnline();
    if (score_) score_->poll();
    if (!paused_) {
        updateOthers();
        updateParticles();
    }
    padFeedback();
    if (toastT_ > 0) toastT_--;
    updateSound();
    radio_->duck(voice_->speaking());
    radio_->tick();
    sfx_->tick();
    voice_->tick();
    if (msgT_ > 0 && --msgT_ == 0) msg_.clear();
    if (splitShow_ > 0) splitShow_--;
    render();
}

void RallyChamp::updateMenus(bool confirm, bool back) {
    const gs::Pad& pad = sys_->pad;
    switch (mode_) {
        case Mode::Title:
            drive(botInput(), true);
            if (t_ > 30 && pad.pressed(gs::BTN_MODE) && sys_->hasHome()) {
                sys_->eject();
                break;
            }
            if (car_.s * U / SEG > course_.finishSeg - 20 || t_ > 60 * 40) toTitle();
            if (t_ > 30 && confirm) {
                sfx_->menuSelect();
                if (!profile_.valid()) startProfile(false);
                else { mode_ = Mode::Menu; menuSel_ = 0; t_ = 0; }
            }
            break;
        case Mode::Menu: {
            drive(botInput(), true);
            const int n = 7;
            if (pad.pressed(gs::BTN_UP)) { menuSel_ = (menuSel_ + n - 1) % n; sfx_->menuMove(); }
            if (pad.pressed(gs::BTN_DOWN)) { menuSel_ = (menuSel_ + 1) % n; sfx_->menuMove(); }
            if (pad.pressed(gs::BTN_LEFT) || pad.pressed(gs::BTN_RIGHT)) {  // difficulty on the first line
                if (menuSel_ == 0 || menuSel_ == 1) {
                    difficulty_ = (difficulty_ + (pad.pressed(gs::BTN_LEFT) ? 2 : 1)) % 3;
                    sfx_->menuMove();
                }
            }
            if (back) { mode_ = Mode::Title; t_ = 31; break; }
            if (!confirm || t_ < 6) break;
            sfx_->menuSelect();
            switch (menuSel_) {
                case 0: game_ = Game::Championship; mode_ = Mode::CarSelect; t_ = 0; break;
                case 1: game_ = Game::Rally; mode_ = Mode::Pick; pickSel_ = 0; t_ = 0; break;
                case 2: game_ = Game::TimeAttack; mode_ = Mode::Pick; pickSel_ = 0; t_ = 0; break;
                case 3:
                    if (!rally::Versus::available()) { toast_ = "ONLINE NEEDS THE DESKTOP VERSION"; toastT_ = 180; }
                    else { game_ = Game::Online; mode_ = Mode::CarSelect; t_ = 0; }
                    break;
                case 4: startProfile(true); break;
                case 5: mode_ = Mode::Controls; ctlSel_ = 0; t_ = 0; break;
                default: tuneRadio(); break;
            }
            break;
        }
        case Mode::Pick: {
            drive(botInput(), true);
            const int n = game_ == Game::Rally ? NUM_VENUES : NUM_STAGES;
            if (pad.pressed(gs::BTN_UP) || pad.pressed(gs::BTN_LEFT)) { pickSel_ = (pickSel_ + n - 1) % n; sfx_->menuMove(); }
            if (pad.pressed(gs::BTN_DOWN) || pad.pressed(gs::BTN_RIGHT)) { pickSel_ = (pickSel_ + 1) % n; sfx_->menuMove(); }
            if (back) { mode_ = Mode::Menu; t_ = 0; break; }
            if (confirm && t_ > 5) { sfx_->menuSelect(); mode_ = Mode::CarSelect; t_ = 0; }
            break;
        }
        case Mode::CarSelect:
            drive(botInput(), true);
            if (pad.pressed(gs::BTN_LEFT) || pad.pressed(gs::BTN_RIGHT)) {
                carId_ = (carId_ + (pad.pressed(gs::BTN_LEFT) ? NUM_CARS - 1 : 1)) % NUM_CARS;
                setCarPalette(*vdp_, PAL_PLAYER, carId_);
                sfx_->menuMove();
            }
            if (pad.pressed(gs::BTN_UP) || pad.pressed(gs::BTN_DOWN)) {
                // Four set-ups: automatic or manual, with or without traction help.
                const int k = (manual_ ? 1 : 0) + (assist_ ? 0 : 2);
                const int n = (k + (pad.pressed(gs::BTN_DOWN) ? 1 : 3)) % 4;
                manual_ = n & 1;
                assist_ = !(n & 2);
                sfx_->menuMove();
            }
            if (back) { mode_ = game_ == Game::Rally || game_ == Game::TimeAttack ? Mode::Pick : Mode::Menu; t_ = 0; break; }
            if (!confirm || t_ < 6) break;
            sfx_->menuSelect();
            attract_ = false;
            if (game_ == Game::Online) {
                mode_ = Mode::Lobby;
                lobbyStep_ = lobbySel_ = 0;
                t_ = 0;
            } else if (game_ == Game::TimeAttack) {
                car_.damage = Damage{};
                loadStage(pickSel_);
                mode_ = Mode::StageIntro;
                t_ = 0;
            } else {
                myPoints_ = 0;
                for (Crew& c : crews_) c.points = 0;
                startRally(game_ == Game::Rally ? pickSel_ : 0);
            }
            break;
        case Mode::Lobby:
            drive(botInput(), true);
            updateLobby(confirm, back);
            break;
        case Mode::Controls:
            drive(botInput(), true);
            updateControls(confirm, back);
            break;
        case Mode::Profile:
            drive(botInput(), true);
            updateProfile(pad.pressed(gs::BTN_START), pad.pressed(gs::BTN_MODE));
            break;
        case Mode::RallyIntro:
            if ((confirm && t_ > 30) || t_ > 60 * 12) { mode_ = Mode::StageIntro; t_ = 0; }
            break;
        case Mode::StageIntro:
            if ((confirm && t_ > 30) || t_ > 60 * 10) beginStart();
            break;
        case Mode::Start: {
            // The start clock: the co-driver reads the first notes, then five, four, three, two, one, go.
            CarInput in = readPad();
            car_.rpm += ((900 + in.throttle * 5500) - car_.rpm) * 0.15f;
            car_.throttle = in.throttle;
            if (pad.pressed(gs::BTN_A)) view_ = View((int(view_) + 1) % 3);
            if (t_ == 30) {
                // First call(s) of the stage, like a real co-driver on the line.
                std::vector<int> words;
                for (int k = 0; k < 2 && k < int(course_.notes.size()); k++)
                    for (int w : course_.notes[size_t(k)].words) words.push_back(w);
                voice_->call(words);
                nextNote_ = 2;
            }
            const int go = startGo_;
            for (int k = 5; k >= 1; k--)
                if (t_ == go - k * 60) { sfx_->beep(false); voice_->say(P_FIVE_S + (5 - k), true); }
            if (t_ == go) {
                sfx_->beep(true);
                voice_->say(P_GO, true);
                mode_ = Mode::Stage;
                started_ = true;
                noProgress_ = 0;
                progressS_ = car_.s;
                // Record the drive: a replay the score server can check (not in online races, which start mid-stage).
                if (game_ != Game::Online) rec_.begin("rally", S3_VERSION_STRING, stage_, car_, course_, manual_);
                else rec_.clear();
                // A registered player's run starts with a server ticket, and counts as a play.
                if (game_ != Game::Online && score_ && score_->registered() && score_->upload() != gs::ScoreClient::Upload::Never) {
                    score_->startRun(stage_);
                    score_->play(false);
                }
                t_ = 0;
                say({"GO!"}, 50);
            }
            break;
        }
        case Mode::Stage:
            if (paused_) {
                pauseT_++;
                if (pad.pressed(gs::BTN_MODE)) {
                    sys_->apu.setMaster(0.8f);
                    paused_ = false;
                    toTitle();
                } else if (pad.pressed(gs::BTN_START) && pauseT_ > 5) {
                    sys_->apu.setMaster(0.8f);
                    paused_ = false;
                }
                break;
            }
            if (pad.pressed(gs::BTN_START) && game_ != Game::Online) {  // no pausing a race against other players
                paused_ = true;
                pauseT_ = 0;
                sys_->apu.setMaster(0.25f);
                break;
            }
            if (pad.pressed(gs::BTN_A)) view_ = View((int(view_) + 1) % 3);
            stageTime_ += DT;
            {
                // The car gets exactly what the replay records.
                const RunInput q = quantize(readPad());
                rec_.frame(q, car_, course_, noProgress_, progressS_);
                drive(expand(q), false);
            }
            stageClock();
            callNotes();
            recordGhost();
            if (car_.state == CarState::Out && mode_ == Mode::Stage) retire();
            break;
        case Mode::Finish:
            drive(botInput(), false);
            if (t_ > 60 * 4) { mode_ = Mode::Result; t_ = 0; }
            break;
        case Mode::Result:
            if (net_ != Net::None) updateNet(confirm, back);
            else if (offerUpload_ && t_ > 40) {
                offerUpload_ = false;
                netSel_ = netT_ = 0;
                netSent_ = false;
                if (score_->registered()) net_ = score_->upload() == gs::ScoreClient::Upload::Always ? Net::Uploading : Net::AskUpload;
                else net_ = Net::AskJoin;
            } else if (confirm && t_ > 40) afterResult();
            break;
        case Mode::Service:
            updateService(confirm, back);
            break;
        case Mode::Standings:
            if (confirm && t_ > 60) afterStandings();
            break;
        case Mode::Podium:
            if ((confirm && t_ > 120) || t_ > 60 * 30) toTitle();
            break;
    }
}

// ================================================================ profile

void RallyChamp::startProfile(bool fromMenu) {
    mode_ = Mode::Profile;
    profFromMenu_ = fromMenu;
    profStep_ = 0;
    profSel_ = 0;
    nameEdit_ = profile_.name;
    pendingChar_ = 'A';
    sys_->typed.clear();
    t_ = 0;
}

void RallyChamp::updateProfile(bool confirm, bool back) {
    const gs::Pad& pad = sys_->pad;
    switch (profStep_) {
        case 0: {
            static const std::string charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.!";
            for (char c : sys_->typed) {
                if (c == '\b') {
                    if (!nameEdit_.empty()) nameEdit_.pop_back();
                } else if (char n = rally::nameChar(c); n && nameEdit_.size() < rally::PROFILE_NAME_MAX &&
                                                        !(n == ' ' && (nameEdit_.empty() || nameEdit_.back() == ' '))) {
                    nameEdit_ += n;
                }
            }
            sys_->typed.clear();
            size_t at = charset.find(pendingChar_);
            if (at == std::string::npos) at = 0;
            if (pad.pressed(gs::BTN_UP)) pendingChar_ = charset[(at + charset.size() - 1) % charset.size()];
            if (pad.pressed(gs::BTN_DOWN)) pendingChar_ = charset[(at + 1) % charset.size()];
            if (pad.pressed(gs::BTN_RIGHT) && nameEdit_.size() < rally::PROFILE_NAME_MAX) nameEdit_ += pendingChar_;
            if (pad.pressed(gs::BTN_LEFT) && !nameEdit_.empty()) nameEdit_.pop_back();
            while (!nameEdit_.empty() && nameEdit_.back() == ' ' && confirm) nameEdit_.pop_back();
            if (back && netProfile_) {  // changed their mind about joining
                netProfile_ = false;
                mode_ = Mode::Result;
                net_ = Net::None;
                t_ = 41;
            } else if (back) {
                mode_ = profile_.valid() ? Mode::Menu : Mode::Title;
                t_ = mode_ == Mode::Title ? 31 : 0;
            } else if (confirm && t_ > 5 && !nameEdit_.empty()) {
                sfx_->menuSelect();
                if (profile_.valid()) {
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
                fetch_ = std::make_unique<rally::IdFetcher>();
                fetch_->start(profSel_ == 0);
                profStep_ = 2;
                t_ = 0;
            }
            break;
        case 2:
            if (fetch_ && fetch_->done()) {
                profile_.name = nameEdit_;
                profile_.id = fetch_->id();
                profile_.idSource = fetch_->source();
                profile_.created = rally::utcNow();
                sys_->saveBlob("profile.txt", profile_.serialize());
                fetch_.reset();
                sfx_->checkpoint();
                profStep_ = 3;
                t_ = 0;
            }
            break;
        default:
            if ((confirm || back) && t_ > 20) {
                if (netProfile_) {  // opened to join the online board: carry on there
                    netProfile_ = false;
                    mode_ = Mode::Result;
                    net_ = Net::Joining;
                    netT_ = 0;
                    netSent_ = false;
                    t_ = 41;
                    break;
                }
                mode_ = Mode::Menu;
                menuSel_ = 0;
                t_ = 0;
            }
            break;
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
    {"ACCELERATE", gs::BTN_C, "C  UP"},     {"BRAKE", gs::BTN_B, "X  DOWN"},      {"HANDBRAKE", gs::BTN_TURBO, "SPACE"},
    {"SHIFT UP", gs::BTN_Y, "W"},           {"SHIFT DOWN", gs::BTN_X, "Q"},       {"CHANGE VIEW", gs::BTN_A, "V  Z"},
    {"RADIO", gs::BTN_Z, "TAB  E"},         {"START/PAUSE", gs::BTN_START, "ENTER"}, {"BACK", gs::BTN_MODE, "ESC"},
};
constexpr int NUM_ACTIONS = int(sizeof ACTIONS / sizeof ACTIONS[0]);
constexpr int CTL_ROWS = NUM_ACTIONS + 2;
bool isDpad(int phys) { return phys >= 11 && phys <= 14; }
}  // namespace

void RallyChamp::updateControls(bool confirm, bool back) {
    gs::Controller& c = sys_->ctl;
    const gs::Pad& pad = sys_->pad;
    if (rebinding_) {
        if (c.lastPressed >= 0 && !isDpad(c.lastPressed)) {
            const int phys = c.lastPressed, btn = ACTIONS[ctlSel_].button;
            const int displaced = c.map[phys];
            bool gaveAway = false;
            for (int i = 0; i < gs::PHYS_COUNT; i++) {
                if (i == phys || isDpad(i) || i == gs::PHYS_LTRIGGER || i == gs::PHYS_RTRIGGER || c.map[i] != btn) continue;
                c.map[i] = (!gaveAway && displaced >= 0 && displaced != btn) ? int8_t(displaced) : int8_t(-1);
                gaveAway = true;
            }
            c.map[phys] = int8_t(btn);
            sys_->saveBlob("controls.txt", c.serialize());
            sfx_->menuSelect();
            rebinding_ = false;
        } else if ((back && !c.anyDown) || t_ > 60 * 6 || !c.connected) {
            rebinding_ = false;
        }
        return;
    }
    if (c.suppress && !c.anyDown) c.suppress = false;
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

void RallyChamp::drawControls() {
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
}

void RallyChamp::padFeedback() {
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
    // Light bar: the car's colour, red when it's damaged.
    const uint16_t body = carSpec(carId_).livery[3];
    const int want = car_.damage.total() > 0.5f ? 0xf00 : body;
    if (want != ledColor_) {
        ledColor_ = want;
        sys_->setLight(((want >> 8) & 15) * 17, ((want >> 4) & 15) * 17, (want & 15) * 17);
    }
}

}  // namespace rc

namespace rc {

// ================================================================ online scoreboard

void RallyChamp::netUpload() {
    // The replay goes with the time: the server drives it again before it counts.
    score_->submit(stage_, myTime_[stage_], base64Encode(rec_.replay().encode()), S3_VERSION_STRING);
    netSent_ = true;
}

void RallyChamp::updateNet(bool confirm, bool back) {
    const gs::Pad& pad = sys_->pad;
    netT_++;
    auto choose = [&](int n) {
        if (pad.pressed(gs::BTN_UP)) { netSel_ = (netSel_ + n - 1) % n; sfx_->menuMove(); }
        if (pad.pressed(gs::BTN_DOWN)) { netSel_ = (netSel_ + 1) % n; sfx_->menuMove(); }
        return confirm && netT_ > 10;
    };
    auto message = [&](const std::string& m) {
        net_ = Net::Message;
        netMsg_ = m;
        netT_ = 0;
    };
    switch (net_) {
        case Net::AskJoin:
            if (back) net_ = Net::None;
            else if (choose(3)) {
                sfx_->menuSelect();
                if (netSel_ == 0) {
                    if (!profile_.valid()) {  // a name and an ID first, on the usual profile screen
                        netProfile_ = true;
                        startProfile(false);
                    } else {
                        net_ = Net::Joining;
                        netT_ = 0;
                        netSent_ = false;
                    }
                } else {
                    if (netSel_ == 2) score_->decline();
                    net_ = Net::None;
                }
            }
            break;
        case Net::Joining:
            if (!netSent_) {
                score_->registerPlayer(profile_.id, profile_.name);
                netSent_ = true;
            } else if (!score_->busy()) {
                if (score_->registered()) {
                    net_ = Net::Uploading;
                    netSent_ = false;
                } else {
                    message(score_->error.empty() ? "COULD NOT JOIN" : score_->error);
                }
            }
            break;
        case Net::AskUpload:
            if (back) net_ = Net::None;
            else if (choose(3)) {
                sfx_->menuSelect();
                if (netSel_ == 1) net_ = Net::None;
                else {
                    if (netSel_ == 2) score_->setUpload(gs::ScoreClient::Upload::Always);
                    net_ = Net::Uploading;
                    netSent_ = false;
                    netT_ = 0;
                }
            }
            break;
        case Net::Uploading:
            if (!netSent_) netUpload();
            else if (!score_->busy()) {
                if (score_->lastStatus == "offline" || score_->lastStatus == "error" || score_->lastStatus.empty()) {
                    message(score_->error.empty() ? "UPLOAD FAILED" : score_->error);
                } else {
                    score_->fetchBoard(stage_);
                    net_ = Net::Board;
                    netT_ = 0;
                }
            }
            break;
        case Net::Board:
        case Net::Message:
            if ((confirm || back) && netT_ > 30) {
                net_ = Net::None;
                t_ = 41;
            }
            break;
        case Net::None:
            break;
    }
}

}  // namespace rc

namespace rc {

void RallyChamp::testNetScreen(int which) {
    mode_ = Mode::Result;
    myTime_[stage_ < 0 ? 0 : stage_] = 122.21f;
    net_ = which == 0 ? Net::AskJoin : which == 1 ? Net::AskUpload : Net::Board;
    netSel_ = 0;
    netT_ = 45;
    if (which == 2 && score_) {
        gs::Board b;
        b.loaded = true;
        b.total = 212;
        const char* names[] = {"KANKKUNEN", "MAKINEN", "SAINZ", "AURIOL", "MCRAE", "BURNS", "GRONHOLM", "LOEB", "ROHRL", "BLOMQVIST"};
        for (int k = 0; k < 10; k++) b.top.push_back({k + 1, names[k], "", 101.4 + k * 1.7, false});
        b.you = {37, profile_.name.empty() ? "YOU" : profile_.name, "", 122.21, true};
        score_->board = b;
        score_->lastStatus = "accepted";
    }
}

}  // namespace rc
