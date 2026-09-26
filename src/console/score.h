// S3 online scoreboard client: the console's side of the score server.
//
// Optional and off unless a server is set (S3_SCORE_URL, or the host's
// console.cfg). A player registers once with the ID their game made and a
// name; the server hands back a secret token, kept in score.txt. After that a
// game can start a run (a server ticket), hand in its score with a replay,
// fetch a leaderboard, report plays and rate the game.
//
// Every call runs in the background (curl on the desktop, which checks the
// server's certificate) and is collected with poll() once a frame, so a slow
// network never stalls the game.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gs {

class System;

struct BoardRow {
    int rank = 0;
    std::string name, shortId;
    double score = 0;
    bool you = false;
};

struct GameStat {  // one game's popularity, from the server's public stats
    std::string game, title;
    int plays7 = 0, plays30 = 0, players30 = 0, up = 0, down = 0;
};

struct Board {
    std::string game;
    int stage = 0, total = 0;
    std::vector<BoardRow> top;
    BoardRow you;       // rank 0 if not on the board
    bool loaded = false;
};

class ScoreClient {
public:
    ScoreClient(System& sys, std::string game);
    ~ScoreClient();

    bool enabled() const { return !url_.empty(); }
    bool registered() const { return !token_.empty() && !id_.empty(); }
    bool busy() const;
    // What the player chose: ask each time, always upload, or never.
    enum class Upload { Ask, Always, Never };
    Upload upload() const { return upload_; }
    void setUpload(Upload u);
    bool declined() const { return declined_; }  // said no to the scoreboard
    void decline();

    void registerPlayer(const std::string& id, const std::string& name);
    void startRun(int stage);                                            // asks for a ticket
    bool haveTicket() const { return !ticket_.empty(); }
    void submit(int stage, double score, const std::string& replay, const std::string& build);
    void fetchBoard(int stage);
    void play(bool finish, double seconds = 0);
    void rate(int thumb);                                                // +1 or -1, this game
    void rateGame(const std::string& game, int thumb);                  // from the launcher
    int vote(const std::string& game) const;                             // how we voted: +1, -1 or 0
    void fetchStats();                                                   // every game's plays and votes
    bool loadStats(const std::string& text);                            // from a saved copy (offline)
    void feedback(const std::string& game, const std::string& text, const std::string& build);  // anyone may
    void forget();                                                       // leave: the server deletes everything of ours

    void poll();  // once a frame

    // Results of the last calls.
    std::string lastStatus;   // "accepted", "review", "rejected", or an error for the player
    std::string lastReason;   // the server's explanation
    int lastRank = 0, lastOf = 0;
    Board board;
    std::string error;        // last failure, for the screen ("OFFLINE", "SERVER BUSY")
    std::vector<GameStat> stats;  // after fetchStats()
    std::string statsText;        // the reply as it came, to keep for offline
    std::string feedbackStatus;   // "sending", "sent", "busy", "offline", "error"

    // Headless tests: wait for everything in flight.
    void wait();

private:
    struct Call;
    void send(const std::string& kind, const std::string& method, const std::string& path, const std::string& body, bool auth);
    void finished(Call& c);
    void save() const;
    std::string signature(const std::string& method, const std::string& path, const std::string& body) const;

    System& sys_;
    std::string game_, url_, token_, id_, ticket_, pendingId_;
    std::map<std::string, int> votes_;
    int ticketStage_ = -1;
    Upload upload_ = Upload::Ask;
    bool declined_ = false;
    std::vector<std::unique_ptr<Call>> calls_;
};

// A new random player ID (UUID v4), for a console that has none yet.
std::string newPlayerId();

// The server's address: S3_SCORE_URL, or score_url= in the console settings.
// Only https://, or http:// to this machine, is accepted.
std::string scoreServerUrl();

}  // namespace gs
