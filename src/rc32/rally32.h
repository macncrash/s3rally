// (3) RALLY 32 - the first S3-32 cartridge.
//
// The same fifteen stages, the same car physics and the same scenery art as
// (3) RALLY on the S3-16, drawn by the 32-bit machine's polygon GPU: a
// real 3D road you can see the shape of, hills and drops, and the car as
// lit polygons instead of pre-rendered frames. Same pad, same keys, same menu.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "console/system.h"
#include "g32/gpu.h"
#include "g32/gte.h"
#include "game/radio.h"
#include "game/sound.h"
#include "rc/art.h"
#include "rc/car.h"
#include "rc/course.h"

namespace rc32 {

class Rally32 : public gs::Cart {
public:
    const char* title() const override { return "(3) RALLY 32"; }
    void init(gs::System& sys) override;
    void frame(gs::System& sys) override;
    bool video(const uint32_t*& px, int& w, int& h) override;

    // Headless: drive a stage with the autopilot; returns the stage time (0 if it didn't finish).
    float simulate(int stage, int frames, std::vector<std::string>* shots, const std::string& dir);
    int lastTriangles() const { return gpu_.lastPrimitives(); }
    size_t textureBytes() const { return gpu_.texBytes(); }

private:
    enum class Mode { Title, Pick, Drive, Done };
    void loadStage(int stage);
    rc::CarInput readPad();
    rc::CarInput autopilot();
    void scene();
    void follow();
    void hud();
    void text(const std::string& s, float x, float y, float scale, uint16_t color, int align = 0);
    g32::V3 roadPoint(float s, float x) const;  // world position of a point on the course
    float headingAt(float s) const;

    gs::System* sys_ = nullptr;
    g32::GPU gpu_;
    rc::Art art_;  // the S3-16 art, built once and turned into textures
    std::unique_ptr<rally::Radio> radio_;
    std::unique_ptr<rally::Sfx> sfx_;
    int font_ = -1, logo_ = -1, firstStageTex_ = -1;
    int roadTex_[rc::SURF_COUNT] = {}, groundTex_ = -1, vergeTex_ = -1, waterTex_ = -1, snowTex_ = -1, dropTex_ = -1;
    int objTex_[rc::O_COUNT] = {};
    uint16_t livery_[16] = {};

    Mode mode_ = Mode::Title;
    int t_ = 0, stage_ = -1, pick_ = 0, carId_ = 1;
    bool attract_ = true;
    rc::Course course_;
    rc::Car car_;
    std::vector<float> botV_;
    std::vector<g32::V3> pos_;  // centre line, world metres, one per segment
    float time_ = 0, best_ = 0;
    g32::Camera cam_;
    float camYaw_ = 0, camY_ = 0;
};

}  // namespace rc32
