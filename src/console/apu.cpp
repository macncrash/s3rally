#include "apu.h"

#include <algorithm>
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

inline float lpCoef(float hz, int rate) { return hz > 0 ? 1 - std::exp(-TAU * hz / rate) : 1.0f; }
}  // namespace

void APU::init(int sampleRate) {
    std::lock_guard<std::mutex> l(m_);
    rate_ = std::max(8000, sampleRate);
    dt_ = 1.0f / rate_;
    for (auto& b : echoBuf_) b.assign(size_t(rate_) * 2, 0.0f);
    echoLen_ = size_t(rate_ * 0.34f);
    echoPos_ = 0;
}

void APU::setPatch(int ch, const FMPatch& p) {
    if (!fmOk(ch)) return;
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].patch = p;
    fm_[ch].vol = p.vol;
}

void APU::setOpMul(int ch, int op, float mul) {
    if (!fmOk(ch) || op < 0 || op > 3) return;
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].patch.op[op].mul = mul;
}

void APU::keyOn(int ch, float freq, float vol) {
    if (!fmOk(ch)) return;
    std::lock_guard<std::mutex> l(m_);
    Chan& c = fm_[ch];
    c.freq = c.targetFreq = freq;
    if (vol >= 0) c.vol = vol;
    c.since = 0;
    for (auto& o : c.op) o.stage = 1;
}

void APU::keyOff(int ch) {
    if (!fmOk(ch)) return;
    std::lock_guard<std::mutex> l(m_);
    for (auto& o : fm_[ch].op)
        if (o.stage) o.stage = 4;
}

void APU::setFreq(int ch, float freq) {
    if (!fmOk(ch)) return;
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].targetFreq = freq;
}

void APU::setVol(int ch, float vol) {
    if (!fmOk(ch)) return;
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].vol = vol;
}

void APU::setPan(int ch, float pan) {
    if (!fmOk(ch)) return;
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].pan = std::clamp(pan, -1.0f, 1.0f);
}

void APU::setGain(int ch, float gain) {
    if (!fmOk(ch)) return;
    std::lock_guard<std::mutex> l(m_);
    fm_[ch].gain = gain;
}

void APU::tone(int ch, float freq, float vol) {
    if (ch < 0 || ch >= PSG_CHANNELS) return;
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

void APU::play(int ch, const Sample* s, float vol, float pitch, float pan, double startSeconds) {
    if (ch < 0 || ch >= PCM_CHANNELS) return;
    std::lock_guard<std::mutex> l(m_);
    pcm_[ch].s = (s && s->frames() > 1) ? s : nullptr;
    pcm_[ch].pos = (s && startSeconds > 0) ? std::min(startSeconds * s->rate, double(s->frames() - 1)) : 0;
    pcm_[ch].vol = vol;
    pcm_[ch].pitch = pitch;
    pcm_[ch].pan = std::clamp(pan, -1.0f, 1.0f);
}

double APU::position(int ch) {
    if (ch < 0 || ch >= PCM_CHANNELS) return 0;
    std::lock_guard<std::mutex> l(m_);
    return pcm_[ch].s ? pcm_[ch].pos / pcm_[ch].s->rate : 0;
}

bool APU::playing(int ch) {
    if (ch < 0 || ch >= PCM_CHANNELS) return false;
    std::lock_guard<std::mutex> l(m_);
    return pcm_[ch].s != nullptr;
}

void APU::setPcmGain(int ch, float gain) {
    if (ch < 0 || ch >= PCM_CHANNELS) return;
    std::lock_guard<std::mutex> l(m_);
    pcm_[ch].gain = gain;
}

void APU::setEcho(float seconds, float feedback, float wet) {
    std::lock_guard<std::mutex> l(m_);
    if (echoBuf_[0].empty()) return;
    echoLen_ = std::clamp(size_t(seconds * rate_), size_t(1), echoBuf_[0].size() - 1);
    echoFb_ = std::clamp(feedback, 0.0f, 0.9f);
    echoWet_ = std::clamp(wet, 0.0f, 1.0f);
}

void APU::setMaster(float v) {
    std::lock_guard<std::mutex> l(m_);
    master_ = v;
}

void APU::setHostTrim(float v) {
    std::lock_guard<std::mutex> l(m_);
    hostTrim_ = std::clamp(v, 0.0f, 1.0f);
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
    c.freq += (c.targetFreq - c.freq) * std::clamp(p.glide, 0.0f, 1.0f);
    float e[4];
    bool any = false;
    for (int i = 0; i < 4; i++) {
        e[i] = envStep(c.op[i], p.op[i]);
        any |= c.op[i].stage != 0;
    }
    if (!any) {
        c.lp *= 0.99f;
        return c.lp * c.vol * c.gain;
    }
    c.since += dt_;
    float f = c.freq;
    if (p.vibDepth > 0) {
        c.vibPhase += p.vibRate * dt_;
        if (c.vibPhase > 1e6) c.vibPhase -= std::floor(c.vibPhase);
        float fade = std::clamp((c.since - p.vibDelay) / 0.3f, 0.0f, 1.0f);
        f *= 1 + p.vibDepth * fade * fastSin(c.vibPhase);
    }
    auto run = [&](int i, float mod) {
        Op& o = c.op[i];
        o.phase += (f * p.op[i].mul + p.op[i].detune) * dt_;
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
    if (p.drive > 0) out = std::tanh(out * (1 + p.drive)) * 0.75f;  // amp stage
    c.lp += (out - c.lp) * lpCoef(p.tone, rate_);                  // cabinet / tone control
    return c.lp * c.vol * c.gain;
}

void APU::render(float* out, int frames) {
    std::lock_guard<std::mutex> l(m_);
    const float lpk = 1 - std::exp(-TAU * 12000.0f / rate_);
    const bool echo = !echoBuf_[0].empty();
    for (int n = 0; n < frames; n++) {
        float L = 0, R = 0, eL = 0, eR = 0;
        for (auto& c : fm_) {
            float v = renderFM(c);
            float l = v * (1 - std::max(0.0f, c.pan) * 0.8f);
            float r = v * (1 + std::min(0.0f, c.pan) * 0.8f);
            L += l;
            R += r;
            eL += l * c.patch.echo;
            eR += r * c.patch.echo;
        }
        float mono = 0;
        for (auto& t : psg_) {
            t.cur += (t.vol - t.cur) * 0.01f;
            if (t.cur < 0.0001f || t.freq <= 0) continue;
            t.phase += t.freq * dt_;
            t.phase -= std::floor(t.phase);
            mono += (t.phase < 0.5 ? 1.0f : -1.0f) * t.cur;
        }
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
            size_t i = size_t(p.pos);
            if (i + 1 >= p.s->frames()) {
                p.s = nullptr;
                continue;
            }
            const float fr = float(p.pos - i);
            if (!p.s->pcm16.empty()) {  // stereo song
                const int16_t* d = p.s->pcm16.data();
                const float g = p.vol * p.gain / 32768.0f;
                L += (d[i * 2] + (d[i * 2 + 2] - d[i * 2]) * fr) * g;
                R += (d[i * 2 + 1] + (d[i * 2 + 3] - d[i * 2 + 1]) * fr) * g;
            } else {
                const auto& d = p.s->data;
                float v = (d[i] + (d[i + 1] - d[i]) * fr) * p.vol * p.gain;
                L += v * (1 - std::max(0.0f, p.pan) * 0.8f);
                R += v * (1 + std::min(0.0f, p.pan) * 0.8f);
            }
            p.pos += double(p.pitch) * p.s->rate / rate_;
        }
        L += mono;
        R += mono;
        if (echo) {
            // Stereo echo, cross-fed so the repeats bounce between the speakers.
            const size_t size = echoBuf_[0].size();
            const size_t rd = (echoPos_ + size - echoLen_) % size;
            const float dL = echoBuf_[0][rd], dR = echoBuf_[1][rd];
            echoBuf_[0][echoPos_] = eL + dR * echoFb_;
            echoBuf_[1][echoPos_] = eR + dL * echoFb_;
            echoPos_ = (echoPos_ + 1) % size;
            L += dL * echoWet_;
            R += dR * echoWet_;
        }
        float ch[2] = {L * master_ * hostTrim_, R * master_ * hostTrim_};
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
