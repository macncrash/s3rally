#include "apu.h"

#include <cmath>

namespace gs {

namespace {
constexpr int SINE_BITS = 13;
constexpr int SINE_SIZE = 1 << SINE_BITS;
constexpr float TAU = 6.28318530718f;
constexpr float MOD_DEPTH = 4.0f;  // radians of phase modulation per unit of modulator output

struct SineTable {
    float t[SINE_SIZE + 1];
    SineTable() {
        for (int i = 0; i <= SINE_SIZE; i++) t[i] = std::sin(TAU * i / SINE_SIZE);
    }
};
const SineTable kSine;

inline float fastSin(double phase) {  // phase in cycles
    double f = phase - std::floor(phase);
    float p = float(f * SINE_SIZE);
    int i = int(p);
    if (i >= SINE_SIZE) i = SINE_SIZE - 1;  // f can round up to exactly 1.0
    if (i < 0) i = 0;
    float fr = p - i;
    return kSine.t[i] + (kSine.t[i + 1] - kSine.t[i]) * fr;
}
}  // namespace

void APU::init(int sampleRate) {
    std::lock_guard<std::mutex> l(m_);
    rate_ = sampleRate;
    dt_ = 1.0f / sampleRate;
}

void APU::setPatch(int ch, const FMPatch& p) {
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].patch = p;
    fm_[ch].vol = p.vol;
}

void APU::keyOn(int ch, float freq, float vol) {
    std::lock_guard<std::mutex> l(m_);
    Chan& c = fm_[ch];
    c.freq = c.targetFreq = freq;
    if (vol >= 0) c.vol = vol;
    for (auto& o : c.op) o.stage = 1;
}

void APU::keyOff(int ch) {
    std::lock_guard<std::mutex> l(m_);
    for (auto& o : fm_[ch].op)
        if (o.stage) o.stage = 4;
}

void APU::setFreq(int ch, float freq) {
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].targetFreq = freq;
}

void APU::setVol(int ch, float vol) {
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].vol = vol;
}

void APU::setPan(int ch, float pan) {
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].pan = pan;
}

void APU::tone(int ch, float freq, float vol) {
    std::lock_guard<std::mutex> l(m_);
    psg_[ch].freq = freq;
    psg_[ch].vol = vol;
}

void APU::noise(float vol, float rate, bool periodic) {
    std::lock_guard<std::mutex> l(m_);
    noiseVol_ = vol;
    noiseRate_ = rate;
    noisePeriodic_ = periodic;
}

void APU::noiseBurst(float vol, float rate, float decay) {
    std::lock_guard<std::mutex> l(m_);
    noiseCur_ = vol;
    noiseRate_ = rate;
    noiseDecay_ = decay;
}

void APU::play(int ch, const Sample* s, float vol, float pitch) {
    std::lock_guard<std::mutex> l(m_);
    pcm_[ch].s = s;
    pcm_[ch].pos = 0;
    pcm_[ch].vol = vol;
    pcm_[ch].pitch = pitch;
}

bool APU::playing(int ch) {
    std::lock_guard<std::mutex> l(m_);
    return pcm_[ch].s != nullptr;
}

void APU::setMaster(float v) {
    std::lock_guard<std::mutex> l(m_);
    master_ = v;
}

void APU::silence() {
    std::lock_guard<std::mutex> l(m_);
    for (auto& c : fm_)
        for (auto& o : c.op) o.stage = o.stage ? 4 : 0;
    for (auto& t : psg_) t.vol = 0;
    noiseVol_ = noiseCur_ = 0;
    for (auto& p : pcm_) p.s = nullptr;
}

float APU::envStep(Op& o, const FMOp& p) {
    switch (o.stage) {
        case 1:
            o.env += dt_ / std::max(p.ar, 0.0005f);
            if (o.env >= 1) {
                o.env = 1;
                o.stage = 2;
            }
            break;
        case 2:
            o.env = p.sl + (o.env - p.sl) * std::exp(-dt_ * 4.6f / std::max(p.dr, 0.001f));
            if (o.env - p.sl < 0.001f) o.stage = 3;
            break;
        case 3:
            o.env = p.sl;
            break;
        case 4:
            o.env *= std::exp(-dt_ * 4.6f / std::max(p.rr, 0.001f));
            if (o.env < 0.0001f) {
                o.env = 0;
                o.stage = 0;
            }
            break;
        default:
            o.env = 0;
    }
    return o.env;
}

float APU::renderFM(Chan& c) {
    const FMPatch& p = c.patch;
    // Portamento towards the target frequency (engines glide smoothly).
    c.freq += (c.targetFreq - c.freq) * 0.004f;
    float e[4];
    bool any = false;
    for (int i = 0; i < 4; i++) {
        e[i] = envStep(c.op[i], p.op[i]);
        any |= c.op[i].stage != 0;
    }
    if (!any) return 0;
    auto run = [&](int i, float mod) {
        Op& o = c.op[i];
        o.phase += (c.freq * p.op[i].mul + p.op[i].detune) * dt_;
        if (o.phase > 1e6) o.phase -= std::floor(o.phase);
        float v = fastSin(o.phase + mod * (MOD_DEPTH / TAU)) * e[i] * p.op[i].level;
        o.out2 = o.out1;
        o.out1 = v;
        return v;
    };
    const float fb = c.op[0].stage ? p.fb * (c.op[0].out1 + c.op[0].out2) * 0.5f : 0;
    float o0 = run(0, fb), o1, o2, o3, out;
    switch (p.alg) {
        case 0: o1 = run(1, o0); o2 = run(2, o1); out = run(3, o2); break;
        case 1: o1 = run(1, 0); o2 = run(2, o0 + o1); out = run(3, o2); break;
        case 2: o1 = run(1, 0); o2 = run(2, o1); out = run(3, o0 + o2); break;
        case 3: o1 = run(1, o0); o2 = run(2, 0); out = run(3, o1 + o2); break;
        case 4: o1 = run(1, o0); o2 = run(2, 0); o3 = run(3, o2); out = o1 + o3; break;
        case 5: o1 = run(1, o0); o2 = run(2, o0); o3 = run(3, o0); out = o1 + o2 + o3; break;
        case 6: o1 = run(1, o0); o2 = run(2, 0); o3 = run(3, 0); out = o1 + o2 + o3; break;
        default: o1 = run(1, 0); o2 = run(2, 0); o3 = run(3, 0); out = o0 + o1 + o2 + o3; break;
    }
    return out * c.vol;
}

void APU::render(float* out, int frames) {
    std::lock_guard<std::mutex> l(m_);
    const float lpk = 1 - std::exp(-TAU * 11000.0f / rate_);
    for (int n = 0; n < frames; n++) {
        float L = 0, R = 0;
        for (auto& c : fm_) {
            float v = renderFM(c);
            L += v * (1 - std::max(0.0f, c.pan) * 0.7f);
            R += v * (1 + std::min(0.0f, c.pan) * 0.7f);
        }
        float mono = 0;
        for (auto& t : psg_) {
            t.cur += (t.vol - t.cur) * 0.01f;
            if (t.cur < 0.0001f || t.freq <= 0) continue;
            t.phase += t.freq * dt_;
            t.phase -= std::floor(t.phase);
            mono += (t.phase < 0.5 ? 1.0f : -1.0f) * t.cur;
        }
        // Noise: 15-bit LFSR clocked at noiseRate_.
        noisePhase_ += noiseRate_ * dt_;
        while (noisePhase_ >= 1) {
            noisePhase_ -= 1;
            uint32_t bit = noisePeriodic_ ? (lfsr_ & 1) : ((lfsr_ ^ (lfsr_ >> 1)) & 1);
            lfsr_ = (lfsr_ >> 1) | (bit << 14);
            noiseOut_ = (lfsr_ & 1) ? 1.0f : -1.0f;
        }
        if (noiseDecay_ > 0) noiseCur_ *= std::exp(-dt_ / noiseDecay_);
        mono += noiseOut_ * std::max(noiseVol_, noiseCur_);
        for (auto& p : pcm_) {
            if (!p.s) continue;
            const auto& d = p.s->data;
            size_t i = size_t(p.pos);
            if (i + 1 >= d.size()) {
                p.s = nullptr;
                continue;
            }
            float fr = float(p.pos - i);
            mono += (d[i] + (d[i + 1] - d[i]) * fr) * p.vol;
            p.pos += p.pitch * p.s->rate / rate_;
        }
        L += mono;
        R += mono;
        float ch[2] = {L * master_, R * master_};
        for (int k = 0; k < 2; k++) {
            lp_[k] += (ch[k] - lp_[k]) * lpk;
            float y = lp_[k] - dcIn_[k] + 0.995f * dcOut_[k];
            dcIn_[k] = lp_[k];
            dcOut_[k] = y;
            out[n * 2 + k] = std::tanh(y);
        }
    }
}

}  // namespace gs
