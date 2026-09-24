// S3 RALLY version. Bump S3_VERSION for each release; the build
// stamps S3_BUILD with the git commit so every binary is traceable.
#pragma once

#define S3_VERSION "1.5.0"
#ifndef S3_BUILD
#define S3_BUILD "dev"
#endif

// e.g. "V1.4.0 (a1b2c3d)"
#define S3_VERSION_STRING "V" S3_VERSION " (" S3_BUILD ")"
