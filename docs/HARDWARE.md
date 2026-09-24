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

- **FM:** 9 channels of 4 operators, 8 algorithms, operator-1 feedback, and
  ADSR on every operator. Each channel also has overdrive, a tone filter,
  delayed vibrato, pitch glide and an echo send, and operators can be
  retuned live for chord voicings.
- **PSG:** 3 square channels, plus a 15-bit LFSR noise channel with one-shot
  bursts.
- **PCM:** 4 sample channels with pitch and pan.
- **Output:** stereo, through a cross-fed stereo echo, a low-pass filter, a DC
  blocker and a soft clipper.

## Pad

Up to one game controller at a time, through SDL: PlayStation, Xbox, Switch Pro and
others are recognised by type. Physical inputs (buttons plus both triggers) pass
through a remappable table onto the console's virtual
6-button pad; analog stick and triggers are also exposed directly. Rumble and
light-bar output are supported where the pad has them.

The virtual pad is a 6-button pad: D-pad, A B C, X Y Z, START and MODE. Analog stick and triggers
are available as `axisX`, `accel` and `brake`.

## Link port

A non-blocking UDP interface (`src/console/link.h`) for network play, on a LAN or over the internet by address:
open a port, send, broadcast to every interface, poll for datagrams. It isn't
available in the browser build.

## Save storage

`loadBlob` and `saveBlob` hold small settings files. They are files in the
user's data folder on desktop and localStorage in a browser.

## Cartridge interface

```cpp
class Cart {
    virtual const char* title() const;
    virtual void init(gs::System&);   // after the boot ROM
    virtual void frame(gs::System&);  // 60 times a second
};
```
