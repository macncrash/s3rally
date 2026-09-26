// The score client in a browser: join, a signed ticket, a thumbs up, feedback
// and the stats, against a local server (tools/web-score-test.sh). Each step
// writes a line to the page; the title says when it's done.
#include <emscripten.h>

#include <cstdio>
#include <string>

#include "console/score.h"
#include "console/sha256.h"
#include "console/system.h"

namespace {
gs::System* sys;
gs::ScoreClient* sc;
int step = 0, fails = 0;

void say(bool ok, const std::string& what) {
    fails += !ok;
    const std::string line = std::string(ok ? "ok   " : "FAIL ") + what;
    std::printf("%s\n", line.c_str());
    EM_ASM({ document.getElementById('log').textContent += UTF8ToString($0) + '\n'; }, line.c_str());
}

void tick() {
    sc->poll();
    if (sc->busy()) return;
    switch (step++) {
        case 0:
            say(gs::toHex(gs::sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 in WebAssembly");
            say(sc->enabled(), "the page names a server");
            sc->registerPlayer(gs::newPlayerId(), "WEB TESTER");
            break;
        case 1:
            say(sc->registered(), "joined from the browser");
            sc->startRun(0);
            break;
        case 2:
            say(sc->haveTicket(), "a signed request: got a start ticket");
            sc->rateGame("rally", 1);
            sc->feedback("rally", "Sent from the browser build.", "web-test");
            break;
        case 3:
            say(sc->feedbackStatus == "sent", "feedback sent (" + sc->feedbackStatus + ")");
            sc->fetchStats();
            break;
        case 4: {
            bool seen = false;
            for (const gs::GameStat& g : sc->stats) seen |= g.game == "rally" && g.up == 1;
            say(seen, "stats show the thumbs up");
            const std::string end = fails ? "WEB SCORE TEST FAILED" : "WEB SCORE TEST OK";
            say(!fails, end);
            EM_ASM({ document.title = UTF8ToString($0); }, end.c_str());
            emscripten_cancel_main_loop();
            break;
        }
    }
}
}  // namespace

int main() {
    sys = new gs::System(true);
    sc = new gs::ScoreClient(*sys, "rally");
    emscripten_set_main_loop(tick, 30, 0);
    return 0;
}
