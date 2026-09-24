// S3 RALLY player profile: a display name the player chooses, plus a
// permanent unique ID. Names don't have to be unique (two RADRACERs are
// fine); the ID is what identifies a player, now on the local network and
// later on an internet server. Saved on this device like a save game.
#pragma once
#include <atomic>
#include <string>
#include <thread>

namespace rally {

constexpr size_t PROFILE_NAME_MAX = 12;
constexpr const char* ID_SERVICE_URL = "https://urandom.ai/v1/random/uuid";

struct Profile {
    std::string name;
    std::string id;        // UUID, lower-case, 36 characters
    std::string idSource;  // "urandom.ai" or "local"
    std::string created;   // UTC, ISO 8601
    bool valid() const { return !name.empty() && !id.empty(); }
    std::string shortId() const;  // first 8 hex digits, upper-case, for display
    std::string serialize() const;
    static Profile parse(const std::string& text);
};

bool validUuid(const std::string& s);
std::string localUuid();  // UUID v4 from this machine's /dev/urandom (or the browser's crypto)
std::string utcNow();
char nameChar(char c);    // upper-cased if allowed in a name, else 0

// Fetches a new ID in the background: urandom.ai if allowed and reachable
// (certificate checks on), otherwise a local UUID.
class IdFetcher {
public:
    ~IdFetcher();
    void start(bool useService);
    bool done();
    const std::string& id() const { return id_; }
    const std::string& source() const { return source_; }

private:
    std::thread worker_;
    std::atomic<bool> done_{false};
    bool started_ = false;
#ifdef __EMSCRIPTEN__
    bool web_ = false;
    int webFrames_ = 0;
#endif
    std::string id_, source_;
};

}  // namespace rally
