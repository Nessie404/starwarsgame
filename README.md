# WiiKart

An original Mario-Kart-style arcade racer built as **Nintendo Wii homebrew**.
It compiles to a real Wii executable (`wiikart.dol`) that runs in the
[Dolphin](https://dolphin-emu.org/) Wii emulator on your PC, and on real Wii
hardware through the Homebrew Channel.

Race 3 laps around a circuit against 4 AI drivers, with Mario-Kart-style
drifting, mini-turbos, boost pads, rubber-band AI, a chase camera, minimap
and tilt steering with the Wii Remote held sideways.

> **Note on Nintendo content:** this is 100% original homebrew. It contains
> no Nintendo code or assets and does not require (or include) any game disc
> or ROM. Emulating the actual *Mario Kart Wii* game requires a disc image
> you have dumped from your own copy, which this project neither provides
> nor needs.

## Running it in the Dolphin Wii emulator

1. Install Dolphin: <https://dolphin-emu.org/download/>
2. Get `wiikart.dol` (see **Building** below, or grab the `wiikart`
   artifact from the GitHub Actions **build** workflow of this repo).
3. In Dolphin: **File → Open…** and pick `wiikart.dol`. Or from a terminal:

   ```sh
   tools/run-dolphin.sh            # finds dolphin-emu and boots the .dol
   # equivalent to: dolphin-emu -b -e wiikart.dol
   ```

Dolphin emulates the Wii Remote for you (**Controllers → Emulated Wii
Remote → Configure**): map the buttons below to your keyboard/gamepad, and
map *Tilt* if you want motion steering, or just steer with the D-pad
mappings.

## Running on a real Wii

1. Build the Homebrew Channel layout: `make dist`
2. Copy `dist/apps` onto an SD card so you end up with
   `SD:/apps/wiikart/boot.dol` (plus `meta.xml` and `icon.png`).
3. Launch **WiiKart** from the Homebrew Channel.

## Controls (Wii Remote held sideways, like Mario Kart Wii)

| Input            | Action                                    |
|------------------|-------------------------------------------|
| Tilt the remote  | Steer (steering-wheel style)              |
| D-pad            | Steer (digital alternative)               |
| **2** (or A)     | Accelerate                                |
| **1**            | Brake / reverse                           |
| **B** (trigger)  | Hold while turning to drift; release for a mini-turbo (charge longer for a bigger one) |
| **+**            | Restart the race                          |
| **HOME**         | Quit to loader                            |

Orange pads on the road give a speed boost. Grass is slow — unless you're
boosting. Drift sparks go blue → yellow → orange as your mini-turbo charges.

## Building

You need devkitPro's **devkitPPC** toolchain (`wii-dev` package group).
No other dependencies — no assets, no external libraries beyond libogc.

```sh
# with devkitPPC installed locally
export DEVKITPPC=/opt/devkitpro/devkitPPC
make            # produces wiikart.dol + wiikart.elf
make dist       # assembles dist/apps/wiikart for SD cards

# or without installing anything, via the official container
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkitppc:latest make
```

Every push also builds `wiikart.dol` on GitHub Actions
(`.github/workflows/build.yml`) and uploads it as the `wiikart` artifact,
so you can download a ready-to-run binary from the Actions tab.

## Project layout

```
source/game.h     shared types and the simulation API
source/track.c    circuit geometry (Catmull-Rom spline), locate/progress
source/game.c     kart physics, drifting, boost, AI, laps, ranking
source/main.c     Wii platform layer: GX 3D renderer, HUD, Wiimote input
tests/            host-side tests for the simulation (plain gcc, no Wii SDK)
hbc/              Homebrew Channel metadata (meta.xml, icon.png)
tools/            run-dolphin.sh launcher
```

The simulation (`game.c`, `track.c`) is deliberately platform-independent
C99, so it is unit-tested on the host with a normal compiler:

```sh
gcc -std=c99 -O2 -Wall -Werror -Isource \
    tests/test_game.c source/game.c source/track.c -lm -o wiikart-test
./wiikart-test
```

The tests simulate whole AI races headlessly and check track geometry, lap
counting, ranking, drift boosts and physics stability.

## Ideas for later

- Engine/skid audio via ASND
- More circuits and a track selector
- Items, and split-screen multiplayer for up to 4 Wii Remotes
