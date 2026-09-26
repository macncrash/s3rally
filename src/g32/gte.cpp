#include "gte.h"

#include <algorithm>

namespace g32 {

void Camera::update() {
    const float cy_ = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    fwd = {sy * cp, sp, cy_ * cp};
    right = normalize(cross({0, 1, 0}, fwd));
    up = cross(fwd, right);
    if (roll != 0) {
        const float cr = std::cos(roll), sr = std::sin(roll);
        const V3 r2 = right * cr + up * sr, u2 = up * cr - right * sr;
        right = r2;
        up = u2;
    }
}

V3 Camera::toView(V3 world) const {
    const V3 d = world - pos;
    return {dot(d, right), dot(d, up), dot(d, fwd)};
}

namespace {
struct CV {  // a vertex in view space with its attributes
    V3 p;
    float u, v, r, g, b;
};
CV lerp(const CV& a, const CV& b, float t) {
    return {a.p + (b.p - a.p) * t, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t, a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t};
}
}  // namespace

bool polygon(GPU& gpu, const Camera& cam, const WVtx* v, int n, int tex, Blend blend, float depthBias) {
    CV in[4], out[8];
    int m = 0;
    for (int i = 0; i < n && i < 4; i++) in[i] = {cam.toView(v[i].p), v[i].u, v[i].v, float(v[i].r), float(v[i].g), float(v[i].b)};
    // Sutherland-Hodgman against the near plane.
    for (int i = 0; i < n; i++) {
        const CV &a = in[i], &b = in[(i + 1) % n];
        const bool ia = a.p.z >= cam.nearZ, ib = b.p.z >= cam.nearZ;
        if (ia) out[m++] = a;
        if (ia != ib) out[m++] = lerp(a, b, (cam.nearZ - a.p.z) / (b.p.z - a.p.z));
    }
    if (m < 3) return false;
    Vtx s[8];
    float depth = 0, minZ = 1e9f;
    for (int i = 0; i < m; i++) {
        const CV& c = out[i];
        const float k = cam.focal / c.p.z;
        s[i].x = cam.cx + c.p.x * k;
        s[i].y = cam.cy - c.p.y * k;
        s[i].u = c.u, s[i].v = c.v;
        s[i].z = c.p.z;
        s[i].r = uint8_t(std::clamp(c.r, 0.0f, 255.0f)), s[i].g = uint8_t(std::clamp(c.g, 0.0f, 255.0f)), s[i].b = uint8_t(std::clamp(c.b, 0.0f, 255.0f));
        s[i].fog = std::clamp((c.p.z - cam.fogNear) / (cam.fogFar - cam.fogNear), 0.0f, 1.0f);
        depth += c.p.z;
        minZ = std::min(minZ, c.p.z);
    }
    depth /= float(m);
    if (minZ > cam.fogFar * 1.05f) return false;  // lost in the fog
    // Quick reject: entirely off one side of the screen.
    bool left = true, rightOut = true, top = true, bottom = true;
    for (int i = 0; i < m; i++) {
        left &= s[i].x < 0, rightOut &= s[i].x >= W, top &= s[i].y < 0, bottom &= s[i].y >= H;
    }
    if (left || rightOut || top || bottom) return false;
    for (int i = 1; i + 1 < m; i++) gpu.tri(s[0], s[i], s[i + 1], depth + depthBias, tex, blend);
    return true;
}

void billboard(GPU& gpu, const Camera& cam, V3 base, float w, float h, int tex, bool flip, uint8_t shade) {
    // Stand it up facing the camera: its width runs along the camera's right, flat on the ground.
    const V3 r = normalize(V3{cam.right.x, 0, cam.right.z}) * (w / 2);
    const float tw = float(gpu.texW(tex)), th = float(gpu.texH(tex));
    float u0 = 0, u1 = tw;
    if (flip) std::swap(u0, u1);
    WVtx q[4];
    q[0] = {base - r, u0, th, shade, shade, shade};
    q[1] = {base + r, u1, th, shade, shade, shade};
    q[2] = {base + r + V3{0, h, 0}, u1, 0, shade, shade, shade};
    q[3] = {base - r + V3{0, h, 0}, u0, 0, shade, shade, shade};
    polygon(gpu, cam, q, 4, tex);
}

}  // namespace g32
