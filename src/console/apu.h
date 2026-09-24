// S3-16 sound hardware.
//   FM:  9 channels x 4 operators, 8 algorithms, operator-1 feedback, ADSR
//        envelopes, plus per-channel overdrive, tone filter, vibrato, glide
//        and echo send (the "stage" expansion of the FM chip)
//   PSG: 3 square channels + 1 LFSR noise channel
//   PCM: 4 sample channels (speech, drums, effects)
//   FX:  stereo echo
// The CPU side writes "registers" through the methods below; the audio
// thread renders samples. All calls are thread-safe; out-of-range channel
// numbers are ignored.
#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

namespace gs {

constexpr int FM_CHANNELS = 9;
constexpr int PSG_CHANNELS = 3;
constexpr int PCM_CHANNELS = 4;

struct FMOp {
    float mul = 1;      // frequency multiple
    float level = 1;    // output level 0..1 (modulators: modulation depth)
    float ar = 0.002f;  // attack time (s)
    float dr = 0.3f;    // decay time to sustain (s)
    float sl = 0.6f;    // sustain level 0..1
    float rr = 0.15f;   // release time (s)
    float detune = 0;   // Hz offset
};

struct FMPatch {
    int alg = 4;
    float fb = 0;  // operator 1 feedback 0..1
    FMOp op[4];
    float vol = 0.25f;
    float drive = 0;      // overdrive amount (0 = clean)
    float tone = 0;       // low-pass cutoff in Hz after the drive (0 = off)
    float vibRate = 0;    // vibrato Hz
    float vibDepth = 0;   // vibrato depth as a fraction of pitch
    float vibDelay = 0;   // seconds after key-on before vibrato fades in
    float glide = 0.004f; // per-sample approach rate towards a new pitch (1 = instant)
    float echo = 0;       // send to the echo unit
};

struct Sample {
    std::vector<float> data;
    int rate = 22050;
};

class APU {
public:
    void init(int sampleRate);
    void render(float* out, int frames);  // stereo interleaved, audio thread

    void setPatch(int ch, const FMPatch& p);
    void setOpMul(int ch, int op, float mul);  // retune one operator (chords)
    void keyOn(int ch, float freq, float vol = -1);
    void keyOff(int ch);
    void setFreq(int ch, float freq);  // glide without retrigger
    void setVol(int ch, float vol);
    void setPan(int ch, float pan);  // -1..1
    void setGain(int ch, float gain);  // extra FM channel gain (mixing, ducking)

    void tone(int ch, float freq, float vol);  // PSG square, vol 0 = off
    void noise(float vol, float rate, bool periodic = false);
    void noiseBurst(float vol, float rate, float decay);  // one-shot

    void play(int ch, const Sample* s, float vol = 1, float pitch = 1, float pan = 0);
    bool playing(int ch);
    void setPcmGain(int ch, float gain);
    void setEcho(float seconds, float feedback, float wet);
    void setMaster(float v);
    void silence();

private:
    struct Op {
        double phase = 0;
        float env = 0;
        int stage = 0;  // 0 off, 1 attack, 2 decay, 3 sustain, 4 release
        float out1 = 0, out2 = 0;
    };
    struct Chan {
        FMPatch patch;
        Op op[4];
        float freq = 440, vol = 0.25f, pan = 0, gain = 1;
        float targetFreq = 440;
        float lp = 0;
        double vibPhase = 0;
        float since = 0;  // seconds since key-on
    };
    struct Tone {
        double phase = 0;
        float freq = 0, vol = 0, cur = 0;
    };
    struct Pcm {
        const Sample* s = nullptr;
        double pos = 0;
        float vol = 1, pitch = 1, pan = 0, gain = 1;
    };

    float renderFM(Chan& c);
    float envStep(Op& o, const FMOp& p);
    static bool fmOk(int ch) { return ch >= 0 && ch < FM_CHANNELS; }

    std::mutex m_;
    int rate_ = 48000;
    float dt_ = 1.0f / 48000;
    Chan fm_[FM_CHANNELS];
    Tone psg_[PSG_CHANNELS];
    uint32_t lfsr_ = 1;
    double noisePhase_ = 0;
    float noiseVol_ = 0, noiseCur_ = 0, noiseRate_ = 4000, noiseDecay_ = 0, noiseOut_ = 0;
    bool noisePeriodic_ = false;
    Pcm pcm_[PCM_CHANNELS];
    std::vector<float> echoBuf_[2];
    size_t echoPos_ = 0, echoLen_ = 1;
    float echoFb_ = 0.35f, echoWet_ = 0.3f;
    float master_ = 0.8f;
    float lp_[2] = {0, 0};
    float dcIn_[2] = {0, 0}, dcOut_[2] = {0, 0};
};

}  // namespace gs
