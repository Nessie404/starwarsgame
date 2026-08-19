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

### 7a. Make pace come from skill, not from the racing line *(start here)*

**The problem.** Each AI strategy sheet has a `line_bias` — a constant
lateral offset held all lap. The INSIDE sheet sits 0.55 of the way to the
inside edge, so its path around the circuit is literally shorter and it is
quick; the CRUISER sheet sits wide and is slow. That happens regardless of
who is driving, so a talented cautious driver is still slow. Measured on
Berthoud, best laps track `line_bias` more closely than they track skill.

**The fix.** Build a real racing line instead of an offset: turn in from
the outside, clip the apex, run out wide again. `Track.curv[]` (per
sample, already smoothed) tells you where the corners are and which way
they bend; `Track.corner_id[]` groups them. In `ai_tactical_line`
(`source/game.c`) return an offset that follows that shape, and let each
sheet decide *how committed* the line is (how close to the apex, how early
the turn-in) rather than a constant offset.

**How you will know it worked.** Add a test that gives two drivers the
same `ai_skill` and different sheets, races them, and checks their best
laps are within about 3% of each other — while `test_skill_sets_pace`
(already written) still shows a skill difference being worth seconds.

### 7b. A rechargeable boost, and a `turbo` block in `cars.json`

Add `boost_charge` (0–1) to `Kart`, recharging when off the throttle and
draining while deployed; a button to fire it (add an action in
`config.h`'s `CONTROL_*` list, bind it in `controls.json` and in
`main.c`'s input reading); a gauge on the HUD next to the tire bar. Gate
it per car with an optional `"turbo": { ... }` object in `cars.json`
parsed in `config.c` — only cars that have one get the button. Test: a car
without a turbo block cannot boost; charge falls while boosting and
recovers when lifting; a boosted lap is quicker than an unboosted one.

### 7c. Persistent standings between races

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
