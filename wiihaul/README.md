# WiiHaul

An original **Nintendo Wii homebrew** truck-and-trailer simulator, built
next to [WiiKart](../README.md) — the racing game's engine, build
pipeline and Wii-homebrew bones, aimed at a completely different skill:
**pulling and backing a loaded semi**. It compiles to a real Wii
executable (`wiihaul.dol`) that runs in the
[Dolphin](https://dolphin-emu.org/) Wii emulator on your PC, and on real
Wii hardware through the Homebrew Channel.

WiiHaul is not a racing game. There is no lap counter and no rival
field — it is a solo skills test, closer in spirit to a real commercial
driver's license (CDL) yard exam than to Mario Kart. You pick a rig
(a tractor plus one or two trailers of a given shape and load) and a
scenario, then try to complete it clean: no cones knocked, no boundary
lines crossed, no jackknife, parked square in the target and holding.

## Why backing a trailer is hard, and why this is semi-realistic

The whole simulation rests on one piece of real vehicle kinematics: a
tractor is a standard bicycle-model car, and each trailer behind it is
governed by the same "car towing a trailer" differential equation
robotics and motion-planning literature uses. Nothing about reverse is
special-cased — the equation is simply run with negative speed, which is
*why* backing feels the way it does:

- **"Steer opposite the way you want the trailer to go"** — the classic
  driver's-ed lesson — falls straight out of the math. A forward turn's
  yaw rate flips sign in reverse for the same steering angle, so the
  same wheel input swings the trailer the other way once you're backing
  up.
- **Off-tracking** (a trailer cutting a corner tighter than the tractor's
  own path) falls out of the same equation on a forward turn.
- **Jackknifing** is a hard mechanical limit on the articulation angle at
  each hitch — clamped, not scripted — and once tripped the rig is only
  recoverable by easing forward and straightening the wheel, same as a
  real one.
- **Doubles** (two trailers) chain the identical equation twice, so
  nothing about the physics is special-cased for a second trailer either.
- **Mass is real.** A loaded low-boy hauling heavy equipment brakes and
  accelerates nothing like an empty yard tractor — see
  `rig_spec_total_mass` and how `rig_step` uses it.
- **A tanker sloshes.** Braking or accelerating a liquid load surges it,
  which wags the trailer a little extra on top of the base kinematics —
  a real hazard tanker drivers are trained for.
- **A grade is real gravity**, not a cosmetic slope: stop on the hill
  scenario without the parking brake and the rig rolls back.

See the file comment at the top of `source/truck.h` for the exact
equation and citations, and `tests/test_truck.c` for the regression
tests that hold all of this to account (including one that literally
asserts backing with the wheel held right swings the trailer left).

## The nine scenarios

Modeled on real CDL skills-test maneuvers: **Straight Back**, **Offset
Back Left/Right**, **Parallel Park**, **Alley Dock** (a 90° backing
maneuver), a forward **Cone Slalom** (tests trailer tail-swing awareness
on a weave), **Hill Start**, a multi-corner **Haul Route** ending in a
dock, and **Doubles Dock** for a two-trailer combo. Each scenario grades
an attempt on cone touches, boundary-line crossings, pull-ups past a free
allowance, and settling square in the target zone — Bronze/Silver/Gold,
or no pass. A short campaign unlocks each scenario after passing the one
before it (`career_scenario_unlocked` in `source/game.c`).

## The rigs

Six combinations of three tractors and five trailer shapes: a **dry van**
(the everyday box), a **flatbed** with stacked cargo, a **tanker** with
slosh, a **low-boy** hauling an oversized load, and a **pup** trailer for
the doubles combo. See `config/rigs.json`.

## Controls

Wii Remote held sideways (same grip WiiKart uses): D-pad left/right to
steer, **2**/A to accelerate, **1** to reverse, **B** (the underside
trigger) to brake, **-** for the parking brake, D-pad up for the horn,
**+** to pause. Nunchuk swaps D-pad steering for the analog stick.
Classic Controller, GameCube pads (Xbox pads through Dolphin) and a USB
keyboard are all supported too — see `config/controls.json`.

## Building

**The tests run on any PC with gcc — no Wii toolchain needed:**

```sh
./tools/run-host-tests.sh
```

or `make test`. This is the main way to work on the simulation — see
`docs/HANDOFF.md`.

**The Wii build needs devkitPPC** (`export DEVKITPPC=<path>`, or use the
`devkitpro/devkitppc` Docker image, which is what CI uses):

```sh
make        # produces wiihaul.dol
make dist   # packages a Homebrew Channel apps/ folder
```

Opening `wiihaul.dol` directly in Dolphin (File → Open) works with no
setup, running on the compiled-in defaults. To use the editable JSON
files, turn on Dolphin's Config → Wii → Insert SD Card, put `dist/apps`
on it, and launch `boot.dol` from there — see `config/README.md`.

## Status

This is a first release (v0.1.0) — see `TODO.md` for scoped-out follow-up
work (mirror-view insets, a full JSON-config debug screen, more
scenarios) and `docs/HANDOFF.md` for the architecture and how to keep
extending it safely.
