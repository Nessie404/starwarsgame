# WiiKart — handoff notes

Written so somebody else (a person, or another AI assistant) can pick this
up cold. It says what the project is, how to work on it safely, what is
already done, and exactly where to start on each thing that is not.

If you are an assistant reading this: **the golden rule is that every
change to the simulation gets a test in `tests/test_game.c` that fails
without it.** That is how everything here has been kept honest. Do not
skip it because a change looks obvious — several "obvious" changes in this
project's history shipped bugs that the tests caught.

---

## 1. What this is

An original racing game for the Nintendo Wii, written as homebrew in C99.
It runs in the [Dolphin](https://dolphin-emu.org) emulator on a PC and on
real Wii hardware through the Homebrew Channel. It contains no Nintendo
code or assets and needs no game disc.

Twelve cars race on seven circuits, four of them stylized Colorado
mountain passes. The driving model is semi-realistic: real power and mass,
braking distances, grip limits, gears, hills, and tires that heat up and
wear out.

---

## 2. How to build and test

**The tests run on any PC with gcc. You do not need a Wii toolchain.**
This is the main way to work on the game:

```sh
gcc -std=c99 -O2 -Wall -Werror -Isource \
    tests/test_game.c source/game.c source/track.c source/config.c \
    source/camera.c -lm -o wiikart-test
./wiikart-test
```

It takes a few seconds and prints measurements as it goes, ending in
`all tests passed`. If you add a new `.c` file under `source/`, add it to
that command **and** to `.github/workflows/build.yml`.

**The Wii build needs devkitPPC**, which is not installed here. It is done
by CI on every push (`make` in the devkitPro container). To check Wii-only
code (`source/main.c`) without the toolchain, syntax-check it against stub
headers — see section 6.

**Releases are automatic.** Push to the branch, and
`.github/workflows/build.yml` runs the tests, builds `boot.dol`, and
publishes a GitHub release named after the `VERSION` file. So: bump
`VERSION`, add a `docs/release-notes/vX.Y.Z.md`, update `CHANGELOG.md`,
push, and the release appears.

---

## 3. Map of the code

| File | What lives there |
|---|---|
| `source/game.h` | Every shared type and constant. Start here. |
| `source/game.c` | The simulation: physics, gears, tires, AI drivers, lap timing. Portable C99 — no Wii headers. |
| `source/track.c` | The seven circuits, built from control points into a sampled centreline with widths, corners and checkpoints. Portable. |
| `source/camera.c` | The chase camera. Portable, and tested. |
| `source/config.c` | The JSON parser and the loaders for the three config files. Portable. |
| `source/main.c` | **The only Wii-specific file**: video, controllers, sound, 3D rendering, HUD, menus. Cannot be compiled or tested on a PC. |
| `tests/test_game.c` | Every test. One big file, plain C, no framework. |
| `config/*.json` | The editable settings shipped with the game. |

**The split matters.** Anything that can go in the portable files should,
because that is what can be tested. When you add a feature, put the logic
in `game.c` (or a new portable module) and let `main.c` only draw it.

---

## 4. Conventions that are load-bearing

- **Tests measure, they do not just assert.** Most tests print a real
  number (`grade: 104.3 km/h down a 20% drop...`). Keep that: it is how
  regressions get spotted by eye in CI logs.
- **Settings live in JSON, not in constants.** If you add a tunable
  number, add it to `GameSettings` in `game.h`, give it a default in
  `game_settings_defaults`, a range check in `game_settings_validate`, a
  parser line in `config.c`, an entry in `config/settings.json`, and a row
  in `config/README.md`. All six, or it is half-done.
- **Config loading is transactional.** A bad file changes nothing. Keep it
  that way.
- **The HUD font is homemade** (`glyph_mask` and `hud_glyph` in
  `main.c`). It draws A–Z, 0–9 and a few symbols. Text is uppercase.
- **Comments explain why, not what.** The codebase is full of notes about
  why a number is what it is. Match that.
- **Never commit to another branch.** Work happens on
  `claude/mario-kart-wii-emulator-ez1pjz`.

---

## 5. What is already done

`CHANGELOG.md` has the full list, newest first. The short version:

- **v1.0–v1.2**: the driving model, the garage, gears, five circuits,
  cliffs and checkpoint recovery, menus you can back out of.
- **v1.3**: the three JSON config files (cars, settings, controls).
- **v1.4**: the chase camera rebuilt as a tested module; 480p and 16:9.
- **v1.5**: lap times, lap popup, live leaderboard, driver names, a
  pre-race lap count, a digital tachometer.
- **v1.6**: road width that varies along a lap; Breakneck and Guanella.
- **v1.7**: hills use real trigonometry; per-car shift points in
  `cars.json`.
- **v1.8**: tires with temperature and wear, so the compound is a choice.
- **v1.9**: finish-line marker on the minimap and in the race view; a
  JSON CONFIG screen that shows whether your edited files were read.
- **v1.10**: driver identity — skill, consistency, aggression and tire
  care per driver, separate from the strategy sheets.
- **v1.11**: fixed a build defect (`tools/pad_dol.py`) that could make a
  release refuse to open in an emulator or on hardware; an AI cool-down
  driver takes over when you finish instead of leaving you in control of a
  car with nothing left to do.
- **v1.12**: a marshal helicopter cuts your engine if you drive the wrong
  way; the on-screen controls panel is off by default in favour of the
  leaderboard.
- **v1.13**: a real apex-based racing line, so pace comes from skill
  rather than from which sheet a driver happens to run.
- **v1.14**: a rechargeable turbo, gated per car by a `turbo` block in
  `cars.json`.

---

## 6. Practical things that will bite you

**The JSON does nothing in Dolphin without an SD card.** Opening
`wiikart.dol` on its own gives the game no filesystem, so the config files
next to it cannot be read. Turn on Dolphin's Config → Wii → Insert SD
Card, put the release's `apps` folder on it, launch `boot.dol` from there.
The JSON CONFIG screen on the main menu tells you what was actually read.

**Checking `main.c` without devkitPPC.** Write stub headers that declare
the libogc functions the file uses (`gccore.h`, `wiiuse/wpad.h`,
`wiikeyboard/keyboard.h`, `asndlib.h`, `ogc/conf.h`, `fat.h`) into a
scratch directory, then:

```sh
gcc -fsyntax-only -std=gnu99 -Wall -Wextra -Werror -Isource -Istubs source/main.c
```

This catches typos and missing declarations. It does **not** prove the
real libogc has a symbol you invented — CI is the check for that.

**Tests share global state.** `kart_specs` is a global roster, so a test
that loads a JSON car file must call `kart_specs_reset_defaults()`
afterwards or it will poison later tests.

**The AI must always finish.** `test_ai_races_all_tracks` drives all
eleven AI round every circuit. If a change makes them slower, that test
starts failing by timeout. It is the canary for physics changes.

---

## 7. What is left, and how to start each one

`TODO.md` is the list. Here is the extra context for the ones that are
not obvious. Each is independent — pick any.

### 7a. Make pace come from skill, not from the racing line — done in v1.13.0

`line_bias` used to be a constant lateral offset held all lap, so a
sheet's pace tracked which way a given circuit's corners happened to bend
rather than who was driving it. Fixed by a new `Track.curv_signed[i]`
(alongside `curv[]`, same 5-sample smoothing window, but keeps the sign —
positive bends toward positive lat, matching `track_locate`'s convention)
and a rewritten `ai_tactical_line` (`source/game.c`) that looks 14 m up
the road, reads the signed curvature there, and leans toward that apex
scaled by `line_bias` — now 0..1 commitment rather than a signed offset.
A straight reads near-zero curvature, so the lean relaxes to the
centerline on its own; the defend/attack terms still layer on top
unchanged. `test_racing_line_pace_is_not_the_sheet` (`tests/test_game.c`)
is the proof: INSIDE and CRUISER, the two ends of the commitment range,
given identical skill and identical learned corner confidence, land
within 1.4% of each other on Berthoud, while `test_skill_sets_pace` still
shows skill alone worth 7.3%.

### 7b. A rechargeable boost, and a `turbo` block in `cars.json` — done in v1.14.0

`Kart.boost_charge` (0..1) drains while the button is held and recharges
while off the throttle — but only off the throttle, never while also
holding it, which is the actual strategic trade-off ("recharging costs
the speed accelerating would have bought"). The AI usage heuristic in
`ai_control` learned that the hard way: firing boost while `in->accel` is
already 0 (the car sitting at the speed the next corner's braking point
allows) drains the charge for nothing, since `kart_step` only spends the
power multiplier inside the `in->accel` branch — so it now also checks
`in->accel` before firing, not just a clear road and enough charge.

Gated per car by `KartSpec.has_turbo` and three tunables
(`boost_power_mult`, `boost_seconds`, `boost_recharge_seconds`), set from
an optional `"turbo"` object in `cars.json` and parsed by `read_turbo` in
`config.c`; a car with no block simply never sets `has_turbo`, and the
button (`CONTROL_BOOST` in `config.h`) is a no-op for it. The shipped
`config/cars.json` adds a fifth car, `TURBO`, as the worked example. The
HUD draws a gauge next to the tire bar for a car that has one, colored by
`Kart.boosting`, a one-frame flag for exactly that.

No dedicated button exists for it on a bare Wii Remote or Wii Remote +
Nunchuk — both are already out of spare buttons (see the comment beside
`in->item` in `main.c`'s input reader for the full inventory). It works
on keyboard, Classic Controller (D-pad up, unclaimed during a race) and
GameCube/Xbox pads (`DPAD_UP` by default).

Proof, in `tests/test_game.c`: `test_turbo_only_for_cars_that_have_one`
(holding the button on a car with no turbo block changes nothing),
`test_turbo_charge_drains_and_recovers` (both halves of "rechargeable",
not just one), and `test_boosted_lap_is_quicker` — a turbo SPORT laps
Berthoud in 72.6 s against a plain SPORT's 74.3 s, AI deciding for itself
when to spend the charge, which is what actually proves the mechanism end
to end rather than in isolation.

### 7c. Persistent standings between races *(start here)*

There is no save file at all yet — everything resets when the game exits.
You would write a small file next to the config (`sd:/apps/wiikart/`),
holding championship points, best laps per circuit and the player's
progress. Keep the format the same JSON style as the config so `config.c`
can read it back. Careful: writing to the SD card can fail, and must never
lose the existing file if it does — write to a temporary name and rename.

### 7d. Learning from the player's laps

Record the player's speed and lateral position per track segment on a lap
that beats the AI's best, then let selected AI use it to raise their
`corner_conf` on those corners. `Kart.corner_conf[]` is the existing
per-corner nerve, so the hook exists. The TODO asks for a reset control so
one heroic accident does not become permanent AI curriculum.

### 7e. Winter variants, snow and ice

The tire model already has a temperature window and per-compound grip, so
a cold surface is mostly a matter of a per-segment grip multiplier on the
track plus colder `tire_ambient_c`. Add a surface value per track sample
in `track.c`, multiply it into `mu_a` in `kart_step`, draw it in
`draw_track`, and give the winter compounds their own entries. Test that
the same car takes measurably longer to stop on ice.

### 7f. Distinctive scenery per pass

Purely `main.c` rendering: `draw_track` already draws rock skirts, trees
and guardrails from `Track.alpine`. Give each circuit its own palette and
roadside furniture so they are recognisable at a glance. No tests needed —
this is the one area where eyeballing it is the whole job.

---

## 8. If you only remember three things

1. Put logic in the portable files and write a test that fails without it.
2. Tunable numbers go in `settings.json`, through all six places listed in
   section 4.
3. Push to `claude/mario-kart-wii-emulator-ez1pjz`, bump `VERSION`, and CI
   builds and publishes the release for you.
