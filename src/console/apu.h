// S3-16 sound hardware.
//   FM:  6 channels x 4 operators, 8 algorithms, operator-1 feedback, ADSR
//        envelopes (modelled on the 16-bit era's favourite FM chip)
//   PSG: 3 square channels + 1 LFSR noise channel
//   PCM: 2 sample channels (speech, effects)
// The CPU side writes "registers" through the methods below; the audio
// thread renders samples. All calls are thread-safe.
#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

namespace gs {

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
    void keyOn(int ch, float freq, float vol = -1);
    void keyOff(int ch);
    void setFreq(int ch, float freq);  // glide without retrigger
    void setVol(int ch, float vol);
    void setPan(int ch, float pan);  // -1..1

    void tone(int ch, float freq, float vol);  // PSG square, vol 0 = off
    void noise(float vol, float rate, bool periodic = false);
    void noiseBurst(float vol, float rate, float decay);  // one-shot

    void play(int ch, const Sample* s, float vol = 1, float pitch = 1);
    bool playing(int ch);
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
        float freq = 440, vol = 0.25f, pan = 0;
        float targetFreq = 440;
    };
    struct Tone {
        double phase = 0;
        float freq = 0, vol = 0, cur = 0;
    };
    struct Pcm {
        const Sample* s = nullptr;
        double pos = 0;
        float vol = 1, pitch = 1;
    };

    float renderFM(Chan& c);
    float envStep(Op& o, const FMOp& p);

    std::mutex m_;
    int rate_ = 48000;
    float dt_ = 1.0f / 48000;
    Chan fm_[6];
    Tone psg_[3];
    uint32_t lfsr_ = 1;
    double noisePhase_ = 0;
    float noiseVol_ = 0, noiseCur_ = 0, noiseRate_ = 4000, noiseDecay_ = 0, noiseOut_ = 0;
    bool noisePeriodic_ = false;
    Pcm pcm_[2];
    float master_ = 0.8f;
    float lp_[2] = {0, 0};
    float dcIn_[2] = {0, 0}, dcOut_[2] = {0, 0};
};

}  // namespace gs
