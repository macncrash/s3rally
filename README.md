<div align="center">

# S3 RALLY

**A 16-bit arcade rally racer, running on a console that never existed.**

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![SDL2](https://img.shields.io/badge/SDL-2-1e6fb8)
![Platforms](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

![Gameplay](https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/gameplay.gif)

🎮 **[Play it now in your browser](https://macncrash.github.io/s3garally/)**, with no install.

▶ **[Watch the full run with sound](https://macncrash.github.io/s3garally-site/)**: boot, menus, car select, and a Desert stage from 16th on the grid.

</div>

---

S3 RALLY is a four-stage championship in the style of mid-90s arcade
rallying. You power-slide gravel bends, splash through fords, listen to your
co-driver's pace notes, and chase checkpoints before the clock runs out.

It runs on the **S3-16**, a 16-bit console written from scratch in C++. The
console is a Genesis-style tile-and-sprite machine plus the two custom chips
that made the 80s "super scaler" arcade boards special: a **hardware sprite
scaler** and a **road generator**. Every graphic, song and sound effect is
generated at boot. There are no asset files.

## Features

- **4 stages:** Desert canyon, Forest pass, Alpine summit in falling snow, and
  a Lakeside sunset special stage that you have to earn.
- **3 modes:** Championship (your finishing position becomes your grid slot for
  the next stage), Practice, and Time Attack with saved records.
- **15 AI rivals** that brake for corners, pick lines, and block.
- **Surfaces change the handling:** tarmac, dirt, sand, snow and water fords all
  drive differently.
- **2 cars,** each with automatic or manual transmission.
- **A co-driver** calls every corner: *"medium left"*, *"hairpin right, don't
  cut!"*, *"over crest!"*.
- **An in-car radio.** Press **Tab** to flip between three stations, each with an
  announcer ident, and static as the dial moves:
  - **88.1 THE BLADE** (hair metal): double-tracked distorted power chords,
    palm-muted chugs and screaming bent-note solos.
  - **101.5 NEON FM** (synth pop): sequenced bass, echoing arpeggios, gated
    snares and handclaps.
  - **94.7 KOOL** (rock & roll): a shuffled boogie bass, a honking sax, and
    pounding piano.
  - **96.6 ARENA FM** (anthems): stabbed fight-anthem power chords, and a
    synth-brass fanfare over a galloping bass.
  - **YOUR MUSIC**: your own songs (see below).

  Stations keep playing while you listen elsewhere, so flipping back lands you
  mid-song, and the music ducks when the co-driver speaks. All eight built-in
  songs are original.
- **FM sound with real grit.** The engine note follows the revs, and rival
  engines pass with Doppler.
- **An arcade finish:** CRT scanlines, 4:3 output, a live course map and a rev
  counter. The clock-out comes with a *"Game over. Yeah!"*

<p align="center">
  <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/title.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/menu.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/cars.png" width="32%"><br>
  <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/village.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/forest.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/mountain.png" width="32%"><br>
  <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/desert.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/lakeside.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/shots/radio.png" width="32%">
</p>

## Play in a browser

**https://macncrash.github.io/s3garally/**

The same C++ compiles to WebAssembly with Emscripten, so the browser runs the
real game at 60 fps. There is no second codebase. The web build uses the
browser's speech synthesis for the co-driver, saves records in local storage,
and works with gamepads. Every push to `main` is built and tested in CI (native
on Linux, then WebAssembly) and published to GitHub Pages.

```bash
make web        # needs Emscripten (em++); output in build-web/, serve it statically
```

The game runs entirely in the player's browser, so hosting it is just static
files, and any web host or CDN will do. Online features only need a small
server next to it. A shared leaderboard is a few HTTP endpoints. Head-to-head
racing is a WebSocket relay that swaps car positions each frame, and the fixed
60 Hz simulation makes lockstep input sync straightforward.

## Build and play

```bash
# macOS
brew install sdl2
# Debian / Ubuntu
sudo apt install libsdl2-dev

make
./s3
```

| | Keyboard | Gamepad |
|---|---|---|
| Steer | ← → | stick / D-pad |
| Accelerate | C / ↑ | right trigger · A |
| Brake | X / ↓ | left trigger · B |
| Shift down / up (manual) | Q / W | LB / RB |
| Start · pause · select | Enter | Start |
| Back · quit (while paused) | Esc | Back |
| Radio station | Tab (or E) | Y |
| Turbo boost (once unlocked) | Space | click either stick |
| CRT scanlines · fullscreen · screenshot | F1 · F11 · F12 | |

### Your music

Drop your own songs into the music folder and they become a **YOUR MUSIC** station
on the radio. WAV works everywhere. MP3, M4A/AAC, FLAC, AIFF and CAF are converted
automatically on first play, with macOS's built-in `afconvert` or with `ffmpeg` if
it's installed. Tracks play in file-name order, load in the background, duck under
the co-driver, and resume where you left off when you tune back in. The game prints
the folder on startup, and `./s3 --music-dir` shows it too. On macOS it's
`~/Library/Application Support/macncrash/s3engine/music/`. Only music you have the
right to play belongs there. The desktop build is required.

### Controllers

Plug in (or pair) a **PlayStation 5 DualSense**, a PS4 DualShock, an Xbox pad or any
SDL-supported controller. The game tells you when it connects and labels buttons the way
your pad does (Cross, Circle, L2 and so on).

| Action | PlayStation | Xbox |
|---|---|---|
| Accelerate | Cross / R2 (analog) | A / RT |
| Brake | Circle / L2 (analog) | B / LT |
| Turbo | Square / L3 / R3 | X / LS / RS |
| Radio | Triangle / touchpad | Y |
| Shift down / up | L1 / R1 | LB / RB |
| Start / back | Options / Create | Menu / View |

All of these can be remapped under **CONTROLS** on the main menu, and your layout
is saved. Pads that support it rumble on crashes, bumps, rough ground and turbo,
and a DualSense or DualShock light bar glows in your car's colour (red while the
turbo burns).

### Head to head (LAN)

Race a friend on the same network. Choose **HEAD TO HEAD**, pick a car, then one
player chooses **HOST A GAME** (and a stage) and the other **JOIN A GAME**. Games
host themselves automatically, so the joiner just picks one from the list. Each machine
drives its own car and streams its position to the other 60 times a second. This
is the same shape a future internet server will use. It needs the desktop build,
because browsers can't open UDP sockets. It uses UDP ports 47016 (discovery) and 47017.

### Version

The title, menu, pause and controls screens show the version and build,
for example `V1.4.0 (a1b2c3d)`. `./s3 --version` prints it too.

**Rumour has it** the developers left something on the title screen for anyone who types the right
four characters and presses Enter. Whatever it is, it comes with three shots of turbo per stage.

**Tip:** at speed, hold the steering *into* a bend. The tail steps out and the
car carries speed through corners that would otherwise push you onto the grass.

The co-driver's voice is generated on first run with the macOS `say` command.
On other systems the game runs without speech.

## The S3-16

| | |
|---|---|
| Video | 320×224 at 60 Hz · 4096 colours · 16 palettes · 8192 tiles · 2 scrolling planes and a HUD layer |
| Sprites | 256 per frame, each scaled to any size, with fog, shadow mode and clip line |
| Road generator | textured road from per-scanline registers: surface, verges, water, kerbs, bands |
| Raster | per-line scroll, backdrop colour and distance fog |
| Audio | 9-channel 4-operator FM with per-channel overdrive, tone filter, vibrato, glide and echo send · 3 square + noise PSG · 4 PCM channels · stereo echo |
| Input | 6-button pad plus analog stick and triggers |

Full reference: **[docs/HARDWARE.md](docs/HARDWARE.md)**.

```
src/console/   vdp · apu · system (SDL2 board, boot ROM) · gfx (SDK, font ROM)
src/game/      rally (game, physics, AI, HUD) · stages · art · sound · radio (songs + sequencer)
```

## Headless test

```bash
./s3 --sim                  # autopilot races every stage and reports
./s3 --sim --shots DIR      # plus PNG screenshots
./s3 --record V.raw A.raw   # scripted player, raw video + audio for ffmpeg
./s3 --radio DIR            # render each radio station to a WAV
./s3 --music-test DIR       # check YOUR MUSIC loads, plays and advances with a folder
./s3 --versus-test [STAGE] [--discover]   # two consoles race over loopback UDP
```

## License

MIT © macncrash
