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
- **v1.19.2**: fixed a real (if inert) data-drift bug — the compiled
  fallback roster hardcoded `awd_front_bias = 0.0` for FWD/RWD cars
  while the JSON parser's own default is `0.5` — and added
  `test_compiled_roster_matches_cars_json`, which diffs every field of
  every car between the two rosters instead of just the count (see §6
  below). No feature work; also clarified that there is no
  turbocharger/supercharger system (retired in v1.16) and that
  v1.19.0's `boost` is an unrelated, universal mechanic tuned in
  `settings.json`, not `cars.json`.
- **v1.20.0**: a genuinely punishing/rewarding oversteer and understeer
  rework (progressive understeer scrub, plus a new catchable power-
  oversteer/spin mechanic for rear-driven cars under throttle — new
  `understeer`/`oversteer` settings blocks); another AI competitiveness
  pass (`skill_multiplier` 1.02 → 1.06, `braking_multiplier` 0.72 →
  0.76); the data-only bones of a colour-based team mode (`TeamDef`,
  `GameConfig.team_mode`/`team[]`) and a career/campaign mode
  (`CareerState`, `GameConfig.career[]`) — neither wired into a race
  yet, same status as v1.19.0's difficulty preset; and the weather
  system fleshed out further: standing water now drags at every car
  (new `weather_puddle_drag_mult`), and the AI's own corner-speed
  lookahead discounts grip for whatever weather patch is ahead instead
  of assuming dry pavement everywhere.
- **v1.21.0**: boost reworked from a meter-you-charge-and-spend into an
  automatic turbo (`Kart.turbo_spool`, `turbo_spool_rate`/
  `turbo_spool_decay_rate`/`turbo_max_power_bonus` in the renamed
  `turbo` settings block) that builds and bleeds off on its own with
  real throttle and revs and applies straight to engine power, tapered
  by how much grip is already spent cornering; the "use" button
  (`Input.boost`) is now an instantaneous full-throttle override
  instead. Another AI competitiveness pass. Two more HOLT-tier
  drivers, KESSLER and DUARTE, replacing RENARD and SOLANO. Some AI
  drivers (OSEI, NORDLI, DUARTE) now genuinely hunt the racing line —
  `ai_line_curvature` (game.c/game.h) blends in a second, farther
  curvature sample per driver's `AIDriver.line_lookahead_m`, tapered
  down the tighter the near corner already is. Both the turbo's grip
  taper and the racing line's corner-tightness taper exist because an
  early cut of each briefly broke the same fragile TRUCK/AI_YOLO
  pairing on Monarch and Berthoud Pass 2.0 — see §6 below. A v1.21.1
  follow-up patch added three self-contained pieces: an in-memory
  session-best lap per circuit (`session_best_lap_get`/`_record` in
  `game.c`, deliberately outside `Game` so it survives `game_init`), a
  HUD readout of the next weather zone's condition on the approach to
  it, and the chase camera leaning its aim point toward an upcoming
  bend's curvature (new `cam_corner_lean` setting, `camera.c`).
- **v1.22.0**: `game_init` now reads the difficulty/team/career
  scaffolding from v1.19.0/v1.20.0 instead of ignoring it. A
  `DifficultyPreset` sets the lap count, scales AI `aggression` and
  `ai_skill` together, leans AI car choice toward specs matched to (or
  weaker than) the human's own power-to-weight (`ai_choice_spec`), and
  can force `track.has_walls` on or off. `team_mode`/`team[]` paint
  every human and AI by team (new `Kart.team` field) and
  `game_team_scores` totals a combined score. `career[]` moves grid
  placement off the old fixed human/AI split — `grid_slot_assign` now
  computes the whole grid from a human's recorded `last_finish_rank`
  first, then fills whatever's left exactly as before. `DIFFICULTY_
  NORMAL` was made enum value 0 (it was `EASY`) specifically so every
  existing zero-initialized `GameConfig`, menu paths included, keeps
  behaving exactly as it did before this release. Still no menu control
  sets any of these fields, so nothing changes in the shipped game yet
  — see `TODO.md`.
- **v1.23.0**: a ninth circuit, `TRACK_BULLRING` — a flat, wide,
  barriered oval generated from its own geometry (`CP_BULLRING` in
  `track.c`, a closed loop of two straights and two constant-radius
  turns computed directly rather than hand-placed) instead of drawn by
  eye like every other circuit. New `Track.grandstands` flag drives
  grandstand scenery in `main.c` (`place_scenery`/`draw_grandstands`),
  the same deterministic-placement pattern trees already use but keyed
  to straight sections instead of scattered by a PRNG. Two new cars,
  STOCKER and SLIPSTREAM, tuned for it (see §6 below for why their
  first cut had to be retuned). The corner-easing pass in
  `track_init_with_settings` now blends over a wider 5-point kernel
  instead of 3, for every circuit except MONARCH (see §6). An in-game
  car designer (`kart_spec_validate`/`kart_specs_add_custom` in
  `game.c`, promoted from `config.c`'s file-private `valid_car` so the
  designer doesn't have to reach into the config layer to validate a
  car; `config_write_cars_text`/`config_save_cars_file` in `config.c`
  for the save half) reachable from the main menu as DESIGN A CAR.
  `MAX_KART_SPECS` raised from 16 to 24 for headroom.
- **v1.24.0**: road banking on every circuit — `Track.bank`, derived
  from `curv_signed` in `track_init_with_settings` (a small universal
  `bank_mult` default of 0.3, BULLRING overriding it to 8.0) and fed
  straight into `kart_step`'s cornering grip budget (`mu_a += GRAVITY *
  tanf(fabsf(bank))`) — real grip, not just a rendering tilt (see
  `bank_dy` in `main.c` for the render side). BULLRING's two straights
  doubled (`CP_BULLRING`, 200 m → 400 m each). Cornering physics
  reworked: `TIRE_SHOULDER` (12%) lets a car pushed past the nominal
  grip cap actually turn tighter instead of just clamping; the old
  RWD-only power-oversteer spin trigger is now `torque_spin_risk`
  (RWD at the old thresholds, AWD only past a harder one, FWD never)
  plus a new, drivetrain-blind `speed_spin_risk` for coming into a
  corner too fast to save regardless of throttle — see §6 for how
  fragile these thresholds turned out to be to get right. The car
  designer no longer exposes mass directly — `main.c`'s
  `designer_finalize` derives it from power
  (`DESIGNER_MASS_BASE + power_hp * DESIGNER_MASS_PER_HP`) — and gear
  count plus each gear's own top speed and shift points are now
  directly editable (`RK_DES_GEARCOUNT`/`GEAR_SEL`/`GEAR_TOP`/
  `GEAR_UP`/`GEAR_DOWN`), replacing the old fixed auto-derived ladder.
  `draw_car_model` takes a `const KartSpec *` now and scales body
  length/width/height and driven-wheel size from it. `draw_minimap`
  takes a new `highlight` kart index that draws a pulsing ring around
  one car — wired to kart 0 in the 1-human case only, since the
  3-human shared minimap has no single "the player" to ring.
- **v1.24.1**: three follow-up fixes to v1.24.0's banking. `draw_box`
  gained a `roll` parameter (rotates its already-pitched lateral/up axes
  about the forward axis, same sign convention as `Track.bank`) and
  `draw_kart` feeds it `t->bank[k->seg]`, so a car's model finally banks
  with the road instead of staying dead level — it had yaw and pitch
  before this but no roll at all. Non-alpine tracks (BULLRING, CLASSIC)
  now get a per-segment fill quad from the banked curb edge to the flat
  background ground plane, both sides, closing a gap that used to let
  the ground clip through the road's low (inside-of-bank) edge and
  leave a floating gap past the high (outside) edge — see §6 for why
  this only showed up on flat tracks. And the guardrail/cliff-edge
  drawing block, previously nested entirely inside `if (t->alpine)`, is
  now gated on `t->has_walls` on its own — BULLRING and CLASSIC both set
  `has_walls = 1` but are not alpine, so they had never actually drawn
  a barrier despite the flag; see §6.
- **v1.25.0**: the v1.20.0 power-oversteer/forced-spin mechanic is gone
  from `kart_step` — no more escalating `yaw_cap` bonus or timed forced
  spin from steering alone; understeer scrub and the tire shoulder are
  the whole grip model now, and the handbrake (`k->drifting`) is the
  only deliberate way to break the rear loose. `yaw_cap`'s speed floor
  moved 0.5 → 6.0 m/s and the guardrail collision penalty 2.5 → 1.2 /s
  — see §6, both were load-bearing fixes for a regression the removal
  alone didn't catch. Gearing reworked: `KartSpec.auto_up`/`auto_down`
  (per-gear shift-point fractions) are gone, replaced by a single
  `nominal_rpm` (one engine curve, same peak in every gear) and
  `gear_power_scale_rpm` (game.c) — a real parabola peaking at
  `nominal_rpm`, floored at 22%, hard cliff at the limiter. New
  `KartSpec.aspiration` (NA/turbo/supercharged) changes how
  `k->turbo_spool` builds: turbo unchanged (real lag, biggest bonus),
  supercharged instant and lag-free (smaller bonus), NA always zero.
  `designer_grip()` (main.c) now derives `lat_g` from mass and drag
  instead of a free dial, same treatment mass got in v1.24.0. New
  `RK_EDIT_CAR` row (garage) and `designer_apply_edit`/
  `designer_load_existing` let a player edit any car — including a
  built-in — in the designer, patching `kart_specs[]` in place for the
  session with no disk write. `cooldown_control` now ramps to
  `COOLDOWN_CRUISE_MPS` (~30 mph) and holds it instead of ramping to a
  stop; `Kart.parked` is gone. Every in-game speed/distance/weight
  display converts to mph/feet/pounds at the point it's drawn
  (`main.c`'s `MPS_TO_MPH`/`KPH_TO_MPH`/`M_TO_FT`/`KG_TO_LB`); the
  simulation itself is still entirely SI.
- **v1.26.0**: weather is no longer CLASSIC-only. `track_init` builds
  the same three-zone layout for every circuit now (still just scaled
  by each track's own point count, not a bespoke pass over each one's
  geometry — see TODO.md's "Weather depth" for what that would still
  take). A new `GameConfig.weather` (`WEATHER_TOGGLE_OFF`/`_ON`, off by
  default) decides whether a race actually sees any of it —
  `game_init` blanks every zone back to -1 when it's off, so
  `track_weather_at` has nothing to find regardless of what
  `track_init` built. Wired into the setup menu as a WEATHER row next
  to LAPS (`sel_weather` in main.c). Separately, AI skill now scales
  two things in `game.c` that used to be flat for every driver: the
  rate `k->line_target` eases onto `ai_tactical_line`'s target each
  frame (`line_ease` in the AI branch of `game_update`), and a
  `capitalize` multiplier on the defend/attack tactical-line
  contributions inside `ai_tactical_line` itself. Both read a new
  `ai_skill01()` helper that normalizes `Kart.ai_skill` to 0..1 across
  the roster's practical range. See §6 for why the ranges on both of
  those ended up wide rather than subtle — a narrower attempt broke a
  second, unrelated test.
- **v1.26.1**: a second no-guts-no-glory driver, VOSS (`ai_drivers[]`,
  `game.c`), replaces PETRAN (`AI_BALANCED`, the field's plainest sheet
  and the only one with zero test dependency — see the "replace, don't
  append" note in §6). Same `AI_YOLO` commitment as TANAKA, skill
  pitched on par with DUARTE/IBARRA (well above TANAKA's own 0.95), but
  a lower consistency than TANAKA's 0.48 (0.40) — sends every apex
  anyway and pays for it in falls more often. HOLT, KESSLER, IBARRA,
  DUARTE and CROSS all got a small aggression-up/consistency-down nudge
  toward the same commitment; CROSS's own skill dropped slightly
  (0.92 → 0.89) purely to keep `test_driver_field_has_characters`'s
  skill-spread check (>= 0.15) satisfied once PETRAN — the previous low
  anchor — was gone. BASTIEN and OSEI, the only two drivers that land
  in that same test's "dependable" bin (consistency >= 0.90, aggression
  in [0.40, 0.70]), still moved a little, just kept carefully inside
  both edges of that bin rather than risk falling out of it. NORDLI and
  DELGADO, the field's two cautious CRUISERs, are untouched by design —
  see the "let me pick" exchange in the commit that shipped this: the
  whole batch came from a short back-and-forth about exactly this kind
  of roster call, not a one-shot judgment.

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

Count matching is not the same as the *fields* matching, though — see
`test_compiled_roster_matches_cars_json` (added v1.19.2 after
`awd_front_bias` quietly drifted between the two rosters for every
FWD/RWD car: harmless because nothing reads that field off an AWD car,
but exactly the kind of one-field drift that would matter for a stat
that isn't gated the same way). That test diffs every field of every
car between the two rosters and is the one to run after any cars.json
edit, not just the count check.

**`DIFFICULTY_EASY` is not 0 any more.** v1.22.0 reordered the
`DIFFICULTY_*` enum so `DIFFICULTY_NORMAL` is value 0 and `EASY`/`HARD`
shifted to 1/2 — `difficulty_presets[]` was reordered to match (index ==
enum value). This was deliberate, not a refactor: `game_init` reads
`GameConfig.difficulty` now, and every existing zero-initialized
`GameConfig` had to keep meaning "today's behavior, unchanged" instead of
silently landing on EASY. Any code that compared `cfg.difficulty` against
a literal `0`/`1`/`2` instead of the enum names would now be reading the
wrong preset — search for the symbolic names, not the numbers.

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

**A steering-derived quantity is not car-independent just because the
formula looks symmetric.** `yaw_cmd = v * tan(steer * delta_max) /
wheelbase` scales as `1 / wheelbase`, so the same `steer` value means
wildly different things car to car: RACER's 1.05 m wheelbase means even
`steer = 0.3` can already exceed a bonused `yaw_cap`, while a long
car's `yaw_cmd` stays well under it at full lock. v1.20.0's power-
oversteer mechanic first judged whether a slide had been "caught" by
comparing `yaw_cmd` against `yaw_cap` directly — which meant a
short-wheelbase kart could never catch one (it would have to release
the wheel almost entirely) while a long-wheelbase one caught trivially
every time, an unintentional and very car-dependent difficulty spike.
The fix judges risk and catch on the raw steering input magnitude
instead (`fabsf(steer) > 0.6`), which means the same fraction of lock
means the same thing in every car; `yaw_cmd > yaw_cap` is still used,
but only as the initial "is this car actually cornering hard" gate.
`test_oversteer_never_triggers_when_driving_gently` is the regression
test — if a future change to the steering model reintroduces a
car-dependent comparison here, that test is where it will show up
first, likely on whichever car has the shortest wheelbase.

**When writing a host test for AI behaviour on a rear-driven car, pick
the spec deliberately or the power-oversteer mechanic will confound
whatever you're actually trying to measure.** Any AI test that puts a
kart under throttle through a hard corner on a car with `rwd_bias >
0.5` (i.e. anything not close to 50/50 AWD or FWD-leaning) can trigger
a genuine spin as a side effect, which pins `slip` at `1.0` and
swamps whatever more specific effect the test was built to isolate —
this happened while first writing `test_ai_weather_awareness`, which
does not care about oversteer at all. TOURER (`front_bias = 0.50`,
`rwd_bias` exactly `0.50`, not `> 0.50`) is the deliberate escape
hatch: assign a test kart to it explicitly when the thing under test
is unrelated to oversteer/spin behaviour.

**CROSS, driving TRUCK, on Monarch or Berthoud Pass 2.0, has essentially
zero safety margin — treat it as the canary for any change to engine
power, AI cornering speed, or AI line selection.** Car spec assignment
for the AI is purely positional (`ai_no % kart_spec_count` in
`game_init`, where `ai_no` is a driver's index into `ai_drivers[]`), so
whichever driver sits at array index 9 is always paired with `TRUCK`
(index 9 in `cars.json`: the heaviest car, the worst grip in the
roster, the longest braking distance) — currently CROSS, on the
`AI_YOLO` sheet (highest aggression, lowest consistency of any sheet).
That pairing, on the field's two tightest and most technical circuits,
came within a hair of an unrecoverable fall/respawn loop (v1.21.0's
turbo rework) and a pinned-against-the-barrier stall (the same
release's racing-line lookahead) from otherwise-reasonable,
well-intentioned tuning changes that never touched CROSS, TRUCK, or
YOLO directly — a modest global engine-power bonus, or a several-meter
lean toward a different apex on a completely different driver's line,
was enough to tip it over in each case. `test_ai_races_all_tracks`'s
per-circuit finish check is what catches this every time; if a future
change to power delivery, braking, or line selection makes that test
start timing out on Monarch (track 4) or Berthoud Pass 2.0 (track 7),
suspect this pairing before anything else, and look for a way to taper
the new effect (by slip, by corner severity, by whatever is actually
relevant) rather than just turning its magnitude down — a flat
reduction big enough to save this one pairing tends to blunt the
feature everywhere else it was never actually a problem.

**It isn't only CROSS+TRUCK — any AI-driven car powerful enough,
relative to its own grip, can hit the same wall on MONARCH.** Tuning
STOCKER for v1.23.0 (620 hp, 1.20 g) put whichever AI got assigned it
into an unrecoverable fall loop on MONARCH's tightest hairpin (a
scratch harness overriding `Kart.spec` directly — the default 11-AI
roster never actually reaches spec index 11+ via `ai_no %
kart_spec_count`, so this cannot happen in a normal race today, only
under a difficulty preset's `ai_choice_spec` pooling by
power-to-weight, or a human driving it there themselves, where it's
just a hard car to drive rather than an AI stuck in a loop). Trading
power for grip fixed it (620 hp/1.20 g → 540 hp/1.38 g) without giving
up the car's edge on BULLRING — measured faster there than before, not
slower, because the extra grip helps in the sweepers more than the
lost power hurts on the straights. Confirmed on MONARCH, BERTHOUD 2.0
and GUANELLA — the same three the CROSS+TRUCK note above already
flags — before it shipped. Any future high-power car needs the same
check: override an early AI slot's spec to it and run it round
MONARCH before trusting `test_ai_races_all_tracks`'s default-roster
pass to have exercised it at all.

**MONARCH did not get the wider corner-smoothing kernel v1.23.0 gave
every other circuit.** `track_init_with_settings` blends position
over a 5-point neighborhood now instead of 3 (softer entry/exit on a
tight bend without opening out the apex), gated on `track_id !=
TRACK_MONARCH`. The wider blend perturbed MONARCH's own tightest
hairpin just enough to strand an AI on the YOLO sheet in the same kind
of fall loop as the paragraph above, for the same underlying reason:
MONARCH already has close to zero margin, so anything that moves its
geometry at all is worth a full `test_ai_races_all_tracks` pass before
it ships, not just a look at the diff.

**A new spin trigger has to be gated on steering, not just speed and
curvature demand — `yaw_cmd / yaw_cap` alone grows unbounded with
speed for any nonzero steering input.** `yaw_cap` (`kart_step`) is
`mu_a / v`: it shrinks as speed rises, while `yaw_cmd` at a *fixed*
steering fraction grows roughly with `v`, so their ratio grows with
`v` squared for literally any nonzero steer. v1.24.0's first cut of
`speed_spin_risk` (a new, drivetrain-blind "came in too hot" spin
trigger) checked only `v > 22` and `yaw_cmd > yaw_cap * 1.6` — no
steering floor — and it fired on a car doing nothing more aggressive
than a 0.2 steering input at cruising speed, because at that speed the
ratio clears 1.6 on its own. It surfaced as `test_oversteer_rewards_a_
catch_and_punishes_a_miss` failing: an RWD car "caught" (steering
eased to 0.2) kept building oversteer through the entire 60-frame
catch window and spun anyway, because the new trigger, not the old
torque one, was still firing. Fixed by adding a real steering floor
(`fabsf(steer) > 0.5f`) to `speed_spin_risk` — any future speed-based
trigger needs the same floor, or a low-speed, low-steering "catch"
will stop working the same way.

**The tire "shoulder" (`TIRE_SHOULDER`, `kart_step`) needs slip
measured against the *original* `yaw_cap`, not the widened one, or the
progressive understeer curve collapses.** The shoulder's whole point
is letting `yaw` reach `yaw_cap * TIRE_SHOULDER` instead of clamping
at `yaw_cap`, so a car pushed a little past the old limit genuinely
turns tighter. The first cut also computed `k->slip` as a fraction of
the *shoulder's width* (`yaw_cap * (TIRE_SHOULDER - 1)`, about 12% of
`yaw_cap`) instead of `yaw_cap` itself — a much narrower denominator —
so both a mild oversteer input and a full-lock one saturated `slip` to
1.0 almost immediately, and `test_understeer_scrub_is_progressive`
(which specifically checks a mild case stays mild) failed. `yaw`
(how much the car actually rotates) and `k->slip` (how severe the
scrub penalty is) are clamped against two different limits on
purpose: the shoulder governs the first, the original `yaw_cap` still
governs the second.

**RWD's power-oversteer reward margin over FWD got diluted, not
erased, by the tire shoulder above — the fix was to strengthen the
reward, not to leave the test where it was.**
`test_oversteer_rewards_a_catch_and_punishes_a_miss` compares total
heading rotated over a fixed 1.5 s (0.5 s building oversteer, 1.0 s
easing off/"catching" it) between an RWD car and an otherwise-identical
FWD one. The shoulder change lifts *both* cars' rotation over that
long catch tail, since FWD also benefits from it during its own clean
cornering, which shrinks RWD's relative share of the total almost
regardless of how large `oversteer_max_bonus` gets — the reward window
(0.5 s) is short next to the tail (1.0 s) that dilutes it. Raising
`oversteer_grow_rate` (0.70 → 1.00) so the bonus reaches its ceiling
*sooner* within that 0.5 s, and `oversteer_max_bonus` itself (0.35 →
0.40) recovered a real, reliable margin; the test's own threshold
(the old code needed >5%) was loosened to >3% to match — both moves
together, not the test change alone, since a threshold that no
implementation could reasonably clear is not actually testing
anything.

**Growing `ai_drivers[]` past `NUM_KARTS - 1` (11) entries silently
adds unreachable drivers, not new ones — replace, don't append.** Car
and driver identity for the AI is assigned by `ai_no = i -
n_humans`, and `ai_driver(ai_no)` indexes `ai_drivers[ai_no %
ai_driver_count()]`. With a single human, `ai_no` only ever runs 0..10
(11 AI grid slots, the maximum there is room for), so if the array has
more than 11 entries the modulo never actually wraps for any of them —
entries at index 11 and beyond are simply never selected in the
common single-human case, however many humans besides that one drop
the AI slot count even further. `test_driver_field_has_characters`
does not catch this: it only checks `ai_driver_count() >= NUM_KARTS -
1` and then iterates every entry in the array by index, so a
dead-in-practice 12th or 13th driver still reads as present and
correctly shaped to that test. Adding KESSLER and DUARTE in v1.21.0
meant replacing two existing entries (RENARD and SOLANO, chosen after
checking which named/strategy dependencies in `tests/test_game.c`
each one alone was load-bearing for) rather than appending, to keep
the roster at exactly 11 and guarantee the new characters actually
show up in a normal race.

**A `TrackDef` flag with no effect will not fail any test, because
`main.c`'s rendering has no host tests at all.** BULLRING and CLASSIC
both had `has_walls = 1` from the moment they were added, and the field
genuinely does something on the physics side (`track_wall_half`,
collision). But in `draw_track` the entire guardrail/cliff-edge block
was nested inside `if (t->alpine)`, and neither track is alpine — so
`has_walls` had zero visual effect on either one for multiple releases
and nothing ever caught it, because `main.c` is Wii-only GX code with
no host build to assert against. The only way this kind of bug
surfaces is a human actually looking at the rendered track (or reading
`draw_track` line by line with the specific question "does this flag
actually reach a `quad()` call"). Worth doing that read whenever a new
`TrackDef` boolean is added: check that every branch which claims to
gate on it is actually reachable for the combination of flags a real
track entry sets, not just the combination the code was written and
tested against first.

**A background quad drawn at a fixed height stops being safely "below
everything" the moment something above it can tilt.** The valley-floor
quad's height was chosen when every road surface was flat (or only
lightly canted) — 0.02 m below nominal was plenty of clearance. Adding
`bank_dy` in v1.24.0 let the road surface itself move up to roughly
`road_half * sin(BANK_MAX)` (about 1.6-2 m on BULLRING's 7.5 m road
half at the 12.6° cap) below that nominal height on the inside of a
turn, and by the same amount above it on the outside — both comfortably
past the old margin, so the flat ground either clipped through the low
edge or left a visible gap past the high one. This only showed up on
non-alpine tracks: alpine ones already drop their equivalent background
(the rock/scrub skirt geometry, not this quad) 7-22 m below road height,
miles clear of anything a `BANK_MAX`-capped tilt can reach. The fix
(the per-segment fill quad in `draw_track`'s `else` branch, keyed off
`bank_dy` at the *same* lateral offset the curb quad above it uses) is
the general pattern for closing this kind of gap: connect the moving
edge to the fixed background at the exact point they need to meet,
rather than trying to push the fixed background far enough away to
never need to think about it again — the latter works until the next
thing that moves more than expected.

**`yaw_cap = mu_a / v` diverges as v shrinks, and a car pinned against a
guardrail under sustained understeer will shrink v a lot.** Removing
v1.20.0's forced-spin mechanic (v1.25.0) was not, by itself, enough to
fix the "holds full lock and loses it" report: a synthetic test that
held full steering lock and full throttle for 2 seconds still showed
yaw rate climbing (0.45 → 0.91 rad/s) and speed collapsing (25 → 4.5
m/s) with the mechanic gone entirely. The actual sequence: full lock
understeers the car wide, it runs off the road into the guardrail
(`kart_step`'s wall-collision branch), gets pinned there (`lat` clamped
to `wall_half`), and the guardrail's own per-frame speed decay
(originally `1 - 2.5*dt`) bleeds speed toward zero while pinned. As v
shrinks, `yaw_cap` (grip-limited turn rate) grows without bound, since
nothing physically stops a `1/v` term from diverging — so the car reads
as spinning faster and faster the slower it gets, which is exactly
what an ever-tightening, out-of-control spin looks like, with no
"spin" mechanic involved at all. Two independent fixes were both
needed: the guardrail decay softened to `1 - 1.2*dt` (still a real
penalty, not a runaway one), and `yaw_cap`'s divisor floored at 6 m/s
instead of 0.5 (below a walking pace, steering geometry — not grip — is
what actually limits a real car's turn radius, so letting the "grip
cap" keep growing there was never physical to begin with). Test with a
scratch harness that holds an adversarial input for *several* seconds,
not one frame — a one-frame check would never have caught this, since
the divergence only shows up once the car has had time to actually
scrub down.

**A power curve with a hard floor needs the floor high enough to
launch, or "more realistic" reads as "broken."** The first cut of
`gear_power_scale_rpm` (v1.25.0) used a floor of 0.05 and a narrow
peak window (`width = ... * 0.6`/`0.55`) around `nominal_frac`. That
technically satisfied "falls off precipitously," but it also meant a
car starting from a dead stop — `rev_frac` near 0, far from
`nominal_frac` — made only 5% power, and every AI car's launch,
`test_gearboxes_sane`'s "does the automatic climb the box" check, and
the AWD-vs-RWD traction-ratio test all broke at once, none of them
about gears on their own. The tell was a debug harness showing a car's
speed rising for a while under full throttle, then suddenly collapsing
and settling into a permanent ~2.5 m/s crawl — the same guardrail-pin
spiral above, just triggered by the engine being too weak to actually
climb through its gears before running off the road. The fix was two
changes together: raise the floor to a value that still lets a car
launch and limp (0.22, roughly what a real idling engine still makes),
and — the one that actually mattered most — make `width` span
`nominal_frac`'s *entire* distance to idle (0) or redline (1) rather
than a fraction of it, so the curve is one continuous parabola with no
flat "dead zone" pinned at the floor across a wide low-RPM range. Both
were needed: floor alone still left a wide, weak plateau; width alone
would have let the floor go to zero. When tuning any curve like this,
check it against a full launch-from-a-stop, not just a few isolated
sample points — a curve that looks right sampled at 0.3/0.7/0.97 can
still be unworkable at the one point (a dead stop) every race actually
starts from.

**Any change to `k->line_target`'s per-frame dynamics ripples into
tests that have nothing to do with lines.** Scaling the ease rate
`line_target` moves toward `ai_tactical_line`'s target, and the
`capitalize` multiplier on that function's defend/attack terms, by
skill (v1.26.0 — see `ai_skill01`) broke `test_ai_learns_from_mistakes`
for CHARGER specifically: IBARRA (the only CHARGER driver, skill 1.02)
went from comfortably clearing "learned nerve dropped at least 0.02
from its overconfident start" to landing at a 0.002 drop, because
slightly tighter/more-committed tactical positioning was enough to
keep it off CLASSIC's guardrails a little more often, and CLASSIC's
corners are mild enough that this nearly eliminated its mistake count
for that one 150-second race. The instinct to fix this by narrowing
the skill ranges (assuming the wide ones — `capitalize` 0.55..1.20,
`line_ease` 1.1..2.6 /s — were simply too strong) made it *worse*: a
narrower attempt (`capitalize` 0.75..1.15, `line_ease` 1.5..2.1)
introduced a second, unrelated failure in the tire-compound sprint
test while still failing the original one. Verified with a `git stash`
round-trip against the pre-change commit that the CHARGER test really
did pass cleanly with neither change present, and that disabling
either one alone (leaving the other active) still failed it almost
identically — the two are not independently responsible, and the
system is too chaotic (wall-collision thresholds, per-corner learning
clamps) to reason about by shrinking magnitudes. The actual fix was to
keep the wider, more meaningful ranges and rewrite the CHARGER-specific
assertion to check what's still honestly true under the new model — it
hit at least one real mistake, and its confidence never net *increased*
past its starting point — rather than a fixed amount of ground given
back that this specific driver, on this specific track, no longer
reliably concedes. Lesson: when a deliberate behavior change perturbs
an existing test's tight numeric margin, check whether the qualitative
claim still holds before either reverting the change or chasing the
threshold with smaller and smaller magnitudes — on a system this
interconnected, "smaller" is not reliably "safer."

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
centerline on its own; the defend/attack terms layer on top (as of
v1.26.0, scaled by skill too — see `ai_skill01`/`capitalize`, above in
§5). `test_racing_line_pace_is_not_the_sheet` (`tests/test_game.c`)
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

### 7e. Winter variants, snow and ice — done in v1.19.0, extended to every circuit in v1.26.0

This predates the section even being written: v1.19.0 already gave
CLASSIC a per-segment surface multiplier (`Track.weather_zone`,
`track_weather_at`) that costs grip by tire compound
(`weather_tire_grip_mult`) and drags on top of that once it's standing
water (`weather_puddle_drag_mult`), exactly the mechanism this used to
propose building. v1.26.0 extended `track_init`'s zone layout to every
circuit and added `GameConfig.weather` so a race can turn it on or off
instead of CLASSIC always having it forced on. What's still genuinely
open, if this gets revisited: each circuit reuses the same generic
three-zone layout scaled by point count rather than a bespoke pass over
its own geometry, there's no deeper dry/snow/ice tire trade beyond the
one grip multiplier, and `draw_track` still only tints a zone rather
than giving it a visible boundary — see TODO.md's "Weather depth" for
the specifics.

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
