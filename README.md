# WiiKart 1.0

An original racing game built as **Nintendo Wii homebrew**. It compiles to
a real Wii executable (`wiikart.dol`) that runs in the
[Dolphin](https://dolphin-emu.org/) Wii emulator on your PC, and on real
Wii hardware through the Homebrew Channel.

**Download:** grab `wiikart.zip` from the
[latest release](https://github.com/Nessie404/starwarsgame/releases/latest),
unzip, and open `wiikart.dol` in Dolphin.

Version 1.0 is a semi-sim mountain racer: pick a car defined by
real-world performance numbers, then race 3 laps against 5 AI drivers over
three circuits — including two stylized Colorado passes with real grades,
switchbacks and gravity.

> **Note on Nintendo content:** this is 100% original homebrew. It contains
> no Nintendo code or assets and does not require (or include) any game
> disc or ROM.

## Features

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
- **Three tracks.** CLASSIC (flat speedway with boost pads), plus
  stylized versions of **Berthoud Pass** (US-40) and **Loveland Pass**
  (US-6) in Colorado: 6-10% grades, stacked switchbacks, guardrails and
  ~78 m of climb per lap.
- **Split-screen multiplayer** for up to 4 players (horizontal split for
  2, quadrants for 3-4).
- **Items and boost:** nitro canisters in item boxes, boost pads on the
  speedway, and a small handbrake-drift mini-turbo as the one arcade nod.
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
- **Keyboard:** Dolphin → **Config → Wii → Connect USB Keyboard**. Then:
  arrows steer, X/↑ gas, Z/↓ brake, Space/Shift handbrake, C item,
  R back to menu, Esc quit.
- **Wii Remote (emulated or real):** sideways grip, tilt to steer —
  Nunchuk and Classic Controller also work.

## Running on a real Wii

Copy the `apps` folder from `wiikart.zip` onto an SD card (so you have
`SD:/apps/wiikart/boot.dol`) and launch WiiKart from the Homebrew Channel.

## Controls

Menus: **left/right** change, **A/2/Enter** confirm, **B/1** back.

| Action      | Wii Remote (sideways) | + Nunchuk    | Classic     | GameCube pad (Xbox in Dolphin) | USB keyboard |
|-------------|------------------------|--------------|-------------|--------------------------------|--------------|
| Steer       | Tilt or D-pad          | Stick        | Left stick  | Main stick                     | ← / →        |
| Accelerate  | 2 or A                 | A            | a or x      | A or X                         | X or ↑       |
| Brake / reverse | 1                  | B            | b or y      | B                              | Z or ↓       |
| Handbrake / drift | Hold B           | C or Z       | L/R/ZL/ZR   | L or R trigger                 | Space/Shift  |
| Use item    | −                      | −            | −           | Y                              | C            |
| Back to menu | +                     | +            | +           | Start                          | R            |
| Quit        | HOME                   | HOME         | HOME        | Z + Start                      | Esc          |

Driving notes: brake before hairpins — the grip circle is real. The
handbrake rotates the car but scrubs speed; hold it through a bend and
release for a small mini-turbo. RALLY keeps 72% of its grip on dirt,
TOURER only 35%.

## The cars

| Car    | Power | Curb  | 100-0 | Lateral | Dirt grip |
|--------|-------|-------|-------|---------|-----------|
| RACER  | 48 hp | 260 kg | 30 m | 1.30 g  | 30% |
| SPORT  | 150 hp | 950 kg | 37 m | 0.95 g  | 45% |
| RALLY  | 220 hp | 1180 kg | 40 m | 0.88 g  | 72% |
| TOURER | 310 hp | 1350 kg | 34 m | 1.02 g  | 35% |

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
source/game.c     vehicle dynamics, AI (braking points + recovery), items
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
distance vs quoted, grip-capped yaw rate, gravity on grades), track
geometry for all circuits, item pickup, and that the AI completes laps
on every track — including recovering from a botched hairpin by backing
out, since a real car can't rotate in place.

## Ideas for later

- Ghost laps and lap-time records
- Weather (rain lowers μ; snow on the passes)
- More passes (Independence, Pikes Peak hill climb)
- Online time-trial leaderboards
