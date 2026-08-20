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

Twelve cars race on eight circuits, seven of them stylized Colorado
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
| `source/track.c` | The eight circuits, built from control points into a sampled centreline with widths, corners and checkpoints. Portable. |
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
- **v1.15**: boost moves off the track and into the engine — every car has
  an `aspiration` (natural, turbo or supercharged) instead of a
  push-to-pass power-up; Berthoud Pass 2.0, an eighth circuit built from
  the real pass's own elevation profile; RUBY, a lightweight turbocharged
  car; and a mirrored minimap fixed.
- **v1.16**: boost and the fresh-tires power-up are both retired — no
  aspiration system, no item panel, no boost button. `TURBO` and
  `BLOWER` are gone from the garage; `RUBY` stays as a plain
  naturally-aspirated car. Tire wear rates retuned now that nothing
  ever refreshes them mid-race.
- **v1.17**: six new cars (BUGGY, WAGON, FORMULA, TRUCK, HERITAGE,
  MUSCLE) for eleven in the garage; Berthoud Pass 2.0 rebuilt so its own
  loop-back can't clip another part of the lap and no corner is sharper
  than a 37 m radius.
- **v1.18**: every car has a drivetrain (FWD, RWD, or AWD with a
  `front_bias`), grip is up ~25% across the board with a gentler,
  later-breaking understeer scrub, `default_kart_specs` in `game.c` now
  mirrors `cars.json`'s full eleven-car roster instead of just the
  original four (see §6 below — this is why cars added in v1.15–v1.17
  were invisible without an SD card), and RUBY's broken gear ladder
  that could strand it in first gear is fixed.
- **v1.19**: weather on CLASSIC only (three zones, snow → ice → puddle
  on their own staggered clocks, tire-compound grip multipliers in the
  new `weather` settings block); a from-scratch boost meter (charges
  with revs on a curve, spends in one shot on a new `boost` input,
  wiped by any gear change — unrelated to the aspiration-era boost
  retired in v1.16); a new `AI_YOLO` strategy on TANAKA and CROSS plus
  a field-wide skill bump; every barriered track's shoulder brought in
  to the guardrail, and Berthoud/Loveland/Monarch/Guanella scaled up to
  open out their tightest corners; and the data-only bones of a
  difficulty preset (`DifficultyPreset` in `game.h`) that nothing reads
  yet.
- **v1.19.1**: Berthoud Pass 2.0's real hairpin switchbacks are back,
  replacing the sine-wiggle shape v1.17.0 rebuilt it into. The
  "self-intersection" that rebuild fixed was never an actual gameplay
  bug — see §6 below — so the original v1.15.0 layout is restored and
  scaled up further (1.30 → 1.50) for room without smoothing anything
  out: 36 corners, an 8 m tightest radius, 85 m of climb.

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

**The compiled roster (`default_kart_specs` in `game.c`) is not read
from `cars.json` automatically — keep them in sync by hand.** `main.c`
falls back to it whenever there is no SD card (opening a release DOL
directly in an emulator, no virtual SD set up). Add a car to
`cars.json` and forget to add the same car to `default_kart_specs`
(and the duplicate `kart_specs[]` initializer right below it) and it
will build, pass a host test that only reads `cars.json`, and still be
invisible in the actual game for most players. `test_json_configuration`
checks `kart_spec_count == DEFAULT_SPEC_COUNT` after loading the
shipped file for exactly this reason — if that stops matching, one of
the two rosters drifted from the other.

**A gear ratio that never gets exercised can still be broken.** The AI
gearbox only tries to upshift if the target gear would land above
`bog_fraction + 0.04`; a gear-to-gear ratio much steeper than the rest
of the roster's ~1.65–1.75x can put a car exactly on the wrong side of
that margin and strand it in the lower gear for an entire race. This
hid in RUBY's `cars.json` entry for three releases because it was never
assigned to an AI driver until the roster-sync fix above put every car
into play — `test_full_race_classic`'s 300 s AI-finish timeout is what
caught it. If a new car's own dedicated tests all pass but an AI race
test times out or a CHARGER/LATE-strategy kart racks up zero mistakes,
check whether that new car is now actually being driven by the AI for
the first time.

**Smoothing a track's plan view is not the same as widening a corner.**
A relaxation filter that pulls each sample toward its neighbors'
average does the opposite of what it looks like it should for a track
with many corners close together (Guanella's chained switchbacks): it
pulls everything toward a smaller, shared centroid, so peak curvature
gets *worse* and the whole lap shrinks. What actually opens a corner
out without hand-editing its control points is a uniform `scale`
increase on that track's `TrackDef` entry (`track.c`) — grade% is
invariant under it since elevation is scaled by the same factor, so
it's the safe lever, not a position filter. v1.19.0's corner-softening
pass tried the filter first and threw it out once
`test_pavement_reaches_guardrail`-style manual measurement showed it
making Guanella worse.

**Two pieces of road passing close together in plan view is not
automatically a gameplay bug — check how `track_locate` is actually
called before reshaping anything to avoid it.** `track_locate` takes a
`hint` segment and, when given one (`hint >= 0`), only searches a
±8-segment window around it — it does not do a global nearest-point
search. The two call sites that matter for a car's actual position
(`kart_step`'s per-frame track relation, and grid placement at race
start) both pass a hint for exactly this reason; grid placement's
comment even names the risk directly. So a real switchback stack where
an upper and lower tier land within a meter of each other in (x, z) at
very different elevations — which is what a genuine mountain
switchback looks like from above — is harmless: the car can never snap
between tiers, because the search never looks that far from where it
already was. The only `hint = -1` (global) call is `main.c`'s
decorative tree scatter, where an occasional wrong answer is invisible.
v1.17.0 rebuilt Berthoud Pass 2.0's real hairpins into a smooth
sine-wiggle shape to make exactly this kind of plan-view proximity
check pass, and lost what made the track fun in the process, for a
problem that was never reachable in play. v1.19.1 put the hairpins
back. If a future "the geometry looks tangled" instinct shows up
again: check whether it is actually reachable through `track_locate`'s
windowed hint before reshaping anything.

**An AI strategy tuned too far can strand a driver in a fall/respawn
loop forever, not just make it a bit crashier.** Respawn puts a kart
back at the same checkpoint it left, so if a corner's grip-limited
speed is genuinely below what the AI's confidence tells it to carry
into that exact corner, it fails the same way every single time it
gets back up to speed — there is no randomness left to eventually get
it through. `test_yolo_can_finish_the_hardest_track` on Monarch (the
narrowest, least forgiving circuit) exists because an early `AI_YOLO`
tuning (`conf_start` 1.20) did exactly this. If a new/more aggressive
AI strategy passes on every other track but times out specifically on
Monarch, suspect this before anything else.

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

### 7b. Boost and the fresh-tires power-up — removed in v1.16.0

This mechanic has a longer history than most: v1.14.0 shipped a
rechargeable turbo as `KartSpec.has_turbo`; v1.15.0 generalized that into
`KartSpec.aspiration` (natural/turbo/supercharged) and retired the old
push-to-pass power-up in favor of it, keeping only fresh-tires on track.
v1.16.0 removes the whole thing — no aspiration system, no boost button,
no item panel at all. If you're looking for `ASPIRATION_TURBO`,
`Kart.boost_charge`, `read_aspiration`, `track_item_row`, `POWER_TIRES`,
`CONTROL_BOOST` or `CONTROL_ITEM`, none of them exist any more; a car's
power is just its plain `power_hp` figure, nothing multiplies it, and
`KartSpec`/`Kart`/`Track`/`GameSettings` all lost the fields that used to
back this (see the v1.16.0 `CHANGELOG.md` entry for the full list). Tires
still wear exactly as before (that part of the tire model is untouched)
— what's gone is any way to reset that wear mid-race.

`TURBO` and `BLOWER` are gone from `cars.json`: both existed purely to
demonstrate the aspiration mechanic, and their underlying chassis specs
were either a near-duplicate of `SPORT` or a strictly worse `TOURER` once
the multiplier was taken away, so there was nothing left worth keeping
them for. `RUBY` (six gears, 1080 kg, high horsepower) keeps its own
identity as a plain naturally-aspirated car — only the turbo
characteristic it shipped with in v1.15.0 is gone.

Removing the fresh-tires refresh meant the tire wear model needed
retuning: it had been calibrated assuming periodic resets from the
panel, so without any reset at all softs wore down to nothing partway
through even a short sprint and lost their sprint advantage entirely.
Current `tire_wear_rate`: medium 0.0080, soft 0.0200, hard 0.0018 (all
in `game_settings_defaults`, `game.c`, and mirrored in
`config/settings.json`'s `tires` block — keep those two in sync if you
touch either). `test_tire_strategy_crossover` is the test that would
catch this drifting out of balance again: softs quicker on a short
sprint (Classic), mediums/hards quicker on a long haul (Kenosha),
averaged across four AI karts to keep overcommit-gamble RNG timing from
being the actual thing the test measures.

If a boost mechanic comes back some day, it should probably not be the
same shape as either past attempt — a track pickup felt arcade-y, and an
engine multiplier turned out to be a lot of state (charge, spool, a
button, a gauge) for a fairly small effect. Worth deciding what problem
it's actually solving before rebuilding it.

### 7b′. Berthoud Pass 2.0, and what a switchback track has to get right — done in v1.15.0

`TRACK_BERTHOUD2` in `track.c` is built from the real Berthoud Pass
elevation profile: a stack of switchbacks up one side, a summit, a stack
down the other, a flat loop-back through the valley floor, and a gentle
climb back to a start/finish sitting at the lap's own middle elevation.

The first version of this track (raw control points reversing lateral
direction at *every single* control point, ~20 m apart) shipped 0 of 11
AI finishing even with an extended time budget, no matter how wide the
road was made — widening pavement did nothing because the problem was
never clearance. A real switchback road is a straight-ish ramp, then a
hairpin, then another ramp; the first version had no ramps at all, just
hairpin after hairpin with no straight section for the AI's pure-pursuit
steering to settle into between reversals. **Spacing between direction
reversals matters more than road width for AI navigability.** The fix
(see `CP_BERTHOUD2`) uses a repeating 5-point cycle borrowed from
`CP_GUANELLA` (a track that already raced fine): three points sweeping
laterally at roughly constant forward progress — the ramp — then two
points hooking the lateral direction back while advancing forward — the
hairpin — then the next cycle sweeps back the other way. Fewer, properly
spaced corners raced cleanly where more, tightly-packed ones did not.

Two more things this track's tight, self-crossing geometry exposed that
any future switchback-heavy track would hit again:

- **Elevation was hand-tuned per point at first and kept spiking to
  absurd grades** (a duplicate point at the loop closure alone produced a
  208% grade) whenever a short connector segment got the same per-point
  share of a large elevation change as a long sweep segment did. Interpolate
  elevation by *cumulative arc length* along the section instead of by
  point index — a short hop gets a proportionally small elevation change,
  a long one gets a proportionally large one, and local grade stays
  roughly constant across both sweeps and hairpins.
- **`kart_place_on_grid` calls `track_locate` with `hint = -1`** (a global
  nearest-point search) after already walking the centerline backward to
  find the exact intended segment. On every straight or gently-curved
  track that's harmless, since the nearest point to a grid slot's world
  position is always the segment it was placed on. On a track that loops
  back close to itself in world space — which a switchback climbing next
  to its own descent, or a track's own start/finish loop closure, both do
  — the global search can snap the grid slot onto a spatially-nearby but
  progress-wise-distant piece of road instead, which `test_full_grid_fits`
  caught as karts starting "past the line." Fixed by passing the
  already-known segment as the hint (`test_full_grid_fits`, `source/game.c`).
  Any future track that brings the road close to itself anywhere, not just
  near the line, should keep this in mind — `track_locate`'s global search
  is only safe where the track stays well clear of itself.
- Control points are capped at 55 (`TRACK_MAX_POINTS 440 / SAMPLES_PER_CP
  8`, `track.c`) — silently truncated past that, which would break loop
  closure with no warning. A 5-point ramp+hairpin cycle burns points fast;
  budget cycles per section before hand-designing one.

### 7b″. Berthoud Pass 2.0, rebuilt again — done in v1.17.0

The v1.15.0 layout above raced fine (11/11 AI finished), but two things
about it were wrong even though nothing in the test suite caught them:
several of its pieces of road passed within 1-2 m of each other in plan
view — including the valley loop-back clipping both the climb and the
base of the descent — and several of its hairpin apexes turned 90° to
161° at a single control point, sharper than a real switchback needs to
be. Neither shows up as a test failure: the AI still drives the
centerline fine even when two different laps'-worth of pavement
physically overlap, since progress is tracked by arc length, not world
position (the same reason `kart_place_on_grid` needed the hint fix in
7b′) — this is exactly the kind of thing that only shows up by actually
checking the geometry, not by racing it.

The fix replaced the ramp+hairpin-cycle approach with two clearly
separated "corridors": the climb follows `x = amp * sin(2*pi*z /
wavelength)` from the start up to the summit, the descent follows the
same shape mirrored and offset sideways by a fixed gap, and the two
never need checking against each other because the gap is chosen wider
than twice the wiggle amplitude plus road width — they're geometrically
incapable of touching regardless of how the wiggle itself turns out. The
amplitude is also tapered to zero over the last stretch before each end
of a corridor (a smoothstep, not a hard cutoff), so the corridor arrives
at the summit and valley turns already running dead straight — matching
the turns' own tangent there — instead of arriving at some arbitrary
angle and kinking into them. The summit and valley are each a single
wide circular arc (radius = half the gap between corridors), which is
plenty gentle on its own once the kink at the junction is gone.

Two general lessons worth keeping for the next track like this:

- **A self-intersection or a right-angle turn is invisible to game
  logic and to the AI-completion test alike** — arc-length progress
  doesn't care that the pavement crosses itself, and pure-pursuit
  steering doesn't refuse a sharp corner, it just drives it badly. If a
  track's shape matters (and it does, here), check the shape itself:
  walk every pair of non-adjacent sampled centerline points and assert
  a minimum separation (comfortably more than `2 * wall_half`, since
  the spline can bulge past the control polygon near a turn), and walk
  every control point's turn angle (deviation from straight) and assert
  a maximum. Both are cheap, deterministic checks you can run from a
  small throwaway harness against `track_init`'s actual output — do not
  trust hand arithmetic on the raw control points, since Catmull-Rom
  bulges past them, in both the ground plane and elevation, enough to
  matter (measured grade came out noticeably steeper here than the raw
  point-to-point arithmetic predicted).
- **Two geometrically-separated corridors joined by wide, amplitude-
  tapered turns is a much easier shape to reason about than a chain of
  hand-placed hairpins**, and it composes: the same recipe (parallel
  sine-wiggle corridors, gap sized off road width, taper into each
  joining turn) would work for another out-and-back mountain pass
  without repeating this debugging.

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
