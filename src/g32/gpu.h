// S3-32 video hardware: a polygon GPU in the style of the mid-90s machines.
//
// 320x240, 15-bit colour. The cartridge submits triangles, quads and sprites
// with a depth; the GPU keeps them in an ordering table and draws back to front
// (there is no depth buffer). Textures are 16-bit texels in 2 MB of texture RAM,
// mapped affinely (so they swim a little, as they did), modulated by a
// Gouraud-shaded colour (128 = unchanged), fogged towards a fog colour, and
// blended opaque, half-transparent or additive. Shaded pixels are dithered
// down to 15 bits.
#pragma once
#include <cstdint>
#include <vector>

namespace g32 {

constexpr int W = 320, H = 240;
constexpr int TEX_RAM = 2 << 20;  // bytes

// Texel: bit 15 set = visible, then 5-5-5 RGB. 0 is transparent.
inline uint16_t texel(int r5, int g5, int b5) { return uint16_t(0x8000 | (r5 & 31) << 10 | (g5 & 31) << 5 | (b5 & 31)); }
inline uint16_t texelFrom12(uint16_t rgb4) {  // a S3-16 colour
    const int r = (rgb4 >> 8) & 15, g = (rgb4 >> 4) & 15, b = rgb4 & 15;
    return texel(r * 2 + (r >> 3), g * 2 + (g >> 3), b * 2 + (b >> 3));
}

enum Blend : uint8_t { OPAQUE, HALF, ADD };

struct Vtx {
    float x = 0, y = 0;             // screen position
    float u = 0, v = 0;             // texture coordinates, in texels
    uint8_t r = 128, g = 128, b = 128;  // shade: 128 leaves the texture as it is
    float fog = 0;                  // 0 clear .. 1 fully fog
};

class GPU {
public:
    GPU();
    // Texture RAM. Wrap-around textures must be a power of two in each direction.
    int texture(int w, int h, const uint16_t* texels);
    void release(int from);  // free textures from this id on (a new stage's set replaces the last)
    int texW(int id) const { return tex_[size_t(id)].w; }
    int texH(int id) const { return tex_[size_t(id)].h; }
    size_t texBytes() const { return used_; }

    void setFog(int r5, int g5, int b5) { fogR_ = r5, fogG_ = g5, fogB_ = b5; }
    // The backdrop: one colour per scanline, drawn before anything else.
    uint16_t lineColor[H] = {};

    // Primitives. `depth` orders them (bigger is further); ties keep submission order.
    void tri(const Vtx& a, const Vtx& b, const Vtx& c, float depth, int tex = -1, Blend blend = OPAQUE);
    void quad(const Vtx& a, const Vtx& b, const Vtx& c, const Vtx& d, float depth, int tex = -1, Blend blend = OPAQUE);
    // A screen-space rectangle of a texture region (HUD, text): depth 0 is in front of everything.
    void sprite(float x, float y, float w, float h, int tex, float u0, float v0, float u1, float v1, float depth = 0,
                uint8_t shade = 128, Blend blend = OPAQUE, bool flip = false);

    // Draw the frame: backdrop, then the ordering table back to front. Returns ARGB pixels.
    const uint32_t* draw();
    int primitives() const { return int(prims_.size()); }
    int lastPrimitives() const { return lastPrims_; }

private:
    struct Tex {
        int w, h;
        size_t off;
        bool pow2;
    };
    struct Prim {
        Vtx v[3];
        float depth;
        int tex;
        Blend blend;
        uint32_t order;
    };
    void raster(const Prim& p);

    std::vector<uint16_t> ram_;
    size_t used_ = 0;
    std::vector<Tex> tex_;
    std::vector<Prim> prims_;
    std::vector<uint16_t> fb_;
    std::vector<uint32_t> out_;
    int fogR_ = 16, fogG_ = 16, fogB_ = 16;
    uint32_t order_ = 0;
    int lastPrims_ = 0;
};

}  // namespace g32
