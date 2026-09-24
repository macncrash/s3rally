#include "profile.h"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "version.h"

namespace rally {

bool validUuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < 36; i++) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? s[i] != '-' : !std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

std::string localUuid() {
    unsigned char b[16] = {};
    bool ok = false;
#ifdef __EMSCRIPTEN__
    const char* js = emscripten_run_script_string(
        "(self.crypto && crypto.randomUUID) ? crypto.randomUUID() : ''");
    if (js && validUuid(js)) return js;
#else
    std::ifstream f("/dev/urandom", std::ios::binary);
    ok = f.read(reinterpret_cast<char*>(b), sizeof b).gcount() == sizeof b;
#endif
    if (!ok) {
        std::random_device rd;
        for (auto& x : b) x = static_cast<unsigned char>(rd());
    }
    b[6] = (b[6] & 0x0f) | 0x40;  // version 4
    b[8] = (b[8] & 0x3f) | 0x80;  // RFC 4122 variant
    char out[37];
    std::snprintf(out, sizeof out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3],
                  b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return out;
}

std::string utcNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

char nameChar(char c) {
    const char u = char(std::toupper(static_cast<unsigned char>(c)));
    if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == ' ' || u == '-' || u == '.' || u == '!') return u;
    return 0;
}

std::string Profile::shortId() const {
    std::string s = id.substr(0, 8);
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string Profile::serialize() const {
    std::ostringstream o;
    o << "# S3 RALLY player profile\n";
    o << "name=" << name << "\n";
    o << "id=" << id << "\n";
    o << "id_source=" << idSource << "\n";
    o << "created=" << created << "\n";
    o << "version=" << S3_VERSION << "\n";
    return o.str();
}

Profile Profile::parse(const std::string& text) {
    Profile p;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "name") {
            for (char c : v)
                if (char n = nameChar(c); n && p.name.size() < PROFILE_NAME_MAX) p.name += n;
        } else if (k == "id" && validUuid(v)) {
            p.id = v;
        } else if (k == "id_source") {
            p.idSource = v;
        } else if (k == "created") {
            p.created = v;
        }
    }
    if (p.id.empty()) p.name.clear();  // a profile without an ID isn't one
    return p;
}

IdFetcher::~IdFetcher() {
    if (worker_.joinable()) worker_.join();
}

void IdFetcher::start(bool useService) {
    if (started_) return;
    started_ = true;
    if (!useService) {
        id_ = localUuid();
        source_ = "local";
        done_ = true;
        return;
    }
#ifdef __EMSCRIPTEN__
    // Ask the service from the page; done() polls for the answer.
    web_ = true;
    EM_ASM({
        window.__gsid = null;
        fetch(UTF8ToString($0), {cache: 'no-store'})
            .then(function(r) { return r.ok ? r.json() : null; })
            .then(function(j) { window.__gsid = (j && j.uuid) ? String(j.uuid) : ''; })
            .catch(function() { window.__gsid = ''; });
    }, ID_SERVICE_URL);
#else
    worker_ = std::thread([this] {
        // curl verifies the server's certificate; nothing but the request itself is sent.
        std::string out;
        std::string cmd = std::string("curl -fsS --max-time 6 ") + ID_SERVICE_URL + " 2>/dev/null";
        if (FILE* p = popen(cmd.c_str(), "r")) {
            char buf[256];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, p)) > 0 && out.size() < 4096) out.append(buf, n);
            pclose(p);
        }
        const size_t at = out.find("\"uuid\"");
        const size_t q1 = at == std::string::npos ? at : out.find('"', out.find(':', at));
        std::string id = q1 == std::string::npos ? "" : out.substr(q1 + 1, 36);
        for (auto& c : id) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (validUuid(id)) {
            id_ = id;
            source_ = "urandom.ai";
        } else {
            id_ = localUuid();
            source_ = "local";
        }
        done_ = true;
    });
#endif
}

bool IdFetcher::done() {
#ifdef __EMSCRIPTEN__
    if (web_ && !done_) {
        const char* r = emscripten_run_script_string("window.__gsid === null ? 'pending' : window.__gsid");
        std::string s = r ? r : "";
        if (s != "pending" || ++webFrames_ > 60 * 8) {
            for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (validUuid(s)) {
                id_ = s;
                source_ = "urandom.ai";
            } else {
                id_ = localUuid();
                source_ = "local";
            }
            done_ = true;
        }
    }
#endif
    return done_;
}

}  // namespace rally
