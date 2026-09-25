<div align="center">

# S3-16

**A 16-bit console that never existed, and the racing games made for it.**

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![SDL2](https://img.shields.io/badge/SDL-2-1e6fb8)
![Platforms](https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20browser-lightgrey)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

![(3) RALLY](https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/trailer.gif)

🎮 **[Play it now in your browser](https://macncrash.github.io/s3garally/)**, with no install.

🧊 **[Play (3) RALLY 32 on the S3-32](https://macncrash.github.io/s3garally/32/)**, the 32-bit version in real 3D.

▶ **[Watch the trailers](https://macncrash.github.io/s3garally-site/)**

</div>

---

The **S3-16** is a Genesis-style tile-and-sprite console written from scratch
in C++, plus the two custom chips that made the 80s "super scaler" arcade boards
special: a **hardware sprite scaler** and a **road generator**. Every graphic,
song, sound and voice line is generated when a cartridge boots. There are no asset
files. It comes with a multi-cart holding two games.

## (3) RALLY

**Real rallying.** Special stages are driven against the clock, one car at a time,
cars starting ten seconds apart. It's you, your co-driver, the road and the
weather. Passing happens, but rarely. What matters is how fast you can go without
flying off.

- **Five rallies, fifteen stages:**
  - **Finland:** fast gravel, blind crests and big jumps through pine and birch
    forest, a lakeside stage and one built around its jumps.
  - **Norway:** snow and ice between ploughed snowbanks you can lean on, the
    last stage at night on headlights.
  - **Italy (Sardinia):** narrow, rocky and twisty, with stone walls through
    the villages.
  - **Australia:** red dirt with marbles off the line, floodways, kangaroos and
    dust that hangs in the air behind the car ahead.
  - **Cyprus:** rough mountain roads, slow hairpins and sheer drops with no
    barrier, in the heat haze.
- **A real car model.** Tyres make grip from slip angle and share it between
  cornering, drive and braking. Weight moves forward under braking, so you get
  lift-off oversteer, power slides, handbrake turns, and understeer when you
  arrive too fast. Gravel, loose gravel, mud, snow, ice, tarmac, water and rock
  each grip and drag differently.
- **Jumps and crests you can fly off,** with the height and distance set by your
  speed. Throttle in the air lifts the nose and braking drops it. Land nose-first
  or sideways and you'll damage the car, or roll it.
- **Crashes that cost time.** Scenery is solid. Bounce off a rock and you may
  get a puncture. Hit a tree hard enough and you roll, and the marshals put you
  back on your wheels. Go over a drop and spectators haul the car back up.
  Engine, suspension, tyres and body wear and break, and each affects the
  handling.
- **A co-driver who reads real pace notes:** *"left four over crest, into right
  two, don't cut"*, *"one fifty, big jump"*, *"caution, ice"*. The calls are
  built from the road's actual geometry, with numbers from corner radius, and
  timed to your speed. A strip on screen shows what's coming.
- **Three views:** chase, far chase and **cockpit**. The cockpit has a turning
  wheel, rev counter, gear display, A-pillars, the co-driver beside you, and mud
  and spray building up on the screen until the wipers clear it.
- **Timing like the real thing:** a start clock, two split points (compared
  against the fastest crew), a flying finish and a stop control. Fifteen AI crews
  are on the times board, each with its own pace and home rallies.
- **Rally structure:** service between stages, with 15 minutes to choose repairs
  (10 seconds' penalty for every minute over). Retire and you rejoin at the next
  stage under Super Rally rules with a 5-minute penalty. Championship points go
  25-18-15-12-10-8-6-4-2-1.
- **Modes:** the Championship (all five rallies), a single rally, Time Attack
  against your own ghost, and **Online** for up to four crews. There are three
  levels (AMATEUR, PRO, LEGEND) and three cars, including a rear-drive legend
  that is a handful. **Driving help** is on by default: a held arrow key asks for the tightest turn the tyres can hold (never a spin-inducing full lock), spins are damped early, wheelspin is limited, and the soft ground past the edge scrubs off speed before you reach the trees. Turn it off on the car screen for the raw car.

<p align="center">
  <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/finland.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/norway-night.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/australia-dusk.png" width="32%"><br>
  <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/cockpit.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/jump.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/cyprus.png" width="32%"><br>
  <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/stage-intro.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/times.png" width="32%"> <img src="https://raw.githubusercontent.com/macncrash/s3garally-site/main/media/rc/service.png" width="32%">
</p>

## (3) RALLY 32: the S3-32

The start of the 32-bit console: a polygon GPU in the style of the mid-90s machines,
with 15-bit colour, Gouraud shading, affine textures, depth fog, transparency and an
ordering table in place of a depth buffer. It runs on the same board as the
16-bit machine, with the same pad, controls, menus and sound chip. Its first
cartridge, **(3) RALLY 32**, drives the same fifteen stages with the same car physics
and scenery in real 3D. The road has the shape of the land, and the car is lit
polygons. Pick it from the multi-cart menu, or run `./s3 --cart rally32`. The
design and the plan for hosting many consoles on one engine are in
**[docs/S3-32.md](docs/S3-32.md)**.

## S3 RUN

The original: a four-stage arcade racer in the OutRun mould. It also lives on as its own game,
**[SUNSET CRUISE](https://github.com/macncrash/s3cruise)** ([play](https://macncrash.github.io/s3cruise/)), rethemed as a run up the
coast from Los Angeles to San Francisco. You beat the
checkpoint clock through Desert, Forest, Alpine and Lakeside stages, passing a
pack of fifteen rivals, with two cars, a co-driver and head-to-head races. Both
games share the radio:

- **88.1 THE BLADE** (hair metal), **101.5 NEON FM** (synth pop), **94.7 KOOL**
  (rock & roll), **96.6 ARENA FM** (anthems), and **YOUR MUSIC**. Press **Tab**
  to change station. Stations keep playing while you listen to another, and the
  music ducks when the co-driver speaks. All eight built-in songs are original.
- **Rumour has it** the developers left something on S3 RUN's title screen
  for anyone who types the right four characters and presses Enter.

## Play in a browser

**https://macncrash.github.io/s3garally/**

The same C++ compiles to WebAssembly with Emscripten, so the browser runs the
real console at 60 fps. There is no second codebase. The browser speaks the
co-driver's calls with its own speech synthesis, keeps records in local storage,
and works with gamepads. Every push to `main` is built and tested in CI (native
on Linux, then WebAssembly) and published to GitHub Pages.

```bash
make web        # needs Emscripten (em++); output in build-web/, serve it statically
```

## Build and play

```bash
# macOS
brew install sdl2
# Debian / Ubuntu
sudo apt install libsdl2-dev

make
./s3              # the multi-cart menu: choose a game
./s3 --cart rally # straight into (3) RALLY (or --cart run)
```

| | Keyboard | PlayStation | Xbox |
|---|---|---|---|
| Steer | ← → | stick / D-pad | stick / D-pad |
| Accelerate | C / ↑ | Cross · R2 (analog) | A · RT |
| Brake (hold when stopped: reverse) | X / ↓ | Circle · L2 (analog) | B · LT |
| Handbrake (Rally) · turbo (Run, once unlocked) | Space | Square · L3 · R3 | X · LS · RS |
| Shift down / up (manual) | Q / W | L1 / R1 | LB / RB |
| Change view (Rally) | V or Z | touchpad | share |
| Radio station | Tab (or E) | Triangle | Y |
| Start · pause · select | Enter | Options | Menu |
| Back · quit · (on a title screen) choose another game | Esc | Create | View |
| CRT scanlines · fullscreen · screenshot | F1 · F11 · F12 | | |

**Driving tips (Rally):** brake in a straight line, then turn in. Lifting off
mid-corner lets the tail come round, and a touch of power holds a slide. On snow,
lean on the banks. Before a big jump, ease off a little: the landing slope is
built for a committed run, not a reckless one. Listen to the numbers: six is
nearly flat out, one is the slowest, and "don't cut" means rocks on the inside.

The co-driver's voice is recorded on first run with the macOS `say` command. This
takes about half a minute in the background, and the lines are kept in the save
folder. On Linux the game runs without speech. In a browser the page speaks the
calls.

### Online (up to 4 crews, LAN or internet)

In **(3) RALLY** choose **ONLINE**. One player chooses **HOST A
STAGE**, and everyone runs that stage, starting ten seconds apart in joining
order, just like a real rally. You see the others on the road if you catch them
or they catch you, and the results rank everyone by stage time. In **S3
RUN** it's **HEAD TO HEAD**, a wheel-to-wheel race of up to four. Either way,
players join by:

- **FIND GAMES ON LAN**: pick the game from the list (same network), or
- **JOIN BY ADDRESS**: type the host's IP and port, e.g. `203.0.113.7:47017`
  (the port defaults to 47017, and the last address is remembered).

No server is involved. The host is the hub, relaying each car's state to the
others 60 times a second (a 112-byte packet, about 7 KB/s per player). Remote
cars are extrapolated between packets, and the HUD shows your ping. **Over the
internet**, only the host needs a reachable port. Forward **UDP 47017** on the
host's router to the host machine, then give the other players your public IP.
It needs the desktop build, because browsers can't open UDP sockets. LAN
discovery uses UDP 47016. The two games keep their sessions apart, so a RUN
lobby never lists a Rally stage.

On macOS, the first time you host or join, the system asks whether S3 may accept
incoming network connections: choose **Allow**, or the others can't reach
you. Set `S3_NET_DEBUG=1` to log the protocol if a game won't connect.

### Four players on one screen

```bash
./s3 --quad 4                         # 2x2 split screen, (3) Rally: four crews, 10 s apart
./s3 --quad 4 --cart run              # the same for S3 RUN
./s3 --record-quad match.mp4 4        # film an autopilot 4-way match (needs ffmpeg)
```

`--quad` runs four complete consoles in one window, connected through the real
network code. The keyboard and pad 1 drive player 1, pads 2 to 4 drive the other
players, and seats without a pad are driven by the autopilot. After each run it
moves on to the next stage. Use it to test with four people on one computer, or
to capture footage for a trailer.

### Your music

Drop your own songs into the music folder and they become a **YOUR MUSIC** station
on the radio. WAV works everywhere. MP3, M4A/AAC, FLAC, AIFF and CAF are converted
automatically on first play, with macOS's built-in `afconvert` or with `ffmpeg` if
it's installed. Tracks play in file-name order, load in the background, duck under
the co-driver, and resume where you left off when you tune back in. `./s3
--music-dir` shows the folder. On macOS it's
`~/Library/Application Support/macncrash/s3engine/music/`. Only music you have the
right to play belongs there. The desktop build is required.

### Controllers

Plug in (or pair) a **PlayStation 5 DualSense**, a PS4 DualShock, an Xbox pad or any
SDL-supported controller. The game tells you when it connects and labels buttons the way
your pad does (Cross, Circle, L2 and so on). Every action can be remapped under
**CONTROLS**, and your layout is saved. Pads that support it rumble on landings,
crashes, rock strikes and rough ground, and a DualSense or DualShock light bar
glows in your car's colour (red when the car is badly damaged).

### Your driver profile

The first time you press Start you choose a **name** (up to 12 letters or numbers)
and get a permanent **driver ID**. Type it on a keyboard, or use a pad: up and down pick a
letter, right adds it, left deletes. Names don't have to be unique, because the ID is
what identifies a player. You can change your name any time under **PROFILE**, and the
ID stays the same.

Before the ID is created you're asked to agree. With your OK the game requests a
random UUID from [urandom.ai](https://urandom.ai) (`GET /v1/random/uuid`, with
the certificate verified). Only the request is sent, never your name. If the service
can't be reached, or you choose **USE AN OFFLINE ID**, a UUID is made from this
machine's `/dev/urandom` (or the browser's crypto) instead, and the profile records
which source it came from. The profile is a small `key=value` file next to your
records (localStorage in a browser), shared by both games.

### Version

The title, menu, pause and controls screens show the version and build,
for example `V2.0.0 (a1b2c3d)`. `./s3 --version` prints it too.

## The S3-16

| | |
|---|---|
| Video | 320×224 at 60 Hz · 4096 colours · 16 palettes · 8192 tiles · 2 scrolling planes and a HUD layer |
| Sprites | 256 per frame, each scaled to any size, with fog, shadow mode and clip line |
| Road generator | textured road from per-scanline registers. Revision B adds gravel ruts, mud puddles, ice, packed snow and rocky surfaces, snow walls, and open drops that show the valley below |
| Raster | per-line scroll, backdrop colour and distance fog |
| Audio | 9-channel 4-operator FM with per-channel overdrive, tone filter, vibrato, glide and echo send · 3 square + noise PSG · 4 PCM channels · stereo echo |
| Input | 6-button pad plus analog stick and triggers |
| Link | UDP for LAN and internet play |
| Multi-cart | pick a game after the boot logo; ESC on a title screen comes back |

The (3) Rally's car is a small 3D model rendered into sprite ROM from
every angle at boot, the way 90s games pre-rendered their cars. That gives 24
headings, 3 pitches and 2 roll-over sequences, so it can slide, spin and tumble.

Full reference: **[docs/HARDWARE.md](docs/HARDWARE.md)**.

```
src/console/   vdp · apu · system (SDL2 board, boot ROM) · gfx (SDK, font ROM) · link (UDP)
src/rc/        (3) RALLY: course (venues, stages, pace notes) · car (physics)
               carmodel (3D car sprites) · art · voice (co-driver) · game · drive · render · online
src/game/      S3 RUN: rally (game, physics, AI, HUD) · stages · art · sound
               shared by both: radio (songs + sequencer) · profile · versus (network sessions)
src/g32/       S3-32: gpu (polygons, ordering table, fog, dithering) · gte (camera, clipping)
src/rc32/      (3) RALLY 32: the rally in 3D on the S3-32
src/           main · multicart (the menu) · multi (several consoles in one process)
```

## Headless test

```bash
./s3 --sim                  # the autopilot drives every stage of both games and reports
./s3 --sim --cart rally     # just one game (or --cart run);  --shots DIR saves screenshots
./s3 --record V.raw A.raw   # S3 RUN: scripted player, raw video + audio for ffmpeg
./s3 --radio DIR            # render each radio station to a WAV
./s3 --music-test DIR       # check YOUR MUSIC loads, plays and advances with a folder
./s3 --versus-test [STAGE] [--players N] [--discover] [--cart run]   # 2-4 consoles race over loopback UDP
```

The daily simulation drives all fifteen (3) Rally stages with the three
cars in turn. It reports stage time against a perfect run, top speed, jumps, time
in the air, hard landings, crashes and damage.

## License

MIT © macncrash
