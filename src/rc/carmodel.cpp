#include "carmodel.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rc {

namespace {

struct V3 {
    float x, y, z;
};
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-9f ? a * (1 / l) : V3{0, 1, 0};
}

// Car palette roles: 1 black, 2 dark grey, 3 body, 4 body shade, 5-6 stripes,
// 7-8 glass, 9-10 lights, 11 metal, 12 tread, 13 body highlight, 14 white, 15 decal.
enum Mat { BODY, GLASS, BLACK, DARK, TREAD, HUB, STRIPE1, STRIPE2, RED, RED2, WHITE, DECAL };

struct Face {
    std::vector<V3> p;
    Mat m;
    bool twoSided = false;
};

struct Model {
    std::vector<Face> faces;
    // Turn every face added since `from` to face away from `centre` (convex parts).
    void orient(size_t from, V3 centre) {
        for (size_t i = from; i < faces.size(); i++) {
            Face& f = faces[i];
            V3 mid{0, 0, 0};
            for (const V3& p : f.p) mid = V3{mid.x + p.x, mid.y + p.y, mid.z + p.z};
            mid = mid * (1.0f / float(f.p.size()));
            const V3 n = cross(f.p[1] - f.p[0], f.p[2] - f.p[0]);
            if (dot(n, mid - centre) < 0) std::reverse(f.p.begin(), f.p.end());
        }
    }
    void quad(V3 a, V3 b, V3 c, V3 d, Mat m, bool two = false) {
        faces.push_back({{a, b, c, d}, m, two});
        orient(faces.size() - 1, {0, 0.6f, 0});
    }
    // Box from 8 corners: bottom (rear-left, rear-right, front-right, front-left) then top in the same order.
    void hexa(const V3 (&k)[8], Mat top, Mat side, Mat rear, Mat front, Mat bottom) {
        const size_t from = faces.size();
        V3 c{0, 0, 0};
        for (const V3& p : k) c = V3{c.x + p.x, c.y + p.y, c.z + p.z};
        quad(k[4], k[5], k[6], k[7], top);            // top (outward normal up)
        quad(k[3], k[2], k[1], k[0], bottom);         // bottom
        quad(k[0], k[4], k[7], k[3], side);           // left (-x)
        quad(k[2], k[6], k[5], k[1], side);           // right (+x)
        quad(k[1], k[5], k[4], k[0], rear);           // rear (-z)
        quad(k[3], k[7], k[6], k[2], front);          // front (+z)
        orient(from, c * 0.125f);
    }
    void box(float x0, float x1, float y0, float y1, float z0, float z1, Mat all) {
        const V3 k[8] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
        hexa(k, all, all, all, all, all);
    }
    void wheel(float cx, float cy, float cz, float rad, float halfW) {
        const size_t from = faces.size();
        const int n = 10;
        std::vector<V3> in, out;
        for (int i = 0; i < n; i++) {
            const float a = i * 6.2831853f / n;
            in.push_back({cx - halfW, cy + std::sin(a) * rad, cz + std::cos(a) * rad});
            out.push_back({cx + halfW, cy + std::sin(a) * rad, cz + std::cos(a) * rad});
        }
        for (int i = 0; i < n; i++) {
            const int j = (i + 1) % n;
            quad(in[size_t(i)], in[size_t(j)], out[size_t(j)], out[size_t(i)], TREAD);
        }
        Face capOut{{}, HUB}, capIn{{}, HUB};
        for (int i = n - 1; i >= 0; i--) capOut.p.push_back(out[size_t(i)]);
        for (int i = 0; i < n; i++) capIn.p.push_back(in[size_t(i)]);
        faces.push_back(capOut);
        faces.push_back(capIn);
        orient(from, {cx, cy, cz});
    }
};

const Model& model() {
    static Model m = [] {
        Model md;
        // Main body: low and wide, sides tucking in slightly towards the waist.
        {
            const float zr = -2.02f, zf = 1.20f, yb = 0.26f, yt = 0.74f;
            const V3 k[8] = {{-0.86f, yb, zr}, {0.86f, yb, zr}, {0.86f, yb, zf}, {-0.86f, yb, zf},
                             {-0.82f, yt, zr}, {0.82f, yt, zr}, {0.82f, 0.72f, zf}, {-0.82f, 0.72f, zf}};
            md.hexa(k, BODY, BODY, BODY, BODY, DARK);
        }
        // Nose: the bonnet slopes down to the front bumper.
        {
            const float z0 = 1.20f, z1 = 2.02f, yb = 0.26f;
            const V3 k[8] = {{-0.86f, yb, z0}, {0.86f, yb, z0}, {0.82f, 0.30f, z1}, {-0.82f, 0.30f, z1},
                             {-0.82f, 0.72f, z0}, {0.82f, 0.72f, z0}, {0.78f, 0.60f, z1}, {-0.78f, 0.60f, z1}};
            md.hexa(k, BODY, BODY, BODY, BODY, DARK);
        }
        // Cabin: small and raked, roof in body colour.
        {
            const V3 k[8] = {{-0.78f, 0.74f, -1.30f}, {0.78f, 0.74f, -1.30f}, {0.78f, 0.72f, 1.00f}, {-0.78f, 0.72f, 1.00f},
                             {-0.60f, 1.24f, -0.78f}, {0.60f, 1.24f, -0.78f}, {0.60f, 1.24f, 0.20f}, {-0.60f, 1.24f, 0.20f}};
            md.hexa(k, BODY, GLASS, GLASS, GLASS, DARK);
        }
        // Flared wheel arches.
        for (float sx : {-1.0f, 1.0f})
            for (float z : {-1.28f, 1.28f}) {
                const float x0 = sx < 0 ? -0.95f : 0.78f, x1 = sx < 0 ? -0.78f : 0.95f;
                md.box(x0, x1, 0.30f, 0.62f, z - 0.46f, z + 0.46f, BODY);
            }
        // A big rear wing on struts, with end plates.
        md.box(-0.92f, 0.92f, 1.04f, 1.10f, -2.04f, -1.70f, BLACK);
        md.quad({-0.92f, 1.105f, -2.04f}, {0.92f, 1.105f, -2.04f}, {0.92f, 1.105f, -1.70f}, {-0.92f, 1.105f, -1.70f}, STRIPE1);
        md.box(-0.94f, -0.90f, 0.92f, 1.16f, -2.06f, -1.66f, STRIPE2);
        md.box(0.90f, 0.94f, 0.92f, 1.16f, -2.06f, -1.66f, STRIPE2);
        md.box(-0.58f, -0.50f, 0.74f, 1.04f, -1.96f, -1.84f, BLACK);
        md.box(0.50f, 0.58f, 0.74f, 1.04f, -1.96f, -1.84f, BLACK);
        // Roof vent and the light pod on the bonnet.
        md.box(-0.16f, 0.16f, 1.24f, 1.30f, -0.30f, 0.02f, DARK);
        md.box(-0.62f, 0.62f, 0.60f, 0.76f, 1.88f, 1.98f, BLACK);
        for (int i = 0; i < 4; i++) {
            const float x = -0.52f + i * 0.35f;
            md.quad({x - 0.12f, 0.62f, 1.985f}, {x + 0.12f, 0.62f, 1.985f}, {x + 0.12f, 0.74f, 1.985f}, {x - 0.12f, 0.74f, 1.985f}, WHITE);
        }
        // Rear end: lights, plate, bumper, exhaust.
        const float zr = -2.025f;
        auto rear = [&](float x0, float x1, float y0, float y1, Mat mt) {
            md.quad({x1, y0, zr}, {x1, y1, zr}, {x0, y1, zr}, {x0, y0, zr}, mt);
        };
        rear(-0.82f, -0.50f, 0.52f, 0.66f, RED);
        rear(0.50f, 0.82f, 0.52f, 0.66f, RED);
        rear(-0.58f, -0.50f, 0.54f, 0.64f, RED2);
        rear(0.50f, 0.58f, 0.54f, 0.64f, RED2);
        rear(-0.26f, 0.26f, 0.36f, 0.48f, WHITE);
        rear(-0.22f, 0.22f, 0.39f, 0.45f, BLACK);
        rear(-0.86f, 0.86f, 0.26f, 0.33f, DARK);
        md.box(0.40f, 0.55f, 0.22f, 0.30f, -2.12f, -1.98f, BLACK);
        // Front: headlights and grille.
        const float zf = 2.025f;
        auto front = [&](float x0, float x1, float y0, float y1, Mat mt) {
            md.quad({x0, y0, zf}, {x0, y1, zf}, {x1, y1, zf}, {x1, y0, zf}, mt);
        };
        front(-0.74f, -0.42f, 0.42f, 0.54f, WHITE);
        front(0.42f, 0.74f, 0.42f, 0.54f, WHITE);
        front(-0.38f, 0.38f, 0.36f, 0.50f, BLACK);
        // Livery stripes down both sides, door number panels.
        for (float sx : {-1.0f, 1.0f}) {
            const float x = sx * 0.955f;
            auto side = [&](float z0, float z1, float y0, float y1, Mat mt) {
                if (sx > 0) md.quad({x, y0, z0}, {x, y0, z1}, {x, y1, z1}, {x, y1, z0}, mt);
                else md.quad({x, y0, z1}, {x, y0, z0}, {x, y1, z0}, {x, y1, z1}, mt);
            };
            side(-0.80f, 0.80f, 0.52f, 0.60f, STRIPE1);
            side(-0.80f, 0.80f, 0.46f, 0.51f, STRIPE2);
            side(-0.35f, 0.45f, 0.60f, 0.72f, DECAL);
            side(-0.80f, 0.80f, 0.28f, 0.34f, DARK);
            // Mud flaps behind each wheel.
            md.quad({sx * 0.66f, 0.06f, -1.66f}, {sx * 0.92f, 0.06f, -1.66f}, {sx * 0.92f, 0.36f, -1.66f}, {sx * 0.66f, 0.36f, -1.66f}, STRIPE1, true);
            md.quad({sx * 0.66f, 0.06f, 0.96f}, {sx * 0.92f, 0.06f, 0.96f}, {sx * 0.92f, 0.36f, 0.96f}, {sx * 0.66f, 0.36f, 0.96f}, BLACK, true);
        }
        // Bonnet stripe.
        md.quad({-0.14f, 0.726f, 1.20f}, {0.14f, 0.726f, 1.20f}, {0.14f, 0.606f, 2.02f}, {-0.14f, 0.606f, 2.02f}, STRIPE1);
        // Wheels.
        for (float sx : {-1.0f, 1.0f})
            for (float z : {-1.28f, 1.28f}) md.wheel(sx * 0.82f, 0.31f, z, 0.31f, 0.12f);
        return md;
    }();
    return m;
}

int shade(Mat m, float lit) {
    switch (m) {
        case BODY: return lit > 0.62f ? 13 : lit > 0.18f ? 3 : 4;
        case GLASS: return lit > 0.72f ? 8 : 7;
        case BLACK: return 1;
        case DARK: return lit > 0.5f ? 11 : 2;
        case TREAD: return lit > 0.4f ? 12 : 1;
        case HUB: return lit > 0.3f ? 11 : 2;
        case STRIPE1: return 5;
        case STRIPE2: return 6;
        case RED: return 9;
        case RED2: return 10;
        case WHITE: return 14;
        default: return 15;
    }
}

}  // namespace

gs::Bitmap renderCar(float yaw, float pitch, float roll, float elevation) {
    const Model& md = model();
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll), ce = std::cos(elevation), se = std::sin(elevation);
    // Roll happens about the car's centre of mass (rolled cars tumble about their middle).
    const float rollY = 0.7f;
    auto xf = [&](V3 p) {
        // roll about Z (positive = right side goes down)
        p.y -= rollY;
        V3 q{p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z};
        q.y += rollY;
        // pitch about X (nose up)
        V3 w{q.x, q.y * cp + q.z * sp, -q.y * sp + q.z * cp};
        // yaw about Y (nose to the right)
        return V3{w.x * cy + w.z * sy, w.y, -w.x * sy + w.z * cy};
    };
    const V3 toCam{0, se, -ce};  // towards the viewer (behind and above)
    const V3 light = norm({-0.35f, 0.85f, -0.4f});

    struct Drawn {
        std::vector<gs::Pt> pts;
        float depth;
        int col;
    };
    std::vector<Drawn> list;
    for (const Face& f : md.faces) {
        std::vector<V3> p;
        for (const V3& v : f.p) p.push_back(xf(v));
        V3 n = norm(cross(p[1] - p[0], p[2] - p[0]));
        float facing = dot(n, toCam);
        if (facing <= 0 && !f.twoSided) continue;
        if (facing < 0) n = n * -1;
        const float lit = std::max(0.0f, dot(n, light));
        Drawn d;
        d.depth = 0;
        for (const V3& v : p) {
            const float sx = v.x;
            const float up = v.y * ce + v.z * se;
            d.depth += -v.y * se + v.z * ce;
            d.pts.push_back({CAR_BW / 2.0f + sx * CAR_PPM, CAR_GROUND - up * CAR_PPM});
        }
        d.depth /= float(p.size());
        d.col = shade(f.m, lit);
        list.push_back(std::move(d));
    }
    std::sort(list.begin(), list.end(), [](const Drawn& a, const Drawn& b) { return a.depth > b.depth; });
    gs::Bitmap b(CAR_BW, CAR_BH);
    for (const Drawn& d : list) b.poly(d.pts, d.col);
    b.outline(1);
    return b;
}

}  // namespace rc

namespace rc {

const std::vector<CarPoly>& carPolys() {
    static const std::vector<CarPoly> polys = [] {
        std::vector<CarPoly> out;
        for (const Face& f : model().faces) {
            CarPoly p;
            for (const V3& v : f.p) p.xyz.insert(p.xyz.end(), {v.x, v.y, v.z});
            p.mat = int(f.m);
            p.twoSided = f.twoSided;
            out.push_back(std::move(p));
        }
        return out;
    }();
    return polys;
}

int carColorIndex(int mat, float lit) { return shade(Mat(mat), lit); }

}  // namespace rc
