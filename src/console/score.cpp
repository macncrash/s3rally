#include "score.h"

#include <atomic>
#include <chrono>
#include <random>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include "system.h"

#include "sha256.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <filesystem>
#include <unistd.h>
#endif

namespace gs {

// ---------------------------------------------------------------- a little JSON

namespace {

struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    double num = 0;
    bool b = false;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;
    const Json& operator[](const std::string& k) const {
        static const Json none;
        auto it = obj.find(k);
        return it == obj.end() ? none : it->second;
    }
};

struct Parser {
    const std::string& s;
    size_t p = 0;
    int depth = 0;
    void ws() {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) p++;
    }
    bool lit(const char* w) {
        const size_t n = std::char_traits<char>::length(w);
        if (s.compare(p, n, w) != 0) return false;
        p += n;
        return true;
    }
    bool string(std::string& out) {
        if (p >= s.size() || s[p] != '"') return false;
        p++;
        while (p < s.size() && s[p] != '"') {
            char c = s[p++];
            if (c == '\\' && p < s.size()) {
                const char e = s[p++];
                if (e == 'u') {  // our server only sends ASCII; keep a placeholder for anything else
                    p = std::min(s.size(), p + 4);
                    c = '?';
                } else {
                    c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
                }
            }
            out += c;
        }
        if (p >= s.size()) return false;
        p++;
        return true;
    }
    bool value(Json& v) {
        if (++depth > 16) return false;
        ws();
        bool ok = true;
        if (p >= s.size()) ok = false;
        else if (s[p] == '{') {
            v.kind = Json::Obj;
            p++;
            ws();
            if (p < s.size() && s[p] == '}') p++;
            else
                for (;;) {
                    std::string k;
                    ws();
                    Json item;
                    if (!string(k)) { ok = false; break; }
                    ws();
                    if (p >= s.size() || s[p++] != ':' || !value(item)) { ok = false; break; }
                    v.obj[k] = std::move(item);
                    ws();
                    if (p < s.size() && s[p] == ',') { p++; continue; }
                    if (p < s.size() && s[p] == '}') { p++; break; }
                    ok = false;
                    break;
                }
        } else if (s[p] == '[') {
            v.kind = Json::Arr;
            p++;
            ws();
            if (p < s.size() && s[p] == ']') p++;
            else
                for (;;) {
                    Json item;
                    if (!value(item)) { ok = false; break; }
                    v.arr.push_back(std::move(item));
                    ws();
                    if (p < s.size() && s[p] == ',') { p++; continue; }
                    if (p < s.size() && s[p] == ']') { p++; break; }
                    ok = false;
                    break;
                }
        } else if (s[p] == '"') {
            v.kind = Json::Str;
            ok = string(v.str);
        } else if (lit("true")) v.kind = Json::Bool, v.b = true;
        else if (lit("false")) v.kind = Json::Bool;
        else if (lit("null")) v.kind = Json::Null;
        else {
            const char* start = s.c_str() + p;
            char* end = nullptr;
            v.num = std::strtod(start, &end);
            if (end == start) ok = false;
            else v.kind = Json::Num, p += size_t(end - start);
        }
        depth--;
        return ok;
    }
};

bool parseJson(const std::string& text, Json& out) {
    Parser ps{text};
    return ps.value(out);
}

std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (c == '\n') o += "\\n";
        else if (static_cast<unsigned char>(c) < 32) o += ' ';
        else o += c;
    }
    return o + "\"";
}

bool urlSafe(const std::string& u) {
    for (char c : u)
        if (!std::isalnum(static_cast<unsigned char>(c)) && !std::strchr(":/._-[]", c)) return false;
    return true;
}

bool slugOk(const std::string& s) {
    if (s.empty() || s.size() > 24) return false;
    for (char c : s)
        if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

}  // namespace

std::string scoreServerUrl() {
    std::string u;
    if (const char* e = std::getenv("S3_SCORE_URL")) u = e;
#ifndef __EMSCRIPTEN__
    if (u.empty()) {
        if (const char* home = std::getenv("HOME"); home && home[0]) {
#if defined(__APPLE__)
            std::ifstream in(std::string(home) + "/Library/Application Support/s3/console/console.cfg");
#else
            const char* xdg = std::getenv("XDG_CONFIG_HOME");
            std::ifstream in((xdg && xdg[0] ? std::string(xdg) : std::string(home) + "/.config") + "/s3/console.cfg");
#endif
            std::string line;
            while (std::getline(in, line))
                if (line.rfind("score_url=", 0) == 0) u = line.substr(10);
        }
    }
#else
    // The page says where its server is (web/shell.html: window.S3_SCORE_URL, or ?score= for testing).
    if (u.empty()) u = emscripten_run_script_string("(typeof window !== 'undefined' && window.S3_SCORE_URL) || ''");
#endif
    while (!u.empty() && (u.back() == '/' || u.back() == '\r' || u.back() == ' ')) u.pop_back();
    const bool https = u.rfind("https://", 0) == 0;
    const bool local = u.rfind("http://127.0.0.1", 0) == 0 || u.rfind("http://localhost", 0) == 0 || u.rfind("http://[::1]", 0) == 0;
    if (u.empty() || u.size() > 200 || !urlSafe(u) || (!https && !local)) return {};
    return u;
}


// ---------------------------------------------------------------- calls in the background

#ifdef __EMSCRIPTEN__
// The browser's fetch: results wait in Module.s3net until the game polls for them.
EM_JS_DEPS(s3net, "$stringToNewUTF8,$UTF8ToString");
EM_JS(void, s3_fetch, (int id, const char* method, const char* url, const char* auth, const char* body), {
    Module.s3net = Module.s3net || {};
    const slot = {done: false, code: 0, text: ''};
    Module.s3net[id] = slot;
    const headers = {'Content-Type': 'application/json'};
    const a = UTF8ToString(auth);
    if (a) headers['X-S3-Auth'] = a;
    const m = UTF8ToString(method);
    const init = {method: m, headers: headers, credentials: 'omit', cache: 'no-store'};
    if (m !== 'GET') init.body = UTF8ToString(body);
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 20000);
    init.signal = ctl.signal;
    fetch(UTF8ToString(url), init)
        .then(r => r.text().then(t => { slot.code = r.status; slot.text = t.slice(0, 262144); }))
        .catch(() => { slot.code = 0; })
        .finally(() => { clearTimeout(timer); slot.done = true; });
});
EM_JS(int, s3_fetch_done, (int id), { const s = Module.s3net && Module.s3net[id]; return s && s.done ? 1 : 0; });
EM_JS(int, s3_fetch_code, (int id), { return Module.s3net[id].code; });
EM_JS(char*, s3_fetch_text, (int id), {
    const t = Module.s3net[id].text;
    delete Module.s3net[id];
    return stringToNewUTF8(t);
});
#endif

struct ScoreClient::Call {
    std::string kind;
    int code = 0;          // HTTP status, 0 if it never got there
    std::string body;
    std::atomic<bool> done{false};
    std::thread worker;
    int webId = 0;
};

namespace {
std::string randomHex(size_t bytes) {
    std::string raw(bytes, '\0');
#ifndef __EMSCRIPTEN__
    if (FILE* f = std::fopen("/dev/urandom", "rb")) {
        const size_t n = std::fread(raw.data(), 1, bytes, f);
        std::fclose(f);
        if (n == bytes) {
            static const char* X = "0123456789abcdef";
            std::string s;
            for (unsigned char c : raw) s += X[c >> 4], s += X[c & 15];
            return s;
        }
    }
#endif
    std::random_device rd;  // the browser's crypto.getRandomValues under Emscripten
    static const char* X = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < bytes; i++) {
        const unsigned v = rd() & 255;
        s += X[v >> 4], s += X[v & 15];
    }
    return s;
}

int64_t unixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace

ScoreClient::ScoreClient(System& sys, std::string game) : sys_(sys), game_(std::move(game)) {
#ifndef __EMSCRIPTEN__
    if (sys.headless && !std::getenv("S3_SCORE_URL")) return;  // tests only talk to a server they were given
#endif
    url_ = scoreServerUrl();
    std::istringstream in(sys_.loadBlob("score.txt"));
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "token" && v.size() == 64) token_ = v;
        else if (k == "id" && v.size() == 36) id_ = v;
        else if (k == "upload") upload_ = v == "always" ? Upload::Always : v == "never" ? Upload::Never : Upload::Ask;
        else if (k == "declined") declined_ = v == "1";
        else if (k.rfind("vote.", 0) == 0 && slugOk(k.substr(5))) votes_[k.substr(5)] = v == "1" ? 1 : v == "-1" ? -1 : 0;
    }
    if (id_.empty()) token_.clear();  // an old sign-up without its ID can't sign requests: join again
}

ScoreClient::~ScoreClient() {
    for (auto& c : calls_)
        if (c->worker.joinable()) c->worker.join();
}

void ScoreClient::save() const {
    std::ostringstream o;
    o << "token=" << token_ << "\nid=" << id_ << "\nupload=" << (upload_ == Upload::Always ? "always" : upload_ == Upload::Never ? "never" : "ask")
      << "\ndeclined=" << (declined_ ? 1 : 0) << "\n";
    for (auto& [g, v] : votes_) o << "vote." << g << "=" << v << "\n";
    sys_.saveBlob("score.txt", o.str());
}

void ScoreClient::setUpload(Upload u) {
    upload_ = u;
    save();
}

void ScoreClient::decline() {
    declined_ = true;
    save();
}

bool ScoreClient::busy() const {
    for (auto& c : calls_)
        if (!c->done) return true;
    return false;
}

std::string ScoreClient::signature(const std::string& method, const std::string& path, const std::string& body) const {
    // X-S3-Auth: id:ms:nonce:HMAC-SHA256(SHA-256(token), METHOD \n PATH \n ms \n nonce \n hex(SHA-256(body)))
    const std::string ms = std::to_string(unixMs()), nonce = randomHex(16);
    const Digest k = sha256(token_);
    const std::string msg = method + "\n" + path + "\n" + ms + "\n" + nonce + "\n" + toHex(sha256(body));
    return id_ + ":" + ms + ":" + nonce + ":" + toHex(hmacSha256(std::string(k.begin(), k.end()), msg));
}

void ScoreClient::send(const std::string& kind, const std::string& method, const std::string& path, const std::string& body, bool auth) {
    if (!enabled() || (auth && !registered())) return;
    auto c = std::make_unique<Call>();
    Call* call = c.get();
    call->kind = kind;
    const std::string url = url_ + path;
    const std::string sig = auth ? signature(method, path, body) : std::string();
#ifdef __EMSCRIPTEN__
    static int nextId = 1;
    call->webId = nextId++;
    s3_fetch(call->webId, method.c_str(), url.c_str(), sig.c_str(), body.c_str());
#else
    call->worker = std::thread([call, url, method, body, sig] {
        // Headers and body go through private temp files, never the command line:
        // nothing here reaches a shell unquoted.
        namespace fs = std::filesystem;
        std::string dir = fs::temp_directory_path().string();
        auto temp = [&](const std::string& content) {
            std::string tpl = dir + "/s3score-XXXXXX";
            std::vector<char> name(tpl.begin(), tpl.end());
            name.push_back(0);
            const int fd = mkstemp(name.data());  // created 0600
            if (fd < 0) return std::string();
            size_t off = 0;
            while (off < content.size()) {
                const ssize_t n = write(fd, content.data() + off, content.size() - off);
                if (n <= 0) break;
                off += size_t(n);
            }
            close(fd);
            std::string path(name.data());
            if (path.find('\'') != std::string::npos) {  // goes into the curl command in single quotes
                std::remove(path.c_str());
                return std::string();
            }
            return path;
        };
        std::string hdr = "Content-Type: application/json\n";
        if (!sig.empty()) hdr += "X-S3-Auth: " + sig + "\n";
        const std::string hf = temp(hdr), bf = body.empty() ? std::string() : temp(body), of = temp("");
        if (hf.empty() || of.empty() || (!body.empty() && bf.empty())) {
            call->done = true;
            return;
        }
        const bool https = url.rfind("https://", 0) == 0;
        std::string cmd = "curl -sS --max-time 20 --proto " + std::string(https ? "=https" : "=http") + " -X " + method + " -H @'" + hf + "'";
        if (!bf.empty()) cmd += " --data-binary @'" + bf + "'";
        cmd += " -o '" + of + "' -w '%{http_code}' '" + url + "' 2>/dev/null";
        std::string code;
        if (FILE* p = popen(cmd.c_str(), "r")) {
            char buf[64];
            while (std::fgets(buf, sizeof buf, p)) code += buf;
            pclose(p);
        }
        std::ifstream in(of, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        call->body = ss.str().substr(0, 256 * 1024);
        call->code = std::atoi(code.c_str());
        for (const std::string& f : {hf, bf, of})
            if (!f.empty()) std::remove(f.c_str());
        call->done = true;
    });
#endif
    calls_.push_back(std::move(c));
}

void ScoreClient::poll() {
#ifdef __EMSCRIPTEN__
    for (auto& c : calls_)
        if (!c->done && s3_fetch_done(c->webId)) {
            c->code = s3_fetch_code(c->webId);
            char* t = s3_fetch_text(c->webId);
            c->body = t ? t : "";
            std::free(t);
            c->done = true;
        }
#endif
    for (size_t i = 0; i < calls_.size();) {
        Call& c = *calls_[i];
        if (!c.done) {
            i++;
            continue;
        }
        if (c.worker.joinable()) c.worker.join();
        finished(c);
        calls_.erase(calls_.begin() + long(i));
    }
}

void ScoreClient::wait() {
#ifndef __EMSCRIPTEN__
    for (auto& c : calls_)
        if (c->worker.joinable()) c->worker.join();
#endif
    poll();
}

void ScoreClient::finished(Call& c) {
    Json j;
    const bool parsed = parseJson(c.body, j);
    if (c.code == 0) {
        error = "OFFLINE";
        if (c.kind == "submit") lastStatus = "offline";
        if (c.kind == "feedback") feedbackStatus = "offline";
        return;
    }
    if (c.code == 429) error = "SERVER BUSY";
    else if (c.code >= 500) error = "SERVER ERROR";
    if (c.kind == "register") {
        if (c.code == 201 && parsed && j["token"].str.size() == 64) {
            token_ = j["token"].str;
            id_ = pendingId_;
            declined_ = false;
            error.clear();
            save();
        } else if (c.code == 409) {
            error = "ID ALREADY REGISTERED";
        } else if (error.empty()) {
            error = "COULD NOT REGISTER";
        }
    } else if (c.kind == "ticket") {
        if (c.code == 200 && parsed) ticket_ = j["ticket"].str;
    } else if (c.kind == "submit") {
        lastRank = lastOf = 0;
        if (c.code == 200 && parsed) {
            lastStatus = j["status"].str;
            lastReason = j["reason"].str.substr(0, 120);
            lastRank = int(j["rank"].num);
            lastOf = int(j["of"].num);
        } else {
            lastStatus = c.code == 409 ? "ticket" : "error";
        }
    } else if (c.kind == "board") {
        if (c.code == 200 && parsed) {
            Board b;
            b.game = j["game"].str;
            b.stage = int(j["stage"].num);
            b.total = int(j["total"].num);
            auto row = [](const Json& r) {
                BoardRow x;
                x.rank = int(r["rank"].num);
                x.name = r["name"].str.substr(0, 12);
                x.shortId = r["short_id"].str.substr(0, 8);
                x.score = r["score"].num;
                x.you = r["you"].b;
                return x;
            };
            for (const Json& r : j["top"].arr) b.top.push_back(row(r));
            if (j["you"].kind == Json::Obj) b.you = row(j["you"]);
            b.loaded = true;
            board = b;
        }
    } else if (c.kind == "stats") {
        if (c.code == 200) loadStats(c.body);
    } else if (c.kind == "feedback") {
        feedbackStatus = c.code == 201 ? "sent" : c.code == 429 ? "busy" : "error";
    } else if (c.kind == "forget") {
        if (c.code == 200) {
            token_.clear();
            id_.clear();
            save();
        }
    }
    if (c.code == 401 && c.kind != "register" && c.kind != "feedback") {  // the server doesn't know us any more
        token_.clear();
        id_.clear();
        save();
    }
}

void ScoreClient::registerPlayer(const std::string& id, const std::string& name) {
    pendingId_ = id;
    send("register", "POST", "/v1/players", "{\"id\":" + jsonStr(id) + ",\"name\":" + jsonStr(name) + "}", false);
}

void ScoreClient::startRun(int stage) {
    ticket_.clear();
    ticketStage_ = stage;
    if (!slugOk(game_)) return;
    send("ticket", "POST", "/v1/tickets", "{\"game\":" + jsonStr(game_) + ",\"stage\":" + std::to_string(stage) + "}", true);
}

void ScoreClient::submit(int stage, double score, const std::string& replay, const std::string& build) {
    lastStatus.clear();
    if (!slugOk(game_)) return;
    char num[32];
    std::snprintf(num, sizeof num, "%.4f", score);
    // With a ticket from the start of the run; without one (the player joined afterwards) the
    // server still checks the replay, but a person has to accept it.
    std::string body = ticket_.empty() || stage != ticketStage_ ? "{\"game\":" + jsonStr(game_) + ",\"stage\":" + std::to_string(stage)
                                                                : "{\"ticket\":" + jsonStr(ticket_);
    body += std::string(",\"score\":") + num + ",\"build\":" + jsonStr(build);
    if (!replay.empty()) body += ",\"replay\":\"" + replay + "\"";  // base64: nothing to escape
    body += "}";
    ticket_.clear();
    send("submit", "POST", "/v1/runs", body, true);
}

void ScoreClient::fetchBoard(int stage) {
    board = Board();
    if (!slugOk(game_)) return;
    send("board", "GET", "/v1/leaderboard/" + game_ + "/" + std::to_string(stage), "", registered());
}

void ScoreClient::fetchStats() { send("stats", "GET", "/v1/stats", "", false); }

void ScoreClient::play(bool finish, double seconds) {
    char num[32];
    std::snprintf(num, sizeof num, "%.1f", seconds);
    send("play", "POST", "/v1/plays", "{\"game\":" + jsonStr(game_) + ",\"event\":\"" + (finish ? "finish" : "start") + "\",\"seconds\":" + num + "}", true);
}

void ScoreClient::rate(int thumb) { rateGame(game_, thumb); }

void ScoreClient::rateGame(const std::string& game, int thumb) {
    if (!slugOk(game)) return;
    votes_[game] = thumb > 0 ? 1 : -1;
    save();
    send("rate", "PUT", "/v1/ratings/" + game, std::string("{\"thumb\":") + (thumb > 0 ? "1" : "-1") + "}", true);
}

int ScoreClient::vote(const std::string& game) const {
    auto it = votes_.find(game);
    return it == votes_.end() ? 0 : it->second;
}

void ScoreClient::feedback(const std::string& game, const std::string& text, const std::string& build) {
    feedbackStatus = "sending";
    if (!slugOk(game)) return;
    send("feedback", "POST", "/v1/feedback", "{\"game\":" + jsonStr(game) + ",\"text\":" + jsonStr(text.substr(0, 2000)) + ",\"build\":" + jsonStr(build) + "}",
         registered());
    if (!enabled()) feedbackStatus = "offline";
}

void ScoreClient::forget() { send("forget", "DELETE", "/v1/players/me", "", true); }

}  // namespace gs

namespace gs {

bool ScoreClient::loadStats(const std::string& text) {
    Json j;
    if (text.size() > 1u << 20 || !parseJson(text, j) || j["games"].kind != Json::Arr) return false;
    std::vector<GameStat> out;
    for (const Json& g : j["games"].arr) {
        GameStat s;
        s.game = g["game"].str.substr(0, 24);
        s.title = g["title"].str.substr(0, 40);
        s.plays7 = std::max(0, int(g["plays_7d"].num));
        s.plays30 = std::max(0, int(g["plays_30d"].num));
        s.players30 = std::max(0, int(g["players_30d"].num));
        s.up = std::max(0, int(g["up"].num));
        s.down = std::max(0, int(g["down"].num));
        if (slugOk(s.game)) out.push_back(s);
    }
    stats = out;
    statsText = text;
    return true;
}

std::string newPlayerId() {
    std::string h = randomHex(16);
    h[12] = '4';                                        // version 4
    h[16] = "89ab"[std::strtol(h.substr(16, 1).c_str(), nullptr, 16) & 3];  // variant 10xx
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20, 12);
}

}  // namespace gs
