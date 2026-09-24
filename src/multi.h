// Several complete S3-16 consoles in one process, racing each other over
// the real network code (UDP on this machine). Used for the automated
// head-to-head test, a live four-way split screen, and filming matches.
#pragma once

// Headless: n consoles (2-4) race; checks everyone sees everyone and agrees on the result.
int versusTest(int stage, int players, bool discover);

// A window split 2x2: keyboard and the first pad drive player 1, further pads
// players 2-4, and seats without a pad are driven by the autopilot.
int runQuad(int players, int stage);

// Film an all-autopilot n-player match to an MP4 (needs ffmpeg).
int recordQuad(int players, int stage, const char* mp4Path);
