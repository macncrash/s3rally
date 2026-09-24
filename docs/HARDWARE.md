# S3-16 hardware reference

The S3-16 is a 16-bit tile-and-sprite machine in the Genesis mould. It adds
the custom chips of the era's "super scaler" arcade boards: a sprite scaler and
a road generator.

## Video (`src/console/vdp.h`)

| | |
|---|---|
| Resolution | 320×224 at 60 Hz, shown at 4:3 |
| Colour | 12-bit RGB (4096 colours), 16 palettes × 16 entries; index 0 is transparent |
| VRAM | 8192 tiles, 8×8 at 4 bits per pixel |
| Planes | B (far) and A (near), with power-of-two sizes, per-line H and V scroll |
| HUD plane | 40×28 tiles, fixed, drawn on top of everything |
| Sprite ROM | 24 MB of 4-bit pixels |
| Sprites | 256 per frame, each **scaled to any size**, with h-flip, fog level, a clip line (for hill occlusion) and a shadow mode (darkens what is beneath) |
| Road generator | a textured road layer driven by per-scanline registers |
| Line registers | backdrop colour and distance fog, per scanline |

Layer order, back to front: backdrop → Plane B → Plane A → road → shadows →
sprites → HUD.

**Road generator.** For each scanline the cart writes a `RoadLine`: whether the
line is on, the road centre and half-width in pixels, a texture distance `v`,
a palette bank, a light or dark band, a surface style, and the ground type on
each side. The chip draws grass or sand, verges, dirt with pebbles and tyre
tracks, tarmac with kerbs and a centre line, water fords, and lake shores. The
texture scrolls with `v` and loses detail with distance.

**Fog.** The fog colour register is blended per scanline into planes and road,
and per sprite into sprites, at 17 levels.

## Audio (`src/console/apu.h`)

- **FM:** 6 channels of 4 operators, 8 algorithms, operator-1 feedback, and
  ADSR on every operator.
- **PSG:** 3 square channels, plus a 15-bit LFSR noise channel with one-shot
  bursts.
- **PCM:** 2 sample channels with pitch control.
- **Output:** stereo, through a low-pass filter, a DC blocker and a soft clipper.

## Pad

A 6-button pad: D-pad, A B C, X Y Z, START and MODE. Analog stick and triggers
are available as `axisX`, `accel` and `brake`.

## Cartridge interface

```cpp
class Cart {
    virtual const char* title() const;
    virtual void init(gs::System&);   // after the boot ROM
    virtual void frame(gs::System&);  // 60 times a second
};
```
