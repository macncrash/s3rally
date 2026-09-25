// S3-32 geometry unit: vectors, the camera, perspective, near-plane
// clipping, and depth-cued polygons handed to the GPU's ordering table.
// World space is metres: x right, y up, z forward.
#pragma once
#include <cmath>

#include "gpu.h"

namespace g32 {

struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-9f ? a * (1 / l) : V3{0, 1, 0};
}

// A world-space vertex with its texture coordinates and shade.
struct WVtx {
    V3 p;
    float u = 0, v = 0;
    uint8_t r = 128, g = 128, b = 128;
};

struct Camera {
    V3 pos;
    float yaw = 0, pitch = 0, roll = 0;  // radians: yaw turns right, pitch looks up
    float focal = 250;                   // pixels
    float nearZ = 0.25f;                 // metres
    float fogNear = 60, fogFar = 300;    // depth cue, metres
    float cx = W / 2.0f, cy = H / 2.0f;
    void update();                        // after moving: rebuilds the view matrix
    V3 toView(V3 world) const;
    V3 right, up, fwd;                    // world axes of the view
};

// Transform, clip against the near plane, project, depth-cue and submit a
// convex polygon (3 or 4 vertices). Returns false if nothing was drawn.
bool polygon(GPU& gpu, const Camera& cam, const WVtx* v, int n, int tex = -1, Blend blend = OPAQUE, float depthBias = 0);

// A camera-facing billboard standing on `base` (w x h metres), for trees, signs and people.
void billboard(GPU& gpu, const Camera& cam, V3 base, float w, float h, int tex, bool flip = false, uint8_t shade = 128);

}  // namespace g32
