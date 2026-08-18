# WiiKart 1.2.0

An original racing game built as **Nintendo Wii homebrew**. It compiles to
a real Wii executable (`wiikart.dol`) that runs in the
[Dolphin](https://dolphin-emu.org/) Wii emulator on your PC, and on real
Wii hardware through the Homebrew Channel.

**Download:** grab `wiikart.zip` from the
[latest release](https://github.com/Nessie404/starwarsgame/releases/latest),
unzip, and open `wiikart.dol` in Dolphin.

Version 1.2.0 is a semi-sim mountain racer: pick a car in the garage —
its gearbox, tires and paint, all defined by real-world performance
numbers — then race a **field of eleven AI drivers who each race
differently, shift differently, learn from their mistakes, and adapt to
you**, over five circuits including four stylized Colorado passes with
real grades, switchbacks, cliffs and gravity. Lap counts are set per
circuit (2–4 laps) so every race runs about the same length. Think arcade
fun with a driving model that expects you to brake for the hairpins.

**Version history**, oldest first:

- **v0.1** — the original arcade kart racer. **v0.1.1** patches it with
  sound and moves steering onto WASD, including the steering fix.
- **v1.0** — the driving-model rewrite: real physics, the garage, WASD,
  the Colorado passes. **v1.0.1** fixes its inverted steering.
- **v1.1.0** — the twelve-car field, each AI with its own strategy,
  learning from its mistakes and adapting to you; Berthoud rebuilt as a
  19-corner technical circuit; nitro and boost pads replaced with
  motorsport power-ups; the remaining arcade cheats removed; and
  **inverted steering fixed** — left really is left now, on every device.
- **v1.2.0** — gearboxes and per-driver shifting strategies, two more
  passes (Kenosha and Monarch), guardrails gone from Loveland and
  Monarch, cliffs with Mario-style checkpoint recovery, per-circuit lap
  counts, a rebuilt menu you can back out of, a controller-count check,
  more garage options, and two-player keyboard defaults.

> **Note on Nintendo content:** this is 100% original homebrew. It contains
> no Nintendo code or assets and does not require (or include) any game
> disc or ROM.

## Features

- **A grid of twelve, and nobody drives like anyone else.** Eleven AI
  rivals share out seven strategy sheets — BALANCED, LATE (brakes far too
  late), INSIDE (tight and defensive), DEFENDER (covers your favourite
  passing side), CHARGER (dives for every gap, spends its power-up at
  once), DRAFTER (sits in your mirrors saving it), CRUISER (cautious,
  wide, smooth). Each sheet sets its racing line, how it brakes, how it
  overtakes, when it spends a power-up, **and how it shifts** — where in
  the rev band it changes up, how early it comes down the box under
  braking, and how crisply it does it. CRUISER short-shifts and rides the
  torque; LATE hangs on to the limiter and pays for it in shift time.
- **They learn from their mistakes.** Every driver keeps its own nerve
  rating for each corner on the track. Run wide, clip a barrier, or spin
  and that corner's rating drops, so it arrives slower next lap; take it
  cleanly and the rating creeps back up. Over a race the field visibly
  tidies up — in the test suite, mistakes across the grid fall from 78 in
  the first third of a race to 13 in the last. The late-brakers start
  over the limit and talk themselves down; the cruiser starts timid and
  finds pace.
- **They adapt to you.** The game keeps a model of each human: which
  side you complete your passes on, how your pace compares with the
  leading AI, and how often you lean on people. Defenders move over to
  cover the side you keep using, a quick human raises the whole field's
  ambition (and a slow one lets it relax), and drivers you keep banging
  into start leaving you more room.
- **Physically-based driving.** Acceleration comes from engine power
  (F = P/v, traction-capped), top speed emerges from aerodynamic drag,
  braking matches the car's quoted 100-0 km/h stopping distance, and
  cornering is limited by lateral grip (v²/r ≤ μg) — push past it and the
  car understeers wide, scrubbing speed. Gravity acts along the road
  grade: climbs cost speed, descents give it back.
- **Car selection with real spec sheets.** Four cars (RACER, SPORT,
  RALLY, TOURER) defined by horsepower, curb weight, stopping distance,
  lateral g, drag area, wheelbase and dirt grip — the menu shows the
  derived 0-100 time and top speed, and the physics actually uses these
  numbers.
- **Five circuits**, all measured rather than guessed. Lap counts are set
  per circuit so every race covers a similar distance:

  | Circuit | Length | Width | Corners | Tightest | Climb | Steepest | Rails | Laps |
  |---------|--------|-------|---------|----------|-------|----------|-------|------|
  | CLASSIC | 555 m | 10 m | 10 | 24 m | flat | — | yes | 4 |
  | BERTHOUD (US-40) | 1421 m | 8.4 m | 19 | 11 m | 48 m | 15% | yes | 2 |
  | LOVELAND (US-6) | 1699 m | 10.8 m | 11 | 12 m | 61 m | 14% | **no** | 2 |
  | KENOSHA (US-285) | 2172 m | 12 m | 43 | 15 m | 58 m | 10% | yes | 2 |
  | MONARCH (US-50) | 1612 m | 7.6 m | 28 | 11 m | 73 m | 17% | **no** | 2 |

  **Kenosha** is the long, wide, flowing one — the biggest lap and the most
  corners, but the gentlest grades, so it is fast. **Monarch** is the
  hardest by every measure: narrowest road, tightest corners, steepest
  grades, biggest climb, and no guardrails anywhere. **Loveland** lost its
  guardrails too, and got wider and longer.
- **Gearboxes.** Every car has a real gearbox — 4 to 6 gears, each with a
  road speed at the limiter. Where you are in the band decides your power:
  bog it below a third of the band and it pulls badly, sit on the limiter
  and it stops pulling at all, and a shift cuts drive for a moment, so
  short-shifting out of a hairpin is a genuine decision. Pick **AUTO** or
  **SHIFT** (manual) per player in the garage.
- **Cliffs and checkpoints.** On the unguarded passes the shoulder is the
  edge: go over it and the car drops away, then gets set back down at the
  last checkpoint it passed — Mario-style, without handing out any free
  progress.
- **Garage.** Each player gets their own: car, one of eight paint colours,
  **gearbox** (auto or manual) and **tire compound** (soft grips more and
  drags more, hard is slipperier and faster), all on a lit 3D turntable
  with the full spec sheet and a bar chart of the car's gearing.
- **Steering with weight.** Every input runs through a virtual analog
  stick: the wheel winds on over a few tenths of a second, self-centers
  faster than it winds on, calms down as speed rises, and passes through
  a progressive curve. Keyboards feel like a stick, not a switch.
- **Split-screen multiplayer** for up to 4 players (horizontal split for
  2, quadrants for 3-4), with view culling so a full field still runs at
  frame rate in four-way split.
- **Power-ups, kept inside what a race car can do.** Roadside panels hold
  one of two things, and each panel always holds the same one so you can
  aim for what you want: **push-to-pass** (+13% engine for 4 s, in the
  region of IndyCar's real overtake boost) or **fresh rubber** (+10%
  lateral grip for 8 s). Deploy with E / Y / −. The AI spend theirs the
  way an engineer would — push-to-pass on open road with someone to
  catch, fresh rubber just before a twisty stretch.
- **No arcade cheats.** There is no rubber-banding (a test proves an AI
  left behind gets exactly the same power: 18.34 m/s either way), no
  floor boost pads, and no mini-turbo reward for sliding — the handbrake
  rotates the car and scrubs speed, because that is what a handbrake
  does.
- **Procedural audio:** engine note that follows revs, tire squeal at the
  grip limit, countdown beeps — synthesized at runtime via ASND, no
  sound assets.
- Chase cameras with terrain avoidance, minimap, 7-segment HUD with
  km/h speedo, rumble on every supported controller.

## Running it in Dolphin

1. Install Dolphin: <https://dolphin-emu.org/download/>
2. Download `wiikart.zip` from the
   [latest release](https://github.com/Nessie404/starwarsgame/releases/latest)
   (or build it — see below), unzip.
3. **File → Open…** → `wiikart.dol`. Or: `tools/run-dolphin.sh`

### Pick your controls (all natively supported)

- **Xbox / any gamepad:** Dolphin → **Controllers → GameCube Controller
  Port 1 → Standard Controller → Configure**, select your pad (XInput
  for Xbox) and map the stick, A/B/X/Y, R/L triggers and Start.
- **Keyboard (WASD):** Dolphin → **Config → Wii → Connect USB Keyboard**,
  and that's it — no key mapping needed. **A** steers left, **D** right,
  **W** is the throttle, **S** the brake (arrow keys mirror all four).
- **Wii Remote (emulated or real):** sideways grip, tilt to steer —
  Nunchuk and Classic Controller also work.

## Running on a real Wii

Copy the `apps` folder from `wiikart.zip` onto an SD card (so you have
`SD:/apps/wiikart/boot.dol`) and launch WiiKart from the Homebrew Channel.

## Controls

**Two players share one keyboard.** A second physical keyboard cannot be
told apart — libwiikeyboard merges every attached keyboard into one event
stream — so players 3 and 4 use pads (an Xbox pad appears as a GameCube
pad under Dolphin) or Wii Remotes.

| Action | Player 1 keyboard | Player 2 keyboard | Pad (Xbox/GameCube) | Wii Remote (sideways) |
|--------|-------------------|-------------------|---------------------|------------------------|
| Steer left / right | **A** / **D** | **J** / **L** | Main stick | Tilt or D-pad |
| Accelerate | **W** | **I** | A or X | 2 or A |
| Brake / reverse | **S** | **K** | B | 1 |
| Up a gear | **E** | **O** | R trigger | Classic ZR |
| Down a gear | **Q** | **U** | L trigger | Classic ZL |
| Power-up | **X** | **M** | Y | − / Classic − |
| Handbrake | Space | **P** | Z + B | Hold B |
| Back to menu | **R** | **R** | Start | + |
| Quit | (EXIT row) | — | Z + Start | HOME |

Arrow keys mirror player 1's WASD. Most of these are just defaults —
they're all in one `KeyMap` table at the top of `source/main.c` if you
want to move them.

**Menus never advance on a stray press.** Every screen is a list of rows
with a cursor: **W/S** (or ↑/↓, or the D-pad) moves the cursor,
**A/D** (or ←/→) changes the value of the row you are on, **Enter**
(or A/2) acts on it, and **Esc** (or B/1) backs out. Only the action rows
— GO, a player's GARAGE, DONE, EXIT — actually do anything when
activated, so you can look around without being thrown forward. Esc no
longer quits the game; EXIT does.

**You cannot start a race without enough controllers.** The setup screen
shows how many pads it can see, and GO refuses with `NEED n PADS FOUND m`
rather than starting a race with a player who cannot steer. If a
controller disappears mid-race, the race is abandoned after a moment's
grace and you land back in the menu with `CONTROLLER LOST` — no freeze,
no dead player.

Driving notes: brake before hairpins — the grip circle is real. The
handbrake rotates the car but costs you speed, so use it to place the car,
not to go faster. Watch the rev bar next to the gear number: shift at the
top of the band, and come down a gear before a hairpin so you are not
bogged on the exit. RALLY keeps 72% of its grip on dirt, TOURER only 35%.
Save push-to-pass for a straight where you have someone to catch, and
fresh rubber for the run into a switchback section. On Loveland and
Monarch there is nothing holding you on the road.

The HUD names the car you are chasing by its strategy, and the finish
screen prints the full classification, so you can see whether DEFENDER or
CHARGER actually got the job done. A quick AI laps Berthoud in about 62 s
and Loveland in about 59 s, against 27 s round Classic.

## The cars

| Car    | Power | Curb  | 100-0 | Lateral | Dirt grip | Gears | 1st tops at |
|--------|-------|-------|-------|---------|-----------|-------|-------------|
| RACER  | 48 hp | 260 kg | 30 m | 1.30 g  | 30% | 4 | 50 km/h |
| SPORT  | 150 hp | 950 kg | 37 m | 0.95 g  | 45% | 5 | 47 km/h |
| RALLY  | 220 hp | 1180 kg | 40 m | 0.88 g  | 72% | 6 | 43 km/h |
| TOURER | 310 hp | 1350 kg | 34 m | 1.02 g  | 35% | 6 | 54 km/h |

0-100 times and top speeds shown in the menu are derived from these
numbers by the same equations the physics uses.

## Building

You need devkitPro's **devkitPPC** toolchain (`wii-dev` package group).

```sh
export DEVKITPPC=/opt/devkitpro/devkitPPC
make            # wiikart.dol + wiikart.elf
make dist       # dist/apps/wiikart for SD cards

# or via the official container
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkitppc:latest make
```

Every push builds on GitHub Actions and publishes a release with
`wiikart.zip`; tags (e.g. `v1.0.0`) publish named version releases.

## Project layout

```
source/game.h     shared types, kart spec sheets, simulation API
source/track.c    3D circuit geometry (Catmull-Rom, elevation, curvature)
source/game.c     vehicle dynamics, AI strategies/learning/adaptation, items
source/main.c     Wii layer: GX renderer, menus, split screen, audio, input
tests/            host-side physics/AI tests (plain gcc, no Wii SDK)
hbc/              Homebrew Channel metadata
tools/            run-dolphin.sh launcher
```

The simulation is platform-independent C99, tested on the host:

```sh
gcc -std=c99 -O2 -Wall -Werror -Isource \
    tests/test_game.c source/game.c source/track.c -lm -o wiikart-test
./wiikart-test
```

Tests verify the physics against the spec sheets (measured stopping
distance vs quoted, grip-capped yaw rate, gravity on grades), the
steering filter (ramp shape, self-centering, speed sensitivity, and that
left really is left), track geometry and corner segmentation for all
circuits, that a twelve-car grid fits on the road, item pickup and AI
nitro use, and that the AI completes laps on every track — including
recovering from a botched hairpin by backing out, since a real car can't
rotate in place.

Steering polarity is tested the way you experience it: the test
re-derives the chase camera's right-hand axis (`cross(forward, up)`, the
same vector `guLookAt` uses) and asserts that a left input moves the car
toward the left of the screen. The earlier test only checked that the
heading angle changed, which is exactly how the controls shipped
mirrored in v1.0.

The v1.1 AI behaviour is tested as behaviour, not just as code: that the
field spreads out across distinct racing lines and error counts, that
mistakes fall as a race progresses, that overconfident strategies end up
believing less than they started and timid ones more, that a defender
covers the side a human has been passing on (and the mirror-image setup
produces the mirror-image line), and that an overtake is logged with the
side it happened on. Power-ups are checked for being deployed, being
distinct from one another, and staying within realistic bounds, and a
dedicated test proves the AI get no rubber-band power boost.

v1.2.0 adds tests for the gear model (the power curve's shape, that a
held key shifts exactly one gear, that a gear's limiter really caps
speed, and that neither the automatic box nor any AI strategy *hunts* —
this caught a real bug where short-shifting drivers changed gear 360
times in 90 seconds because an upshift landed on their own downshift
point), for the circuit roster (which tracks are unguarded, that Kenosha
is the longest/widest/turniest and Monarch the narrowest/tightest/
steepest), for lap counts scaling with circuit length, and for cliff
recovery: that a car off an unguarded edge falls, comes back at the last
checkpoint it passed, keeps its lap, gains no free progress, and is
drivable afterwards — while a barriered track still holds cars in.

## Ideas for later

- Open-road mode down the passes with traffic to overtake
- Ghost laps and lap-time records
- Weather (rain lowers μ; snow on the passes)
- More passes (Independence, Pikes Peak hill climb)
- Online time-trial leaderboards
