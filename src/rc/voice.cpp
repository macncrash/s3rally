#include "voice.h"

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace rc {

namespace {
constexpr int VOICE_CH = 0;  // PCM channel for speech

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}
}  // namespace

Voice::Voice(gs::APU& apu) : apu_(apu) {
    for (auto& o : ok_) o = false;
}

Voice::~Voice() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
}

void Voice::loadAsync(const std::string& dir) {
#ifdef __EMSCRIPTEN__
    (void)dir;
#else
    if (worker_.joinable()) return;
    worker_ = std::thread([this, dir]() {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        // The first run records every phrase with `say`, four at a time.
        std::vector<std::thread> crew;
        for (int w = 0; w < 4; w++) crew.emplace_back([this, dir, w]() { loadRange(dir, w, 4); });
        for (auto& t : crew) t.join();
    });
#endif
}

void Voice::loadRange(const std::string& dir, int first, int step) {
#ifdef __EMSCRIPTEN__
    (void)dir, (void)first, (void)step;
#else
    {
        for (int i = first; i < P_COUNT && !stop_; i += step) {
            const std::string path = dir + "p" + std::to_string(i) + ".wav";
            struct stat st;
            if (stat(path.c_str(), &st) != 0) {
                // A brisk British co-driver.
                const std::string cmd = "say -v Daniel -r 230 -o " + shellQuote(path) + " --file-format=WAVE --data-format=LEI16@22050 " +
                                        shellQuote(PHRASE_TEXT[i]) + " 2>/dev/null";
                if (std::system(cmd.c_str()) != 0) continue;
            }
            SDL_AudioSpec spec;
            Uint8* buf = nullptr;
            Uint32 len = 0;
            if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) continue;
            if (spec.format == AUDIO_S16LSB && spec.channels > 0) {
                const int16_t* s = reinterpret_cast<const int16_t*>(buf);
                const size_t n = len / 2 / spec.channels;
                std::vector<float> data(n);
                for (size_t k = 0; k < n; k++) data[k] = s[k * spec.channels] / 32768.0f;
                // Trim the silence either side so phrases run together like real speech.
                size_t a = 0, b = n;
                while (a < n && std::fabs(data[a]) < 0.02f) a++;
                while (b > a && std::fabs(data[b - 1]) < 0.02f) b--;
                a = a > 200 ? a - 200 : 0;
                b = std::min(n, b + 400);
                samples_[i].rate = spec.freq;
                samples_[i].data.assign(data.begin() + long(a), data.begin() + long(b));
                ok_[i] = !samples_[i].data.empty();
                if (ok_[i]) ready_++;
            }
            SDL_FreeWAV(buf);
        }
    }
#endif
}

void Voice::clear() {
    queue_.clear();
    gap_ = 0;
}

#ifdef __EMSCRIPTEN__
void Voice::call(const std::vector<int>& phrases, bool urgent) {
    std::string text;
    for (int p : phrases)
        if (p >= 0 && p < P_COUNT) text += std::string(text.empty() ? "" : ", ") + PHRASE_TEXT[p];
    if (text.empty()) return;
    EM_ASM(
        {
            if (!window.speechSynthesis) return;
            if ($1) speechSynthesis.cancel();
            const u = new SpeechSynthesisUtterance(UTF8ToString($0));
            u.rate = 1.35;
            speechSynthesis.speak(u);
        },
        text.c_str(), urgent);
}
void Voice::tick() {}
bool Voice::speaking() { return EM_ASM_INT({ return window.speechSynthesis && speechSynthesis.speaking ? 1 : 0; }) != 0; }
#else
void Voice::call(const std::vector<int>& phrases, bool urgent) {
    if (urgent) {
        queue_.clear();
        apu_.play(VOICE_CH, nullptr, 0);
    }
    for (int p : phrases)
        if (p >= 0 && p < P_COUNT) queue_.push_back(p);
    // A co-driver who has fallen behind skips ahead rather than reading stale notes.
    while (queue_.size() > 14) queue_.pop_front();
}

void Voice::tick() {
    if (apu_.playing(VOICE_CH)) return;
    if (gap_ > 0) {
        gap_--;
        return;
    }
    while (!queue_.empty()) {
        const int p = queue_.front();
        queue_.pop_front();
        if (!ok_[p]) continue;
        apu_.play(VOICE_CH, &samples_[p], 0.95f);
        gap_ = 1;
        return;
    }
}

bool Voice::speaking() { return apu_.playing(VOICE_CH) || !queue_.empty(); }
#endif

}  // namespace rc
