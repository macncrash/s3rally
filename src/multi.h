// Several complete consoles in one process, racing each other over the real
// network code (UDP on this machine). Used for the automated multiplayer test,
// the live four-way split screen (the party demo), and filming matches.
#pragma once
#include <string>

// Which game, on which machine: S3 RUN and (3) RALLY on the S3-16, and (3) RALLY 32/64.
enum class QuadCart { Run, Rally, Rally32, Rally64 };
QuadCart quadCartFor(const std::string& name);  // --cart run|rally|rally32|rally64 (default rally)

// Headless: n consoles (2-4) race; checks everyone sees everyone and agrees on the result.
int versusTest(int stage, int players, bool discover, QuadCart cart);

// A window split 2x2: the keyboard drives player 1, and any controller joins (plugged in at any
// time, START to join) in place of the next computer-driven car. Seats nobody has are driven by
// the autopilot. `demo`: fullscreen, cars 2 s apart (F11 toggles fullscreen either way).
int runQuad(int players, int stage, QuadCart cart, bool demo = false);

// Film an all-autopilot n-player match to an MP4 (needs ffmpeg).
int recordQuad(int players, int stage, const char* mp4Path, QuadCart cart);

// A still of the quad screen (labels and all) after some seconds of racing, as a PNG (needs ffmpeg).
int quadShot(int players, int seconds, const char* png, QuadCart cart);
