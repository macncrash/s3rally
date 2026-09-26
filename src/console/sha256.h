// SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104), for signing requests to the
// score server. Small and dependency-free so it runs the same on the desktop
// and in the browser build.
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace gs {

using Digest = std::array<uint8_t, 32>;

Digest sha256(const std::string& data);
Digest hmacSha256(const std::string& key, const std::string& data);
std::string toHex(const Digest& d);

}  // namespace gs
