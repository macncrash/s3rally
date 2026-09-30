// Several complete S3-16 consoles in one process, racing each other over
// the real network code (UDP on this machine). Used for the automated
// multiplayer test, a live four-way split screen, and filming matches.
// `champ` picks the cartridge: (3) RALLY (true) or S3 RUN.
#pragma once

// Headless: n consoles (2-4) race; checks everyone sees everyone and agrees on the result.
int versusTest(int stage, int players, bool discover, bool champ);

// A window split 2x2: the keyboard drives player 1, and any controller joins (plugged in at any
// time, START to join) in place of the next computer-driven car. Seats nobody has are driven by
// the autopilot. `fullscreen` for the demo (F11 toggles either way).
int runQuad(int players, int stage, bool champ, bool fullscreen = false);

// Film an all-autopilot n-player match to an MP4 (needs ffmpeg).
int recordQuad(int players, int stage, const char* mp4Path, bool champ);

// A still of the quad screen (labels and all) after some seconds of racing, as a PNG (needs ffmpeg).
int quadShot(int players, int seconds, const char* png);
