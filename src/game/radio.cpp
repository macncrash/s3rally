// S3 RALLY radio: patches, drum kit, songs and the sequencer.
//
// Tracker notation, one token per step, bars separated by '|' for reading:
//   E2        play a note          E2:m   play a chord (M m 5 6 7) on chord patches
//   B5/D6     play B5 and bend up to D6
//   x         re-pick the last note short (palm-muted chug)
//   -         hold          .       release
// Drum tokens combine letters: K kick, S snare, P clap, H hat, O open hat,
// C crash, R ride, T high tom, L low tom.
// A part shorter than its section repeats to fill it.

#include "radio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

#include <SDL.h>

#include "stages.h"

namespace rally {

namespace {

constexpr int FIRST_CH = 2;  // FM channels 2..7 belong to the radio
constexpr int MAX_TRACKS = 6;
constexpr float PI2 = 6.2831853f;

float noteFreq(const std::string& n) {
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    if (n.size() < 2 || n[0] < 'A' || n[0] > 'G') return 0;
    int semi = base[n[0] - 'A'];
    size_t i = 1;
    if (n[i] == '#') { semi++; i++; }
    if (i >= n.size() || !std::isdigit(static_cast<unsigned char>(n[i]))) return 0;
    int oct = n[i] - '0';
    return 440.0f * std::pow(2.0f, (12 * (oct + 1) + semi - 69) / 12.0f);
}

Step parseStep(const std::string& t) {
    Step s;
    if (t == ".") s.type = Step::Off;
    else if (t == "-") s.type = Step::Hold;
    else if (t == "x") s.type = Step::Chug;
    else {
        std::string head = t, bend;
        size_t slash = t.find('/');
        if (slash != std::string::npos) {
            head = t.substr(0, slash);
            bend = t.substr(slash + 1);
        }
        size_t colon = head.find(':');
        if (colon != std::string::npos) {
            s.chord = colon + 1 < head.size() ? head[colon + 1] : 0;
            head = head.substr(0, colon);
        }
        s.f = noteFreq(head);
        s.f2 = bend.empty() ? 0 : noteFreq(bend);
        s.type = s.f > 0 ? Step::Note : Step::Off;
        if (s.f <= 0) std::fprintf(stderr, "radio: bad token '%s'\n", t.c_str());
    }
    return s;
}

std::vector<std::string> tokens(std::string s) {
    std::replace(s.begin(), s.end(), '|', ' ');  // bar lines are only for reading
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string t;
    while (ss >> t) out.push_back(t);
    return out;
}

// Chord voicings as operator frequency multiples.
bool chordMuls(char c, float m[4]) {
    switch (c) {
        case 'M': m[0] = 1; m[1] = 1.2599f; m[2] = 1.4983f; m[3] = 2; return true;
        case 'm': m[0] = 1; m[1] = 1.1892f; m[2] = 1.4983f; m[3] = 2; return true;
        case '5': m[0] = 1; m[1] = 1.4983f; m[2] = 2; m[3] = 1; return true;
        case '6': m[0] = 1; m[1] = 1.6818f; m[2] = 2; m[3] = 1; return true;
        case '7': m[0] = 1; m[1] = 1.2599f; m[2] = 1.4983f; m[3] = 1.7818f; return true;
        default: return false;
    }
}

// ------------------------------------------------------------ patches

gs::FMPatch rhythmGuitar(float detune) {
    gs::FMPatch p;
    p.alg = 7;  // four carriers: root, fifth, octave, doubled root = a power chord
    p.fb = 0.55f;
    p.op[0] = {1, 0.55f, 0.002f, 0.4f, 0.85f, 0.07f};
    p.op[1] = {1.4983f, 0.42f, 0.002f, 0.4f, 0.85f, 0.07f};
    p.op[2] = {2, 0.3f, 0.002f, 0.4f, 0.85f, 0.07f};
    p.op[3] = {1, 0.35f, 0.002f, 0.4f, 0.85f, 0.07f, 1.3f + detune};
    p.drive = 7;
    p.tone = 3200;
    p.vol = 0.075f;
    p.glide = 1;
    p.echo = 0.05f;
    return p;
}

gs::FMPatch crunchGuitar() {  // rock & roll double-stops
    gs::FMPatch p = rhythmGuitar(0);
    p.drive = 2.2f;
    p.tone = 3800;
    p.fb = 0.35f;
    for (auto& o : p.op) { o.dr = 0.25f; o.sl = 0.45f; o.rr = 0.06f; }
    p.vol = 0.085f;
    p.echo = 0.2f;
    return p;
}

gs::FMPatch leadGuitar() {
    gs::FMPatch p;
    p.alg = 0;
    p.fb = 0.6f;
    p.op[0] = {1, 0.5f, 0.003f, 0.6f, 0.9f, 0.2f};
    p.op[1] = {2, 0.35f, 0.003f, 0.6f, 0.9f, 0.2f};
    p.op[2] = {1, 0.5f, 0.003f, 0.6f, 0.9f, 0.2f};
    p.op[3] = {1, 1.0f, 0.004f, 1.2f, 0.9f, 0.18f};
    p.drive = 9;
    p.tone = 4200;
    p.vibRate = 5.8f;
    p.vibDepth = 0.012f;
    p.vibDelay = 0.18f;
    p.glide = 0.0025f;
    p.echo = 0.28f;
    p.vol = 0.07f;
    return p;
}

gs::FMPatch rockBass() {
    gs::FMPatch p;
    p.alg = 4;
    p.op[0] = {1, 0.45f, 0.002f, 0.2f, 0.4f, 0.06f};
    p.op[1] = {1, 1.0f, 0.002f, 0.5f, 0.8f, 0.06f};
    p.op[2] = {3, 0.12f, 0.001f, 0.08f, 0.0f, 0.05f};
    p.op[3] = {1, 0.5f, 0.002f, 0.4f, 0.7f, 0.06f};
    p.drive = 1.2f;
    p.tone = 1600;
    p.vol = 0.17f;
    p.glide = 1;
    return p;
}

gs::FMPatch uprightBass() {
    gs::FMPatch p = rockBass();
    p.drive = 0.4f;
    p.tone = 1100;
    for (auto& o : p.op) { o.dr = 0.3f; o.sl = 0.25f; o.rr = 0.08f; }
    p.vol = 0.2f;
    return p;
}

gs::FMPatch synthBass() {
    gs::FMPatch p;
    p.alg = 4;
    p.op[0] = {1, 0.7f, 0.001f, 0.18f, 0.15f, 0.06f};
    p.op[1] = {1, 1.0f, 0.001f, 0.4f, 0.7f, 0.06f};
    p.op[2] = {2, 0.35f, 0.001f, 0.1f, 0.0f, 0.05f};
    p.op[3] = {1, 0.5f, 0.001f, 0.4f, 0.7f, 0.06f, 0.8f};
    p.tone = 2500;
    p.vol = 0.16f;
    p.glide = 1;
    return p;
}

gs::FMPatch pad(bool stab) {
    gs::FMPatch p;
    p.alg = 7;
    p.fb = 0.35f;
    const float det[4] = {0, 0.7f, -0.6f, 1.1f};
    for (int i = 0; i < 4; i++)
        p.op[i] = {1, 0.3f, stab ? 0.005f : 0.04f, stab ? 0.25f : 0.6f, stab ? 0.5f : 0.75f, stab ? 0.2f : 0.35f, det[i]};
    p.drive = 0.6f;
    p.tone = 3000;
    p.vol = stab ? 0.075f : 0.07f;
    p.glide = 1;
    p.echo = 0.3f;
    return p;
}

gs::FMPatch arpPluck() {
    gs::FMPatch p;
    p.alg = 4;
    p.op[0] = {2, 0.55f, 0.001f, 0.09f, 0.0f, 0.1f};
    p.op[1] = {1, 1.0f, 0.001f, 0.25f, 0.0f, 0.1f};
    p.op[2] = {5, 0.15f, 0.001f, 0.06f, 0.0f, 0.1f};
    p.op[3] = {1, 0.35f, 0.001f, 0.2f, 0.0f, 0.1f};
    p.tone = 5000;
    p.vol = 0.07f;
    p.glide = 1;
    p.echo = 0.45f;
    return p;
}

gs::FMPatch synthLead() {
    gs::FMPatch p;
    p.alg = 4;
    p.fb = 0.8f;
    p.op[0] = {1, 0.6f, 0.01f, 0.5f, 0.85f, 0.2f};
    p.op[1] = {1, 1.0f, 0.01f, 0.6f, 0.85f, 0.2f};
    p.op[2] = {1, 0.4f, 0.01f, 0.5f, 0.85f, 0.2f, 2.2f};
    p.op[3] = {1, 0.6f, 0.01f, 0.6f, 0.85f, 0.2f, -1.8f};
    p.vibRate = 5.2f;
    p.vibDepth = 0.008f;
    p.vibDelay = 0.25f;
    p.glide = 0.003f;
    p.tone = 5000;
    p.echo = 0.35f;
    p.vol = 0.08f;
    return p;
}

gs::FMPatch pianoChords() {  // pounding rock & roll piano
    gs::FMPatch p;
    p.alg = 7;
    p.fb = 0.2f;
    const float det[4] = {0, 1.2f, -0.9f, 1.8f};
    for (int i = 0; i < 4; i++) p.op[i] = {1, 0.3f, 0.001f, 0.4f, 0.15f, 0.12f, det[i]};
    p.tone = 4500;
    p.vol = 0.085f;
    p.glide = 1;
    p.echo = 0.08f;
    return p;
}

gs::FMPatch sax() {
    gs::FMPatch p;
    p.alg = 0;
    p.fb = 0.4f;
    p.op[0] = {1, 0.35f, 0.03f, 0.5f, 0.8f, 0.1f};
    p.op[1] = {1, 0.5f, 0.03f, 0.5f, 0.8f, 0.1f};
    p.op[2] = {3, 0.15f, 0.03f, 0.5f, 0.8f, 0.1f};
    p.op[3] = {1, 1.0f, 0.03f, 0.8f, 0.85f, 0.1f};
    p.vibRate = 5;
    p.vibDepth = 0.01f;
    p.vibDelay = 0.2f;
    p.drive = 1.5f;
    p.tone = 3500;
    p.glide = 0.003f;
    p.echo = 0.15f;
    p.vol = 0.085f;
    return p;
}

// ------------------------------------------------------------ drum kit

gs::Sample drum(int kind) {
    gs::Sample s;
    s.rate = 44100;
    const float lens[] = {0.35f, 0.27f, 0.3f, 0.08f, 0.45f, 1.6f, 0.9f, 0.4f, 0.45f};
    const int n = int(lens[kind] * s.rate);
    s.data.resize(n);
    uint32_t seed = 0x1234567u + kind * 7919u;
    float lp = 0, hpPrev = 0, clapLp = 0;
    const float metal[6] = {263, 400, 421, 474, 587, 845};
    for (int i = 0; i < n; i++) {
        const float t = float(i) / s.rate;
        seed = seed * 1664525u + 1013904223u;
        const float noise = float(int32_t(seed)) / 2147483648.0f;
        lp += (noise - lp) * 0.25f;
        const float hp = noise - lp;  // bright noise
        float metallic = 0;
        for (float f : metal) metallic += (std::fmod(t * f * 2.7f, 1.0f) < 0.5f ? 1.0f : -1.0f);
        metallic /= 6;
        const float metalHp = metallic - hpPrev;
        hpPrev = metallic;
        float v = 0;
        switch (kind) {
            case 0: {  // kick: pitch-dropping sine with a beater click
                float f = 48 + 125 * std::exp(-t * 32);
                v = std::sin(PI2 * f * t) * std::exp(-t * 8) + noise * std::exp(-t * 320) * 0.35f;
                v = std::tanh(v * 1.6f);
                break;
            }
            case 1: {  // gated snare: body + bright noise held open then chopped
                float body = (std::sin(PI2 * 185 * t) + 0.5f * std::sin(PI2 * 330 * t)) * std::exp(-t * 22) * 0.55f;
                float gate = t < 0.21f ? 1.0f : std::max(0.0f, 1 - (t - 0.21f) / 0.03f);
                v = body + hp * (0.85f * std::exp(-t * 20) + 0.32f) * gate;
                break;
            }
            case 2: {  // clap: three quick bursts then a short tail
                clapLp += (hp - clapLp) * 0.3f;
                float env = 0;
                for (float o : {0.0f, 0.011f, 0.022f})
                    if (t >= o) env = std::max(env, std::exp(-(t - o) * 110));
                env = std::max(env, t > 0.022f ? std::exp(-(t - 0.022f) * 14) * 0.45f : 0.0f);
                v = clapLp * env * 1.6f;
                break;
            }
            case 3: v = (metalHp * 0.6f + hp * 0.5f) * std::exp(-t * 60); break;  // closed hat
            case 4: v = (metalHp * 0.6f + hp * 0.5f) * std::exp(-t * 7); break;   // open hat
            case 5: v = (hp * 0.6f + metalHp * 0.4f) * std::exp(-t * 2.8f) * 0.9f; break;  // crash
            case 6: v = (metalHp * 0.35f + std::sin(PI2 * 1650 * t) * 0.25f) * std::exp(-t * 5) + hp * 0.1f * std::exp(-t * 9); break;
            case 7: {  // high tom
                float f = 120 + 60 * std::exp(-t * 18);
                v = std::sin(PI2 * f * t) * std::exp(-t * 9) + noise * std::exp(-t * 60) * 0.1f;
                break;
            }
            default: {  // low tom
                float f = 80 + 40 * std::exp(-t * 16);
                v = std::sin(PI2 * f * t) * std::exp(-t * 8) + noise * std::exp(-t * 60) * 0.1f;
                break;
            }
        }
        s.data[i] = v;
    }
    return s;
}

// ------------------------------------------------------------ song builder

struct Builder {
    Song song;
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> sections;

    Builder(const char* title, const char* artist, float bpm, int spb) {
        song.title = title;
        song.artist = artist;
        song.bpm = bpm;
        song.stepsPerBeat = spb;
    }
    Builder& echo(float t, float fb, float wet) {
        song.echoTime = t;
        song.echoFb = fb;
        song.echoWet = wet;
        return *this;
    }
    Builder& track(const char* name, const gs::FMPatch& p, float pan) {
        song.tracks.push_back({name, p, pan, {}});
        return *this;
    }
    Builder& section(const char* name, std::vector<std::pair<std::string, std::string>> parts) {
        sections.push_back({name, std::move(parts)});
        return *this;
    }
    Song build(std::initializer_list<const char*> order) {
        for (const char* name : order) {
            auto it = std::find_if(sections.begin(), sections.end(), [&](auto& s) { return s.first == name; });
            if (it == sections.end()) {
                std::fprintf(stderr, "radio: %s: no section '%s'\n", song.title.c_str(), name);
                continue;
            }
            std::vector<std::pair<std::string, std::vector<std::string>>> parts;
            size_t len = 0;
            for (auto& [key, text] : it->second) {
                parts.push_back({key, tokens(text)});
                len = std::max(len, parts.back().second.size());
            }
            for (auto& [key, toks] : parts)
                if (toks.empty() || len % toks.size())
                    std::fprintf(stderr, "radio: %s/%s: part '%s' has %zu steps, section has %zu\n", song.title.c_str(), name,
                                 key.c_str(), toks.size(), len);
            auto partFor = [&](const std::string& track) -> const std::vector<std::string>* {
                for (auto& [key, toks] : parts) {
                    if (key == track) return &toks;
                    if (!key.empty() && key.back() == '*' && track.compare(0, key.size() - 1, key, 0, key.size() - 1) == 0)
                        return &toks;
                }
                return nullptr;
            };
            for (auto& tr : song.tracks) {
                const auto* p = partFor(tr.name);
                for (size_t i = 0; i < len; i++) {
                    if (!p || p->empty()) tr.steps.push_back(i == 0 ? Step{Step::Off} : Step{});
                    else tr.steps.push_back(parseStep((*p)[i % p->size()]));
                }
            }
            const auto* d = partFor("drums");
            for (size_t i = 0; i < len; i++) {
                std::string tok = d && !d->empty() ? (*d)[i % d->size()] : ".";
                song.drums.push_back(tok == "." ? "" : tok);
            }
            song.steps += int(len);
        }
        return song;
    }
};

// Repeat a string n times (for long sustained or repeated bars).
std::string rep(const std::string& s, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += s + " ";
    return out;
}
// A held note or chord over n steps.
std::string hold(const std::string& note, int n) { return note + " " + rep("-", n - 1); }
// Twelve-bar (or any) form from per-chord bars, e.g. form(bars, "AAAADDAAEDAE").
std::string form(const std::string& a, const std::string& d, const std::string& e, const char* seq) {
    std::string out;
    for (const char* c = seq; *c; c++) out += (*c == 'A' ? a : *c == 'D' ? d : e) + " ";
    return out;
}

// ------------------------------------------------------------ the songs
// All original compositions.

Song neonKnuckles() {  // hair metal, E minor
    Builder b("NEON KNUCKLES", "VELVET RAZOR", 146, 4);
    b.echo(0.3f, 0.25f, 0.22f)
        .track("gtrL", rhythmGuitar(-0.9f), -0.75f)
        .track("gtrR", rhythmGuitar(0.9f), 0.75f)
        .track("bass", rockBass(), 0)
        .track("lead", leadGuitar(), 0.15f)
        .track("keys", pad(true), -0.3f);
    const std::string riff =
        "E2 x x x x x G2 - A2 - x x E2 x D2 - | E2 x x x x x G2 - A2 - C3 - B2 - G2 -";
    const std::string riffBass =
        "E1 E1 E1 E1 E1 E1 G1 - A1 - A1 A1 E1 E1 D2 - | E1 E1 E1 E1 E1 E1 G1 - A1 - C2 - B1 - G1 -";
    const std::string beat = "KH . H . SH . H K KH . H . SH . H H";
    b.section("intro", {{"gtr*", riff},
                        {"drums", rep(".", 32) + beat + " S . S S T . T T L . L L S S S S"}});
    b.section("verse", {{"gtr*", riff}, {"bass", riffBass}, {"drums", rep(beat, 7) + " KH . H . SH . H K KH . SH . S S S S"}});
    b.section("chorus",
              {{"gtr*", "C3 - C3 - C3 - C3 - C3 - C3 - C3 - C3 - | D3 - D3 - D3 - D3 - D3 - D3 - D3 - D3 - |"
                        "E3 - E3 - E3 - E3 - E3 - E3 - E3 - E3 - | E3 - - - - - - - D3 - - - B2 - - -"},
               {"bass", "C2 - C2 - C2 - C2 - C2 - C2 - C2 - C2 - | D2 - D2 - D2 - D2 - D2 - D2 - D2 - D2 - |"
                        "E1 - E1 - E1 - E1 - E1 - E1 - E1 - E1 - | E1 - - - - - - - D2 - - - B1 - - -"},
               {"keys", hold("C4:M", 16) + hold("D4:M", 16) + hold("E4:m", 16) + hold("E4:m", 8) + hold("D4:M", 4) + hold("B3:M", 4)},
               {"lead", "E5 - - - D5 - E5 - G5 - - - E5 - D5 - | D5 - - - C5 - D5 - F#5 - - - A5 - - - |"
                        "B5 - - - - - - - G5 - - - E5 - - - | B5/D6 - - - - - - - B5 - A5 - G5 - E5 -"},
               {"drums", "KC . H . SH . H K KH . H . SH . H H | " + rep(beat, 2) + " KH . H . SH . H K KH . SH . S S T L"}});
    b.section("solo", {{"gtr*", riff}, {"bass", riffBass}, {"drums", beat},
                       {"lead", "B5 - - - B5/D6 - - - B5 A5 G5 E5 G5 - A5 - | B5 D6 E6 D6 B5 A5 G5 A5 B5 - - - E6/F#6 - - - |"
                                "G6 F#6 E6 D6 E6 D6 B5 A5 B5 A5 G5 E5 D5 E5 G5 A5 | B5 - - - - - - - E6/G6 - - - - - - - |"
                                "E6 D6 B5 D6 E6 D6 B5 D6 E6 D6 B5 D6 E6 G6 A6 B6 | A6/B6 - - - - - - - G6 E6 D6 B5 A5 G5 E5 D5 |"
                                "E5 G5 A5 B5 D6 E6 G6 A6 B6 - A6 - G6 - E6 - | E6/F#6 - - - - - - - - - - - . . . ."}});
    b.section("outro", {{"gtr*", hold("E2", 16) + rep(".", 16)},
                        {"bass", hold("E1", 16) + rep(".", 16)},
                        {"keys", hold("E4:m", 16) + rep(".", 16)},
                        {"lead", hold("E6/G6", 16) + rep(".", 16)},
                        {"drums", "KC " + rep(".", 31)}});
    return b.build({"intro", "verse", "chorus", "verse", "chorus", "solo", "chorus", "outro"});
}

Song redlineRomance() {  // hair metal anthem, A major
    Builder b("REDLINE ROMANCE", "GLITTER VIPER", 128, 4);
    b.echo(0.35f, 0.3f, 0.25f)
        .track("gtrL", rhythmGuitar(-0.7f), -0.7f)
        .track("gtrR", rhythmGuitar(0.7f), 0.7f)
        .track("bass", rockBass(), 0)
        .track("lead", leadGuitar(), 0.1f)
        .track("keys", pad(false), -0.2f);
    const std::string beat = "K . H . S . H . K K H . S . H .";
    const std::string chorusGtr = "A2 - A2 - A2 - A2 - A2 - A2 - A2 - A2 - | E2 - E2 - E2 - E2 - E2 - E2 - E2 - E2 - |"
                                  "F#2 - F#2 - F#2 - F#2 - F#2 - F#2 - F#2 - F#2 - | D2 - D2 - D2 - D2 - D2 - D2 - D2 - D2 -";
    const std::string chorusBass = "A1 - A1 - A1 - A1 - A1 - A1 - A1 - A1 - | E1 - E1 - E1 - E1 - E1 - E1 - E1 - E1 - |"
                                   "F#1 - F#1 - F#1 - F#1 - F#1 - F#1 - F#1 - F#1 - | D2 - D2 - D2 - D2 - D2 - D2 - D2 - D2 -";
    const std::string chorusKeys = hold("A3:M", 16) + hold("E3:M", 16) + hold("F#3:m", 16) + hold("D3:M", 16);
    const std::string chorusDrums = "KC . H . S . H . K K H . S . H . | " + rep(beat, 2) + " K . H . S . H . K K S . S S S S";
    b.section("intro", {{"gtr*", hold("A2", 16) + hold("E2", 16)},
                        {"keys", hold("A3:M", 16) + hold("E3:M", 16)},
                        {"lead", hold("E5/A5", 16) + hold("G#5", 16)},
                        {"drums", "C " + rep(".", 15) + " S . S . S S S S T T T T L L L L"}});
    b.section("verse", {{"gtr*", "A2 x x x A2 x x x A2 x x x A2 x E2 - | D2 x x x D2 x x x D2 x x x E2 - - -"},
                        {"bass", "A1 - A1 - A1 - A1 - A1 - A1 - A1 - E1 - | D2 - D2 - D2 - D2 - D2 - D2 - E2 - - -"},
                        {"keys", hold("A3:M", 16) + hold("D3:M", 8) + hold("E3:M", 8)},
                        {"lead", "C#5 - - - E5 - - - A5 - - - G#5 - E5 - | F#5 - - - E5 - - - - - - - . . . . |"
                                 "C#5 - - - E5 - - - A5 - - - B5 - C#6 - | B5 - - - - - - - A5/B5 - - - . . . ."},
                        {"drums", beat}});
    b.section("chorus", {{"gtr*", chorusGtr}, {"bass", chorusBass}, {"keys", chorusKeys}, {"drums", chorusDrums},
                         {"lead", "C#6 - - - E6 - C#6 - B5 - - - A5 - B5 - | B5 - - - G#5 - B5 - E6 - - - - - - - |"
                                  "C#6 - - - F#6 - E6 - C#6 - - - B5 - A5 - | A5 - - - F#5 - A5 - B5 - C#6 - B5/C#6 - - - |"
                                  "C#6 - - - E6 - C#6 - B5 - - - A5 - B5 - | B5 - - - G#5 - B5 - E6 - - - - - - - |"
                                  "C#6 - - - F#6 - E6 - C#6 - - - B5 - A5 - | A5 - - - - - - - . . . . . . . ."}});
    b.section("solo", {{"gtr*", chorusGtr}, {"bass", chorusBass}, {"keys", chorusKeys}, {"drums", chorusDrums},
                       {"lead", "E6 - C#6 - A5 - C#6 - E6/F#6 - - - E6 C#6 B5 A5 | B5 - - - G#5 B5 E6 G#6 B6 - - - G#6 E6 B5 G#5 |"
                                "A5 C#6 F#6 A6 C#7 - - - B6 A6 F#6 E6 C#6 B5 A5 F#5 | D6 - E6 - F#6 - A6 - B6/C#7 - - - - - - -"}});
    b.section("end", {{"gtr*", hold("A2", 32)}, {"bass", hold("A1", 32)}, {"keys", hold("A3:M", 32)},
                      {"lead", hold("C#6/E6", 32)}, {"drums", "KC " + rep(".", 31)}});
    return b.build({"intro", "verse", "chorus", "verse", "chorus", "solo", "chorus", "end"});
}

Song heartsInChrome() {  // synth pop, A minor
    Builder b("HEARTS IN CHROME", "PAPER SATELLITES", 118, 4);
    b.echo(0.381f, 0.42f, 0.33f)
        .track("bass", synthBass(), 0)
        .track("arp", arpPluck(), -0.45f)
        .track("pad", pad(false), 0.3f)
        .track("lead", synthLead(), 0.1f);
    const std::string bass = "A1 - A2 - A1 - A2 - A1 - A2 - A1 - A2 - | F1 - F2 - F1 - F2 - F1 - F2 - F1 - F2 - |"
                             "C2 - C3 - C2 - C3 - C2 - C3 - C2 - C3 - | G1 - G2 - G1 - G2 - G1 - G2 - G1 - G2 -";
    const std::string arp = rep("A4 C5 E5 C5 A5 E5 C5 E5", 2) + rep("F4 A4 C5 A4 F5 C5 A4 C5", 2) +
                            rep("C5 E5 G5 E5 C6 G5 E5 G5", 2) + rep("G4 B4 D5 B4 G5 D5 B4 D5", 2);
    const std::string pads = hold("A3:m", 16) + hold("F3:M", 16) + hold("C4:M", 16) + hold("G3:M", 16);
    const std::string beat = "K . H . SP . H . K . H K SP . H O";
    b.section("intro", {{"arp", arp}, {"pad", pads}, {"drums", rep(".", 32) + rep("H . H . H . H . H . H . H . H .", 1) + " K . . . SP . . . K . SP . SP SP SP SP"}});
    b.section("verse", {{"bass", bass}, {"arp", arp}, {"pad", pads}, {"drums", beat},
                        {"lead", "A4 - C5 - E5 - - - D5 - C5 - A4 - - - | F4 - A4 - C5 - - - A4 - G4 - F4 - - - |"
                                 "G4 - C5 - E5 - - - D5 - C5 - G4 - - - | B4 - D5 - G5 - - - F5 - D5 - B4 - - -"}});
    b.section("chorus", {{"bass", bass}, {"arp", arp}, {"pad", pads},
                         {"drums", "KC . H . SP . H . K . H K SP . H O | " + rep(beat, 6) + " K . H K SP . H . K . SP . SP SP SP SP"},
                         {"lead", "E5 - - - D5 - C5 - D5 - - - E5 - G5 - | A5 - - - - - G5 - F5 - E5 - F5 - - - |"
                                  "G5 - - - E5 - C5 - E5 - - - G5 - C6 - | B5 - - - - - - - A5 - G5 - D5 - - - |"
                                  "E5 - - - D5 - C5 - D5 - - - E5 - A5 - | C6 - - - - - A5 - F5 - A5 - C6 - D6 - |"
                                  "E6 - - - D6 - C6 - G5 - - - E5 - G5 - | D6 - - - B5 - - - G5 - - - . . . ."}});
    b.section("bridge", {{"bass", "D2 - D3 - D2 - D3 - D2 - D3 - D2 - D3 - | A1 - A2 - A1 - A2 - A1 - A2 - A1 - A2 - |"
                                  "F1 - F2 - F1 - F2 - F1 - F2 - F1 - F2 - | G1 - G2 - G1 - G2 - G1 - G2 - G1 - G2 -"},
                         {"arp", rep("D5 F5 A5 F5 D6 A5 F5 A5", 2) + rep("A4 C5 E5 C5 A5 E5 C5 E5", 2) +
                                     rep("F4 A4 C5 A4 F5 C5 A4 C5", 2) + rep("G4 B4 D5 B4 G5 D5 B4 D5", 2)},
                         {"pad", hold("D4:m", 16) + hold("A3:m", 16) + hold("F3:M", 16) + hold("G3:M", 16)},
                         {"lead", hold("A5", 16) + hold("E5", 16) + hold("F5", 16) + "G5 - - - - - - - D6 - - - B5 - - -"},
                         {"drums", "K . . . K . . . K . . . K . . P"}});
    b.section("outro", {{"arp", rep("A4 C5 E5 C5 A5 E5 C5 E5", 4)}, {"pad", hold("A3:m", 32)}, {"lead", hold("A5", 32)},
                        {"bass", hold("A1", 16) + rep(".", 16)}, {"drums", "KC " + rep(".", 31)}});
    return b.build({"intro", "verse", "verse", "chorus", "verse", "chorus", "bridge", "chorus", "outro"});
}

Song rainOnNeon() {  // dark synth pop, D minor, sequenced bass
    Builder b("RAIN ON NEON", "VAPOR KID", 124, 4);
    b.echo(0.363f, 0.45f, 0.35f)
        .track("bass", synthBass(), 0)
        .track("arp", arpPluck(), 0.45f)
        .track("pad", pad(false), -0.3f)
        .track("lead", synthLead(), 0);
    const std::string bass = rep("D2 D2 D3 D2", 4) + rep("A#1 A#1 A#2 A#1", 4) + rep("F1 F1 F2 F1", 4) + rep("C2 C2 C3 C2", 4);
    const std::string pads = hold("D4:m", 16) + hold("A#3:M", 16) + hold("F3:M", 16) + hold("C4:M", 16);
    const std::string arp = "D5 - A5 - F5 - A5 - D6 - A5 - F5 - A5 - | D5 - A#5 - F5 - A#5 - D6 - A#5 - F5 - A#5 - |"
                            "C5 - A5 - F5 - A5 - C6 - A5 - F5 - A5 - | C5 - G5 - E5 - G5 - C6 - G5 - E5 - G5 -";
    const std::string beat = "K . H . KS . H . K . H . KS . H O";
    b.section("intro", {{"bass", bass}, {"pad", pads}, {"drums", rep(".", 32) + rep("H . H . H . H . H . H . H . H .", 2)}});
    b.section("verse", {{"bass", bass}, {"pad", pads}, {"arp", arp}, {"drums", beat},
                        {"lead", "D5 - - - F5 - - - E5 - D5 - C5 - A4 - | A#4 - - - D5 - - - F5 - - - - - - - |"
                                 "A4 - - - C5 - - - F5 - E5 - C5 - A4 - | G4 - - - C5 - - - E5 - - - - - - -"}});
    b.section("chorus", {{"bass", bass}, {"pad", pads}, {"arp", arp},
                         {"drums", "KC . H . KP . H . K . H . KP . H O | " + rep("K . H . KP . H . K . H . KP . H O", 6) +
                                       " K . H . KP . H . K . KP . KP KP KP KP"},
                         {"lead", "A5 - - - - - G5 - F5 - - - E5 - D5 - | F5 - - - - - - - D5 - - - A#4 - - - |"
                                  "C5 - - - - - D5 - F5 - - - A5 - C6 - | A5 - - - G5 - - - - - - - . . . . |"
                                  "D6 - - - C6 - A5 - C6 - - - D6 - F6 - | F6 - - - E6 - D6 - - - C6 - A#5 - - - |"
                                  "A5 - - - - - C6 - F6 - - - E6 - C6 - | D6 - - - - - - - - - - - . . . ."}});
    b.section("outro", {{"pad", pads}, {"arp", arp}, {"lead", hold("D6", 32) + hold("C6", 32)}, {"bass", hold("D2", 16) + rep(".", 48)},
                        {"drums", "KC " + rep(".", 63)}});
    return b.build({"intro", "verse", "verse", "chorus", "verse", "chorus", "outro"});
}

Song gravelBoogie() {  // shuffle rock & roll, 12-bar in A (triplet grid)
    Builder b("GRAVEL BOOGIE", "JOHNNY PISTON", 172, 3);
    b.echo(0.11f, 0.12f, 0.3f)
        .track("bass", uprightBass(), 0)
        .track("gtr", crunchGuitar(), 0.5f)
        .track("keys", pianoChords(), -0.45f)
        .track("lead", sax(), 0.1f);
    const std::string walkA = "A1 - C#2 E2 - F#2 G2 - F#2 E2 - C#2";
    const std::string walkD = "D2 - F#2 A2 - B2 C3 - B2 A2 - F#2";
    const std::string walkE = "E2 - G#2 B2 - C#3 D3 - C#3 B2 - G#2";
    const std::string gA = rep("A2:5 - A2:6", 4), gD = rep("D3:5 - D3:6", 4), gE = rep("E2:5 - E2:6", 4);
    const std::string kA = rep(". . A4:7", 4), kD = rep(". . D4:7", 4), kE = rep(". . E4:7", 4);
    const std::string shuffle = "KR . R SR . R KR . R SR . R";
    const char* twelve = "AAAADDAAEDAE";
    const std::string drums12 = rep(shuffle, 11) + " S . S S . S T . T L . L";
    b.section("intro", {{"bass", rep(walkA, 4)}, {"drums", rep(". . R", 8) + shuffle + " S . S S . S T . T L . L"}});
    b.section("head", {{"bass", form(walkA, walkD, walkE, twelve)}, {"gtr", form(gA, gD, gE, twelve)},
                       {"keys", form(kA, kD, kE, twelve)}, {"drums", drums12},
                       {"lead", "A4 - C5 C#5 - E5 - - - . . . | A5 - G5 E5 - C#5 E5 - - . . . | A4 - C5 C#5 - E5 G5 - F#5 E5 - C#5 |"
                                "E5 - - - - - . . . . . . | F#5 - A5 A5 - F#5 D5 - - . . . | C6 - A5 F#5 - D5 C5 - - . . . |"
                                "E5 - C#5 A4 - C#5 E5 - A5 - - - | G5 - E5 C#5 - A4 . . . . . . | B5 - - G#5 - E5 B5 - - G#5 - - |"
                                "A5 - - F#5 - D5 A5 - - F#5 - - | E5 - C#5 A4 - C#5 E5 - G5 A5 - - | B4/C#5 - - - - - E5 - D5 B4 - G#4"}});
    b.section("blow", {{"bass", form(walkA, walkD, walkE, twelve)}, {"gtr", form(gA, gD, gE, twelve)},
                       {"keys", form(kA, kD, kE, twelve)}, {"drums", drums12},
                       {"lead", "A5 - A5 A5 - A5 G5 - E5 C5 - C#5 | E5 - A5 C6 - A5 G5 - E5 C#5 - A4 | A5 - C6 C#6 - C6 A5 - G5 E5 - C#5 |"
                                "E5/A5 - - - - - - - - . . . | D5 - F#5 A5 - C6 D6 - C6 A5 - F#5 | A5 - C6 D6 - C6 A5 - F#5 D5 - C5 |"
                                "C#5 - E5 A5 - C#6 E6 - C#6 A5 - E5 | C6/C#6 - - - - - A5 - - . . . | B5 - - B5 - - B5 - - G#5 - E5 |"
                                "A5 - - A5 - - A5 - - F#5 - D5 | E5 - A5 C#6 - E6 C#6 - A5 E5 - C#5 | E5 - - B4 - - E5 - - . . ."}});
    b.section("end", {{"bass", hold("A1", 24)}, {"gtr", hold("A2:6", 24)}, {"keys", hold("A4:7", 24)},
                      {"lead", hold("A5/C#6", 24)}, {"drums", "KC " + rep(".", 23)}});
    return b.build({"intro", "head", "blow", "head", "end"});
}

Song sixCylinderShake() {  // straight-eighths rock & roll, 12-bar in E
    Builder b("SIX CYLINDER SHAKE", "THE HOT RODDERS", 184, 2);
    b.echo(0.1f, 0.1f, 0.25f)
        .track("bass", uprightBass(), 0)
        .track("gtr", crunchGuitar(), -0.5f)
        .track("keys", pianoChords(), 0.45f)
        .track("lead", leadGuitar(), 0.1f);
    const std::string wE = "E1 G#1 B1 C#2 D2 C#2 B1 G#1", wA = "A1 C#2 E2 F#2 G2 F#2 E2 C#2", wB = "B1 D#2 F#2 G#2 A2 G#2 F#2 D#2";
    const std::string gE = rep("E2:5 E2:6", 4), gA = rep("A2:5 A2:6", 4), gB = rep("B2:5 B2:6", 4);
    const std::string kE = rep("E4:M", 8), kA = rep("A4:M", 8), kB = rep("B4:7", 8);
    // The quick change: E A E E A A E E B A E B.
    auto twelve = [](const std::string& e, const std::string& a, const std::string& b2) {
        return e + " " + a + " " + e + " " + e + " " + a + " " + a + " " + e + " " + e + " " + b2 + " " + a + " " + e + " " + b2;
    };
    const std::string beat = "KH H SH H KH KH SH H";
    const std::string drums12 = rep(beat, 11) + " S S S S T T L L";
    const std::string head = "E5 - G5 G#5 B5 - G#5 E5 | A5 - G5 E5 C#5 - A4 - | E5 - G5 G#5 B5 - C#6 B5 | G#5 - E5 - - - . . |"
                             "A5 - C6 C#6 E6 - C#6 A5 | G5 - E5 C#5 A4 - - - | B5 - G#5 E5 G5 G#5 B5 - | E6 - - - - - . . |"
                             "F#5 - D#5 F#5 B5 - A5 F#5 | E5 - C#5 E5 A5 - G5 E5 | G#5 - B5 - E6 - D6 B5 | B5/C6 - B5 - A5 F#5 D#5 B4";
    b.section("intro", {{"keys", rep(kE, 4)}, {"bass", rep(wE, 4)}, {"drums", rep(". H . H . H . H", 3) + " S S S S T T L L"}});
    b.section("head", {{"bass", twelve(wE, wA, wB)}, {"gtr", twelve(gE, gA, gB)}, {"keys", twelve(kE, kA, kB)},
                       {"drums", drums12}, {"lead", head}});
    b.section("keysolo", {{"bass", twelve(wE, wA, wB)}, {"gtr", twelve(gE, gA, gB)}, {"drums", drums12},
                          {"keys", twelve(rep("E5:M", 8), rep("A5:M", 8), rep("B5:7", 8))},
                          {"lead", rep(".", 96)}});
    b.section("end", {{"bass", "E1 - - - - - - -"}, {"gtr", hold("E2:6", 8)}, {"keys", hold("E4:M", 8)},
                      {"lead", hold("E6/G#6", 8)}, {"drums", "KC . . . . . . ."}});
    return b.build({"intro", "head", "keysolo", "head", "end"});
}


gs::FMPatch synthBrass(float detune) {  // big 80s synth brass
    gs::FMPatch p;
    p.alg = 4;
    p.fb = 0.72f;
    p.op[0] = {1, 0.7f, 0.03f, 0.5f, 0.75f, 0.2f};
    p.op[1] = {1, 1.0f, 0.02f, 0.8f, 0.85f, 0.25f};
    p.op[2] = {1, 0.5f, 0.03f, 0.5f, 0.75f, 0.2f, 1.5f + detune};
    p.op[3] = {1, 0.7f, 0.02f, 0.8f, 0.85f, 0.25f, -1.2f + detune};
    p.drive = 0.8f;
    p.tone = 3500;
    p.vibRate = 5.0f;
    p.vibDepth = 0.004f;
    p.vibDelay = 0.35f;
    p.glide = 1;
    p.echo = 0.2f;
    p.vol = 0.08f;
    return p;
}

Song ironHeart() {  // fight anthem, D minor: stabbed chords and a big chorus
    Builder b("IRON HEART", "STEEL RAVEN", 108, 4);
    b.echo(0.28f, 0.25f, 0.2f)
        .track("gtrL", rhythmGuitar(-0.8f), -0.8f)
        .track("gtrR", rhythmGuitar(0.8f), 0.8f)
        .track("bass", rockBass(), 0)
        .track("lead", leadGuitar(), 0.1f)
        .track("keys", pad(true), -0.25f);
    const std::string beat = "K . H . S . H K K . H . S . H .";
    const std::string riff = "D2 . . . . . D2 . . . D2 . . . . . | D2 . . . . . F2 . . . G2 . . . . .";
    const std::string riffBass = "D1 . . . . . D1 . . . D1 . . . . . | D1 . . . . . F1 . . . G1 . . . . .";
    const std::string riffDrums = "KC . . . . . KC . . . KC . . . . . | KC . . . . . KC . . . KC . . S S S";
    const std::string verseGtr = "D2 x x x x x x x x x x x C2 x x x | A#1 x x x x x x x C2 x x x D2 - - -";
    const std::string verseBass = "D1 - D1 - D1 - D1 - D1 - D1 - C2 - C2 - | A#1 - A#1 - A#1 - A#1 - C2 - C2 - D2 - - -";
    b.section("riff", {{"gtr*", riff}, {"bass", riffBass}, {"drums", riffDrums}});
    b.section("verse", {{"gtr*", verseGtr}, {"bass", verseBass}, {"drums", beat},
                        {"lead", "A4 - - - A4 - G4 - F4 - - - E4 - D4 - | F4 - - - - - E4 - D4 - - - . . . . |"
                                 "A4 - - - C5 - A4 - G4 - - - F4 - E4 - | D4 - - - - - - - . . . . . . . ."}});
    b.section("pre", {{"gtr*", rep("A#1 -", 8) + rep("C2 -", 8) + rep("A#1 -", 8) + "C2 - C2 - C2 - C2 - A1 - A1 - A1 - A1 -"},
                      {"bass", rep("A#1 -", 8) + rep("C2 -", 8) + rep("A#1 -", 8) + "C2 - C2 - C2 - C2 - A1 - A1 - A1 - A1 -"},
                      {"keys", hold("A#3:M", 16) + hold("C4:M", 16) + hold("A#3:M", 16) + hold("C4:M", 8) + hold("A3:M", 8)},
                      {"lead", "D5 - - - C5 - D5 - F5 - - - E5 - - - | E5 - - - D5 - E5 - G5 - - - - - - - |"
                               "F5 - - - E5 - F5 - A5 - - - G5 - F5 - | E5 - - - - - - - C#5 - - - - - - -"},
                      {"drums", rep(beat, 3) + rep("S", 16)}});
    b.section("chorus", {{"gtr*", rep("D2 -", 8) + rep("A#1 -", 8) + rep("F2 -", 8) + rep("C2 -", 8)},
                         {"bass", rep("D2 -", 8) + rep("A#1 -", 8) + rep("F1 -", 8) + rep("C2 -", 8)},
                         {"keys", hold("D4:m", 16) + hold("A#3:M", 16) + hold("F3:M", 16) + hold("C4:M", 16)},
                         {"lead", "A5 - - - - - G5 - A5 - - - D6 - - - | C6 - - - A#5 - A5 - F5 - - - - - - - |"
                                  "A5 - - - - - G5 - A5 - - - C6 - A5 - | G5 - - - - - E5 - C5 - - - - - - - |"
                                  "A5 - - - - - G5 - A5 - - - D6 - - - | D6 - - - C6 - A#5 - C6 - - - D6 - F6 - |"
                                  "E6 - - - D6 - C6 - A5 - - - G5 - A5 - | A5 - - - - - - - - - - - . . . ."},
                         {"drums", "KC . H . S . H K K . H . S . H . | " + rep(beat, 6) + " K . H . S . H . K . S . S S S S"}});
    b.section("solo", {{"gtr*", verseGtr}, {"bass", verseBass}, {"drums", beat},
                       {"lead", "D5 F5 G5 A5 C6 - A5 - C6/D6 - - - C6 A5 G5 F5 | G5 - - - F5 - D5 - F5 G5 A5 - G5 F5 D5 C5 |"
                                "D5 - F5 - A5 - D6 - F6 - E6 D6 C6 - A5 - | A#5/C6 - - - - - - - A5 G5 F5 E5 D5 - - - |"
                                "A5 C6 D6 F6 A6 - G6 F6 D6 - C6 A5 G5 - F5 - | D6 C6 A5 C6 D6 C6 A5 C6 D6 C6 A5 G5 F5 E5 D5 C5 |"
                                "D5 - - - A5 - - - D6 - - - F6 - - - | E6/F6 - - - - - - - E6 - D6 - C#6 - - -"}});
    b.section("outro", {{"gtr*", "D2 . . . . . D2 . . . D2 . . . . . | " + hold("D2", 16)},
                        {"bass", "D1 . . . . . D1 . . . D1 . . . . . | " + hold("D1", 16)},
                        {"keys", rep(".", 16) + hold("D4:m", 16)},
                        {"lead", rep(".", 16) + hold("A5", 16)},
                        {"drums", "KC . . . . . KC . . . KC . . . . . | KC " + rep(".", 15)}});
    return b.build({"riff", "riff", "verse", "pre", "chorus", "riff", "verse", "pre", "chorus", "solo", "chorus", "outro"});
}

Song launchWindow() {  // synth-brass arena anthem, A minor
    Builder b("LAUNCH WINDOW", "NORTHERN LIGHTS", 118, 4);
    b.echo(0.38f, 0.35f, 0.3f)
        .track("brassA", synthBrass(0), -0.2f)
        .track("brassB", synthBrass(0.9f), 0.35f)
        .track("pad", pad(false), 0)
        .track("bass", rockBass(), 0)
        .track("gtr", rhythmGuitar(0), -0.6f)
        .track("lead", leadGuitar(), 0.15f);
    const std::string beat = "K . H . S . H K K . H . S . H .";
    const std::string hookA = "E5 - A5 - B5 - C6 - - - B5 - A5 - E6 - | F6 - - - E6 - C6 - A5 - - - C6 - - - |"
                              "D6 - G5 - B5 - D6 - G6 - - - D6 - B5 - | E6 - - - - - D6 - B5 - G#5 - - - - -";
    const std::string hookB = "C5 - E5 - G5 - A5 - - - G5 - E5 - C6 - | C6 - - - A5 - A5 - F5 - - - A5 - - - |"
                              "B5 - D5 - G5 - B5 - D6 - - - B5 - G5 - | B5 - - - - - A5 - G#5 - E5 - - - - -";
    const std::string pads = hold("A3:m", 16) + hold("F3:M", 16) + hold("G3:M", 16) + hold("E3:M", 16);
    const std::string gallop = rep("A1 - A1 A1", 4) + rep("F1 - F1 F1", 4) + rep("G1 - G1 G1", 4) + rep("E1 - E1 E1", 4);
    const std::string gtr8 = rep("A2 -", 8) + rep("F2 -", 8) + rep("G2 -", 8) + rep("E2 -", 8);
    const std::string hookDrums = "KC . H . S . H K K . H . S . H . | " + rep(beat, 2) + " K . H . S . H . K K S . S S T L";
    b.section("intro", {{"brassA", hookA}, {"brassB", hookB}, {"pad", pads},
                        {"drums", rep(".", 48) + " S . S S S . S S T . T T L . L L"}});
    b.section("hook", {{"brassA", hookA}, {"brassB", hookB}, {"pad", pads}, {"bass", gallop}, {"gtr", gtr8}, {"drums", hookDrums}});
    b.section("verse", {{"gtr", rep("A2 x x x", 4) + rep("F2 x x x", 4) + rep("G2 x x x", 4) + rep("E2 x x x", 4)},
                        {"bass", gallop}, {"pad", pads}, {"drums", "K . H . S . H . K . H . S . H ."},
                        {"lead", "A4 - - - C5 - B4 - A4 - - - E4 - - - | F4 - - - A4 - G4 - F4 - - - C4 - - - |"
                                 "G4 - - - B4 - A4 - G4 - - - D5 - - - | E5 - - - D5 - C5 - B4 - - - G#4 - - -"}});
    b.section("chorus", {{"gtr", gtr8}, {"bass", gallop}, {"pad", pads},
                         {"brassA", hold("E5", 16) + hold("F5", 16) + hold("G5", 16) + hold("G#5", 16)},
                         {"brassB", hold("C5", 16) + hold("C5", 16) + hold("D5", 16) + hold("E5", 16)},
                         {"lead", "C6 - - - B5 - A5 - B5 - - - C6 - E6 - | C6 - - - - - A5 - F5 - - - A5 - - - |"
                                  "B5 - - - - - G5 - D6 - - - B5 - G5 - | B5 - - - G#5 - - - E5 - - - - - - -"},
                         {"drums", "KC . H . S . H K K . H . S . H . | " + rep(beat, 6) + " K . H . S . H . K . S . S S S S"}});
    b.section("solo", {{"gtr", gtr8}, {"bass", gallop}, {"pad", pads}, {"drums", hookDrums},
                       {"lead", "E6 D6 C6 B5 A5 B5 C6 E6 A6 - - - G6 E6 C6 A5 | F6 - E6 - C6 - A5 - C6 - E6 - F6 - A6 - |"
                                "G6 F6 D6 B5 G5 B5 D6 G6 B6/C7 - - - B6 G6 D6 B5 | G#6 - - - B6 - - - E6 - - - . . . ."}});
    b.section("end", {{"brassA", hold("A5", 32)}, {"brassB", hold("E5", 32)}, {"pad", hold("A3:m", 32)},
                      {"bass", hold("A1", 16) + rep(".", 16)}, {"gtr", hold("A2", 32)}, {"drums", "KC " + rep(".", 31)}});
    return b.build({"intro", "hook", "verse", "chorus", "hook", "verse", "chorus", "solo", "hook", "chorus", "end"});
}

}  // namespace

// ------------------------------------------------------------ the radio

Radio::Radio(gs::APU& apu) : apu_(apu) {
    songs_ = {neonKnuckles(), redlineRomance(), heartsInChrome(), rainOnNeon(), gravelBoogie(), sixCylinderShake(),
              ironHeart(), launchWindow()};
    stations_[0] = {"88.1", "THE BLADE", "HAIR METAL", V_ST_BLADE, {0, 1}};
    stations_[1] = {"101.5", "NEON FM", "SYNTH POP", V_ST_NEON, {2, 3}};
    stations_[2] = {"94.7", "KOOL", "ROCK & ROLL", V_ST_KOOL, {4, 5}};
    stations_[3] = {"96.6", "ARENA FM", "ANTHEMS", V_ST_ARENA, {6, 7}};
    kick_ = drum(0);
    snare_ = drum(1);
    clap_ = drum(2);
    hat_ = drum(3);
    openHat_ = drum(4);
    crash_ = drum(5);
    ride_ = drum(6);
    tomHi_ = drum(7);
    tomLo_ = drum(8);
    snareClap_ = snare_;
    for (size_t i = 0; i < snareClap_.data.size() && i < clap_.data.size(); i++) snareClap_.data[i] += clap_.data[i] * 0.7f;
    startSong(true);
}

void Radio::release() {
    for (int i = 0; i < MAX_TRACKS; i++) {
        apu_.keyOff(FIRST_CH + i);
        live_[i] = Live{};
    }
}

void Radio::startSong(bool fromTop) {
    if (station_ < 0) return;
    if (station_ == USER_STATION) {  // your music: userTick() starts the track once it has loaded
        card_ = 200;
        return;
    }
    Station& st = stations_[station_];
    const Song& s = songs_[st.songs[st.song]];
    if (fromTop) st.step = 0;
    for (int i = 0; i < MAX_TRACKS; i++) {
        const int ch = FIRST_CH + i;
        if (i < int(s.tracks.size())) {
            apu_.setPatch(ch, s.tracks[i].patch);
            apu_.setPan(ch, s.tracks[i].pan);
        } else {
            apu_.keyOff(ch);
        }
    }
    apu_.setEcho(s.echoTime, s.echoFb, s.echoWet);
    timer_ = 0;
    card_ = 200;
}

int Radio::next() {
    const int count = NUM_STATIONS + (user_.count() ? 1 : 0);
    tuneTo(station_ + 1 >= count ? -1 : station_ + 1);
    return station_;
}

int Radio::voiceFor(int station) const {
    return station >= 0 && station < NUM_STATIONS ? stations_[station].voice : -1;
}

void Radio::loadUserMusic(const std::string& dir, const std::string& cacheDir) { user_.scan(dir, cacheDir); }

void Radio::userStop() {
    if (userPlaying_) userPos_ = apu_.position(1);
    apu_.play(1, nullptr);
    userPlaying_ = false;
}

// Your music: start the current track once loaded, move on when it ends.
void Radio::userTick() {
    const size_t n = user_.count();
    if (!n) return;
    if (!userPlaying_) {
        user_.request(userTrack_);
        if (const gs::Sample* s = user_.get(userTrack_)) {
            apu_.play(1, s, 0.6f, 1, 0, userPos_);
            userPlaying_ = true;
            userSkips_ = 0;
            card_ = 200;
            user_.request(userNext());  // get the next one ready
        } else if (user_.failed(userTrack_) && ++userSkips_ <= int(n)) {
            userTrack_ = userNext();  // unreadable file: skip it
            userPos_ = 0;
        }
        return;
    }
    if (apu_.playing(1)) {
        userPos_ = apu_.position(1);
        return;
    }
    // Track finished.
    userPlaying_ = false;
    userTrack_ = userNext();
    userPos_ = 0;
    user_.keepOnly(userTrack_, userNext());
}

void Radio::tuneTo(int station) {
    if (station_ == USER_STATION) userStop();  // remembers where you were in the track
    else if (station_ >= 0) stations_[station_].leftAt = frame_;
    release();
    station_ = station;
    static_ = 16;  // a burst of static while the dial moves
    apu_.noiseBurst(0.22f, 11000, 0.18f);
    card_ = 200;
    if (station_ < 0) return;
    if (station_ == USER_STATION) return;
    // The station kept broadcasting while we were away: skip ahead.
    Station& st = stations_[station_];
    long away = st.leftAt ? frame_ - st.leftAt : 0;
    while (away > 0) {
        const Song& s = songs_[st.songs[st.song]];
        long left = long((s.steps - st.step) * s.stepFrames());
        if (away < left) {
            st.step += int(away / s.stepFrames());
            break;
        }
        away -= left + 40;
        st.song = (st.song + 1) % int(st.songs.size());
        st.step = 0;
    }
}

void Radio::duck(bool on) {
    if (on == ducked_) return;
    ducked_ = on;
    const float g = on ? 0.45f : 1.0f;
    for (int i = 0; i < MAX_TRACKS; i++) apu_.setGain(FIRST_CH + i, g);
    for (int c = 1; c < gs::PCM_CHANNELS; c++) apu_.setPcmGain(c, g);
}

std::string Radio::stationLine() const {
    if (station_ < 0) return "RADIO OFF";
    if (station_ == USER_STATION) return "YOUR MUSIC  " + std::to_string(user_.count()) + (user_.count() == 1 ? " TRACK" : " TRACKS");
    const Station& st = stations_[station_];
    return std::string(st.freq) + " " + st.name + "  " + st.genre;
}

std::string Radio::songLine() const {
    if (station_ < 0) return "";
    if (station_ == USER_STATION) return userPlaying_ ? user_.title(userTrack_) : "LOADING " + user_.title(userTrack_);
    const Station& st = stations_[station_];
    const Song& s = songs_[st.songs[st.song]];
    return s.title + " - " + s.artist;
}

void Radio::playDrums(const std::string& tok) {
    bool k = false, s = false, p = false, h = false, o = false, c = false, r = false, t = false, l = false;
    for (char ch : tok) {
        switch (ch) {
            case 'K': k = true; break;
            case 'S': s = true; break;
            case 'P': p = true; break;
            case 'H': h = true; break;
            case 'O': o = true; break;
            case 'C': c = true; break;
            case 'R': r = true; break;
            case 'T': t = true; break;
            case 'L': l = true; break;
        }
    }
    if (k) apu_.play(1, &kick_, 0.75f);
    else if (t) apu_.play(1, &tomHi_, 0.6f, 1, -0.3f);
    else if (l) apu_.play(1, &tomLo_, 0.65f, 1, 0.3f);
    if (s && p) apu_.play(2, &snareClap_, 0.5f);
    else if (s) apu_.play(2, &snare_, 0.55f);
    else if (p) apu_.play(2, &clap_, 0.5f, 1, 0.1f);
    if (c) apu_.play(3, &crash_, 0.35f, 1, 0.35f);
    else if (o) apu_.play(3, &openHat_, 0.22f, 1, -0.25f);
    else if (r) apu_.play(3, &ride_, 0.22f, 1, 0.3f);
    else if (h) apu_.play(3, &hat_, 0.2f, 1, -0.25f);
}

void Radio::doStep() {
    Station& st = stations_[station_];
    const Song& s = songs_[st.songs[st.song]];
    if (st.step >= s.steps) {  // song over: next in rotation after a short pause
        release();
        st.song = (st.song + 1) % int(st.songs.size());
        st.step = 0;
        gap_ = 40;
        return;
    }
    const float sf = s.stepFrames();
    for (size_t i = 0; i < s.tracks.size() && i < MAX_TRACKS; i++) {
        const Step& e = s.tracks[i].steps[st.step];
        const int ch = FIRST_CH + int(i);
        Live& lv = live_[i];
        switch (e.type) {
            case Step::Note: {
                float m[4];
                if (!chordMuls(e.chord, m))
                    for (int k = 0; k < 4; k++) m[k] = s.tracks[i].patch.op[k].mul;
                if (s.tracks[i].patch.alg == 7 || e.chord)
                    for (int k = 0; k < 4; k++) apu_.setOpMul(ch, k, m[k]);
                apu_.keyOn(ch, e.f);
                lv.cur = e.f;
                lv.offIn = 0;
                lv.bendIn = e.f2 > 0 ? std::max(1, int(sf * 0.9f)) : 0;
                lv.bendTo = e.f2;
                break;
            }
            case Step::Chug:
                if (lv.cur > 0) {
                    apu_.keyOn(ch, lv.cur);
                    lv.offIn = std::max(1, int(sf * 0.45f));
                }
                break;
            case Step::Off:
                apu_.keyOff(ch);
                lv.offIn = lv.bendIn = 0;
                break;
            default:
                break;
        }
    }
    if (!s.drums[st.step].empty()) playDrums(s.drums[st.step]);
    st.step++;
}

void Radio::tick() {
    frame_++;
    if (card_ > 0) card_--;
    if (static_ > 0) {
        if (--static_ == 0 && station_ >= 0) startSong(false);
        return;
    }
    if (station_ < 0) return;
    if (station_ == USER_STATION) {
        userTick();
        return;
    }
    if (gap_ > 0) {
        if (--gap_ == 0) startSong(true);
        return;
    }
    for (int i = 0; i < MAX_TRACKS; i++) {
        Live& lv = live_[i];
        if (lv.offIn > 0 && --lv.offIn == 0) apu_.keyOff(FIRST_CH + i);
        if (lv.bendIn > 0 && --lv.bendIn == 0) apu_.setFreq(FIRST_CH + i, lv.bendTo);
    }
    const float sf = songs_[stations_[station_].songs[stations_[station_].song]].stepFrames();
    timer_ -= 1;
    while (timer_ <= 0 && gap_ == 0) {
        doStep();
        timer_ += sf;
    }
}


// ------------------------------------------------------------ your music

namespace {

std::string shQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

bool isAudio(const std::string& ext) {
    static const char* ok[] = {".wav", ".mp3", ".m4a", ".aac", ".flac", ".aif", ".aiff", ".caf", ".ogg"};
    for (const char* e : ok)
        if (ext == e) return true;
    return false;
}

}  // namespace

UserMusic::~UserMusic() {
    {
        std::lock_guard<std::mutex> l(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void UserMusic::scan(const std::string& dir, const std::string& cacheDir) {
#ifdef __EMSCRIPTEN__
    (void)dir;
    (void)cacheDir;  // a browser has no music folder
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::create_directories(cacheDir, ec);
    cache_ = cacheDir;
    std::vector<std::string> files;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (isAudio(ext)) files.push_back(it->path().string());
    }
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        auto t = std::make_unique<Track>();
        t->path = f;
        tracks_.push_back(std::move(t));
    }
    if (!tracks_.empty()) worker_ = std::thread([this] { run(); });
#endif
}

std::string UserMusic::title(size_t i) const {
    if (i >= tracks_.size()) return "";
    std::string name = std::filesystem::path(tracks_[i]->path).stem().string();
    std::string out;
    for (char c : name) {  // keep what the system font can draw
        unsigned char u = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(c)));
        out += (std::isalnum(u) || std::strchr(" .,:!?'-&()", u)) && u < 128 ? char(u) : ' ';
    }
    return out.substr(0, 36);
}

void UserMusic::request(size_t i) {
    if (i >= tracks_.size()) return;
    int expected = 0;
    if (!tracks_[i]->state.compare_exchange_strong(expected, 1)) return;  // already queued, ready or failed
    {
        std::lock_guard<std::mutex> l(m_);
        queue_.push_back(i);
    }
    cv_.notify_one();
}

const gs::Sample* UserMusic::get(size_t i) const {
    return i < tracks_.size() && tracks_[i]->state == 2 ? &tracks_[i]->sample : nullptr;
}

bool UserMusic::failed(size_t i) const { return i < tracks_.size() && tracks_[i]->state == 3; }

void UserMusic::keepOnly(size_t a, size_t b) {
    // Only ever called when the audio chip is no longer playing any of these.
    for (size_t i = 0; i < tracks_.size(); i++) {
        if (i == a || i == b || tracks_[i]->state != 2) continue;
        tracks_[i]->sample = gs::Sample{};
        tracks_[i]->state = 0;
    }
}

void UserMusic::run() {
    for (;;) {
        size_t i;
        {
            std::unique_lock<std::mutex> l(m_);
            cv_.wait(l, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            i = queue_.front();
            queue_.pop_front();
        }
        Track& t = *tracks_[i];
        t.state = decode(t) ? 2 : 3;
    }
}

// Get a 16-bit stereo 44.1 kHz copy of the track in memory. WAV is read
// directly; anything else is converted once into the cache with afconvert
// (built into macOS) or ffmpeg.
bool UserMusic::decode(Track& t) {
#ifdef __EMSCRIPTEN__
    (void)t;
    return false;
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string wav = t.path;
    std::string ext = fs::path(t.path).extension().string();
    for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".wav") {
        const auto size = fs::file_size(t.path, ec);
        const long long stamp = static_cast<long long>(fs::last_write_time(t.path, ec).time_since_epoch().count());
        wav = cache_ + std::to_string(std::hash<std::string>{}(t.path + "|" + std::to_string(static_cast<unsigned long long>(size)) + "|" + std::to_string(stamp))) + ".wav";
        if (!fs::exists(wav, ec)) {
            const std::string tmp = wav + ".part";
            std::string cmd;
            if (std::system("command -v afconvert >/dev/null 2>&1") == 0)
                cmd = "afconvert -f WAVE -d LEI16@44100 -c 2 " + shQuote(t.path) + " " + shQuote(tmp) + " >/dev/null 2>&1";
            else if (std::system("command -v ffmpeg >/dev/null 2>&1") == 0)
                cmd = "ffmpeg -loglevel error -y -i " + shQuote(t.path) + " -ac 2 -ar 44100 -c:a pcm_s16le -f wav " + shQuote(tmp) +
                      " >/dev/null 2>&1";
            if (cmd.empty() || std::system(cmd.c_str()) != 0) {
                fs::remove(tmp, ec);
                return false;
            }
            fs::rename(tmp, wav, ec);
            if (ec) return false;
        }
    }
    SDL_AudioSpec spec;
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(wav.c_str(), &spec, &buf, &len)) return false;
    SDL_AudioCVT cvt;
    bool ok = false;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, 44100) >= 0) {
        std::vector<Uint8> work(size_t(len) * size_t(std::max(1, cvt.len_mult)));
        std::memcpy(work.data(), buf, len);
        cvt.buf = work.data();
        cvt.len = int(len);
        if (cvt.needed == 0 || SDL_ConvertAudio(&cvt) == 0) {
            const size_t bytes = cvt.needed ? size_t(cvt.len_cvt) : size_t(len);
            t.sample.pcm16.resize(bytes / 2);
            std::memcpy(t.sample.pcm16.data(), work.data(), (bytes / 4) * 4);
            t.sample.data.clear();
            t.sample.rate = 44100;
            ok = t.sample.frames() > 1;
        }
    }
    SDL_FreeWAV(buf);
    return ok;
#endif
}

}  // namespace rally
