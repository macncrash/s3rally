// S3 RALLY CHAMPIONSHIP - the rally car as a small 3D model, rendered
// into sprite ROM from every angle when the cartridge boots (the way 90s
// games pre-rendered their cars), so it can slide, spin and tumble.
#pragma once
#include "console/gfx.h"

namespace rc {

// Sprite frames are CAR_BW x CAR_BH pixels at CAR_PPM pixels per metre, with
// the point under the middle of the car at (CAR_BW / 2, CAR_GROUND).
constexpr int CAR_BW = 432, CAR_BH = 240;
constexpr float CAR_PPM = 96;
constexpr int CAR_GROUND = 150;
constexpr int YAW_FRAMES = 24;   // 15 degrees apart; frame 0 is the rear view
constexpr int PITCH_FRAMES = 3;  // nose down, level, nose up
constexpr int ROLL_FRAMES = 12;  // one full roll over

// yaw: radians, 0 = seen from behind, positive = nose turned to the viewer's right.
gs::Bitmap renderCar(float yaw, float pitch, float roll, float elevation);

}  // namespace rc
