# Changelog

Notable player-facing and development changes are recorded here. Release
artifacts and their longer descriptions remain available on the
[GitHub releases page](https://github.com/Nessie404/starwarsgame/releases).

## [1.24.0] - 2026-08-24

### Added

- **Road banking on every circuit.** Every track's curves now cant into
  the turn — a small, realistic crown on the mountain passes, and a
  real (if gentle) bank on BULLRING's own turns, both derived directly
  from curvature so they ramp in and out with the bend rather than
  switching on at its edges. It is not just cosmetic: a banked corner
  genuinely lends the tires extra cornering grip, the way a real banked
  turn does.
- **BULLRING's straights, doubled.** 200 m each to 400 m each — a short
  track grown into a proper speedway, now with real banking in its
  turns to go with the extra room on the straights.
- **A rebuilt car designer.** Gear count is now a control, not fixed at
  five; every gear's own top speed and upshift/downshift point is
  directly, granularly editable instead of a single auto-derived
  ladder. Mass is no longer a free dial: it now follows power directly
  (more engine costs more weight, the way it does in a real car), so a
  design can no longer have a superkart's power-to-weight and a truck's
  raw horsepower at the same time.
- **Cars look like their numbers.** A car's on-track (and in-garage)
  model now varies with its spec instead of one fixed shape repainted —
  a long wheelbase reads as a long car, more mass as a broader one, more
  drag as a taller, boxier one, and the driven axle carries visibly
  bigger tires.
- **Your car, obvious on the minimap.** In single-player, your own dot
  now carries a pulsing white ring so picking it out of eleven others
  is instant, not a squint at colours mid-corner.

### Changed

- **Cornering physics reworked: cars hold a line much longer before
  actually losing it.** A tire's grip does not end at a hard wall —
  past the nominal grip limit there is now a "shoulder" where a car
  pushed harder genuinely turns tighter instead of just being clamped,
  before it truly lets go. Losing it outright (a real spin, not just
  understeer) now has two distinct, deliberate causes instead of one:
  big torque on lock in a tight turn (rear-driven and, with a harder
  commitment, all-wheel-drive cars only — front-driven cars push wide
  instead, they do not spin under power), or carrying too much speed
  into a corner for any drivetrain to save.

## [1.23.0] - 2026-08-23

### Added

- **BULLRING**, a ninth circuit: a flat, wide, barriered oval generated
  from its own geometry (two straights, two constant-radius sweeping
  turns) rather than hand-drawn, so it has no abrupt curves anywhere on
  the lap. Grandstands run the length of both straights.
- **STOCKER and SLIPSTREAM**, two new oval-specialist cars tuned for
  BULLRING — huge power and low drag for the straights, with enough
  grip to actually hold a sweeping turn without banking to lean on.
  Genuinely competitive with the roster's other big-power cars on the
  new circuit.
- **An in-game car designer**, reachable from the main menu: dial in a
  name, mass, power, brakes, grip, drag, dirt grip and drivetrain, with
  a live preview of the derived top speed and 0-100 time, then save it
  into the garage for the rest of the session — and to `cars.json`
  itself, wherever it was actually found, so it's still there next
  time.

### Changed

- **Smoother corners on every circuit but MONARCH.** The corner-easing
  pass in `track_init` now blends over a wider neighborhood, softening
  how sharply curvature ramps into and out of a bend, without eroding
  a genuinely tight apex — Berthoud 2.0's and Guanella's hairpins are
  still real hairpins. MONARCH keeps the original, narrower pass: it's
  the one circuit where the wider blend measurably hurt, stranding an
  AI driver in a fall loop on its tightest hairpin under the YOLO
  strategy sheet.

## [1.22.0] - 2026-08-22

### Added

- **`game_init` now reads the difficulty, team and career scaffolding**
  added back in v1.19.0/v1.20.0. A difficulty preset sets the lap count,
  scales AI aggression and skill together, leans AI car assignment
  toward cars matched to (or weaker than) the human's own, and can
  force a circuit's guardrails on or off. Team mode paints every human
  and AI car by team and totals a combined per-team score. Career mode
  starts a human at the grid slot matching their last race's finish
  instead of always at the back. There is still no menu control that
  sets any of this, so a race today is unaffected either way — see
  `TODO.md` for what's left (the garage menu itself, a team-score HUD
  element, and save/load for career progress).

## [1.21.0] - 2026-08-21

### Changed

- **Boost reworked into an automatic turbo.** The old meter-you-charge-
  and-spend mechanic is gone. A turbo now spools up entirely on its
  own while genuinely on the throttle at real revs (same curve as
  before: time near the limiter counts for far more than the same time
  low in the band), bleeds off on its own the instant you lift or
  brake, and applies straight to engine power every frame it's on the
  gas — nothing to save up, nothing to spend. A quick gear change
  holds the spool steady rather than wiping it, so short-shifting
  through a band is no longer punished. The bonus is tapered by how
  much grip is already spent cornering (full effect in a straight
  line, next to none mid-slide), the way a traction control would back
  off boost rather than piling more power onto tires already at their
  limit. The "use" button does something different now: it's an
  instantaneous full-throttle stab — for as long as it's held, it's
  exactly as if the gas pedal were on the floor and the brake
  untouched, whatever those two are actually doing. Tunable in the
  renamed `turbo` block of `settings.json` (was `boost`).
- **AI competitiveness, pushed further, again.** `skill_multiplier` up
  from 1.02 to 1.06 and `braking_multiplier` up from 0.72 to 0.76, so
  the field corners and brakes closer to the real limit.

### Added

- **KESSLER and DUARTE**, two more genuinely hard-to-beat drivers,
  replacing RENARD and SOLANO in the eleven. KESSLER is cut from the
  same cloth as HOLT — late braking, rides the limiter, commits and
  rarely pays for it, arguably a little more consistent than HOLT if
  anything. DUARTE is a different problem: a metronome who takes the
  tightest line on the track lap after lap and essentially never puts
  a wheel wrong.
- **Some AI drivers now hunt the real racing line.** OSEI, NORDLI and
  DUARTE read past the corner they're already in when deciding their
  line — the reason to run wide into one bend is often the shape of
  the next one — each reading a different distance ahead
  (`line_lookahead_m` per driver), so they feel like different people
  finding the line rather than one setting worn by everybody. That
  anticipation fades on its own the tighter the near corner already
  is: a driver mid-hairpin is committed to that corner, not still
  weighing what comes after it.
- **Session-best lap per circuit.** Not saved to disk — it lives only
  as long as the game stays open — but every human's best lap on each
  circuit now survives from one race to the next within that session,
  shown next to the current race's own best on the HUD.
- **A HUD readout for the weather ahead.** On the approach to a snow,
  ice, or puddle patch, the HUD now names the condition coming up, not
  just the color change on the road surface itself.
- **The chase camera leads into corners.** The aim point now leans
  toward the signed curvature of the road at the look-ahead point
  instead of only ever pointing along the car's nose, so a corner
  starts framing itself before you turn in. Eases off the tighter the
  swing is already round for a reverse, and hard-clamped so a hairpin
  can't send the aim point somewhere absurd. Tunable via the new
  `corner_lean` key in the `camera` block of `settings.json`.

### Fixed

- Two host-testable safety margins that a driver in an unrecoverable
  fall/respawn or pinned-against-the-barrier loop on Monarch or
  Berthoud Pass 2.0 exposed while the above two features were being
  tuned: the turbo's grip-based taper (above) and the racing line's
  corner-tightness taper (above) both exist because an early cut of
  each briefly broke the same fragile TRUCK/AI_YOLO pairing on the
  circuit's tightest point. `test_ai_races_all_tracks` is what caught
  both.

## [1.20.0] - 2026-08-20

### Added

- **Oversteer and understeer are both far more punishing/rewarding.**
  Understeer now scrubs speed progressively — `scrub * (1 +
  scrub_curve * slip) * mu_a * slip * dt` instead of the old flat rate
  — so a corner taken barely too hot just runs a little wide, one taken
  way too hot really pays for it. Rear-driven cars get a brand new
  power-oversteer mechanic on top of that, separate from the existing
  handbrake drift: committing hard to a corner on the throttle
  (`|steer| > 0.6`, genuinely cornering past the grip limit) builds
  extra rotation for free the longer it's held; ease off in time and it
  settles back down having gained real rotation over an identical
  front-driven car, but hold it past `oversteer_spin_seconds` (default
  1.0 s) and the car spins — losing real speed and control for
  `spin_seconds` (default 0.6 s) before it's driveable again. Tunable
  in the new `understeer`/`oversteer` blocks of `settings.json`.
- **AI competitiveness, pushed further.** `skill_multiplier` up from
  1.02 to 1.06 and `braking_multiplier` (how much of the theoretical
  maximum deceleration the AI trusts itself with) up from 0.72 to
  0.76, so the field corners and brakes closer to the real limit.
- **Team mode, colour-based — SCAFFOLDING ONLY.** `TeamDef`,
  `team_defs[]`, `team_name()`, and `GameConfig.team_mode`/`team[]` in
  `game.h`/`game.c` describe four teams, each identified by a paint
  colour, that a future garage choice would group humans and AI onto.
  Nothing reads `team_mode` yet, so setting it today has no effect on
  a race — see `TODO.md` for what real wiring needs (a menu control, a
  combined team score, AI team assignment, and a decision on whether
  team mates get any in-race awareness of each other).
- **Career/campaign mode — SCAFFOLDING ONLY.** `CareerState`,
  `career_record_result()`, and `GameConfig.career[]` describe a
  human's grid slot for the next race defaulting to where they
  finished the last one, instead of always starting at the back.
  Nothing persists this to disk and `game_init` does not read it yet
  — see `TODO.md` for the save/load I/O, grid-slot logic, and campaign
  menu flow a real implementation still needs.
- **Weather, fleshed out further.** Standing water now drags at every
  car regardless of tire choice (`weather_puddle_drag_mult`, default
  1.12, on top of the existing per-tire grip trade), so a puddle costs
  top speed as well as cornering grip. The AI's own corner-speed
  lookahead now discounts grip for whatever weather patch is ahead of
  it — on CLASSIC, it slows for an upcoming snow/ice/puddle segment
  the same way it already slows for a corner it has learned to
  respect, instead of assuming dry pavement everywhere.

## [1.19.2] - 2026-08-20

### Fixed

- **Compiled roster / `cars.json` field drift.** The compiled fallback
  roster (`default_kart_specs`/`kart_specs` in `game.c`, used whenever
  there's no SD card for `cars.json` to be read from) hardcoded
  `awd_front_bias = 0.0` for every FWD/RWD car, while the JSON parser
  fills in `0.5` as the default for a car whose `"drivetrain"` block
  doesn't specify `front_bias`. Harmless today — the field is only
  read for AWD cars — but it was real drift between the two rosters of
  exactly the kind that caused the v1.18.0 missing-cars bug, just in a
  field that happened not to matter yet. Fixed, and a new
  `test_compiled_roster_matches_cars_json` now diffs every field of
  every car between the two rosters (not just the car count) so a
  future edit to one side without the other fails loudly instead of
  waiting to matter.

### Clarified

No code change, but worth stating plainly since it comes up: **there
is no turbocharger/supercharger system.** WiiKart had one (`aspiration`
in `cars.json`, added v1.15.0) and retired it completely in v1.16.0 —
no `"turbo"`/`"supercharged"` field exists in `cars.json` any more.
The `boost` feature added in v1.19.0 is unrelated: a universal meter
(not a per-car property) tuned in the `boost` block of
`settings.json`, not in `cars.json` at all. There is exactly one
`cars.json` in the project, at `config/cars.json`; the compiled
fallback roster in `game.c` is not meant to be hand-edited by players
and exists only so the garage isn't empty when there's no SD card to
read the real file from.

## [1.19.1] - 2026-08-20

### Fixed

- **Berthoud Pass 2.0's real switchbacks are back.** v1.17.0 replaced
  the circuit's actual hairpin stack — climb, summit, valley loop-back,
  climb home — with a smooth sine-wiggle shape to make a plan-view
  self-intersection check pass, which fixed that check at the cost of
  the track's whole character. It turns out the "self-intersection"
  was never an actual gameplay bug: `track_locate` is always called
  with a windowed hint during driving and grid placement, so two
  switchback tiers landing close together in plan view (at very
  different elevations, the way a real mountain switchback stack
  looks from above) was never confusable at the wheel — only
  decorative tree placement uses a global search, and getting that
  occasionally wrong is invisible. The original v1.15.0 control points
  are restored, scaled up further (1.30 → 1.50) for more room
  everywhere while keeping the real elevation profile's ~85 m of
  climb. 36 corners, down to an 8 m hairpin radius, versus the
  sine-wave version's 16-20 corners with nothing tighter than 37 m.
  `test_berthoud2_keeps_its_switchbacks` guards against this happening
  again.

## [1.19.0] - 2026-08-20

### Added

- **Weather (CLASSIC only).** Three patches of road each start as snow
  at the green flag, melt into ice, and melt again into a puddle —
  independently staggered so the whole track is never in lockstep.
  Soft tires are the ones to have in snow or ice and the worst choice
  once it's a puddle; hard tires are exactly the reverse; medium is
  deliberately never the best or worst pick either way. Tunable in the
  new `weather` block of `settings.json`; rendered as a road-surface
  color change. `track_weather_at` (`track.c`) resolves a segment and
  race clock into the current condition.
- **Boost.** A universal meter that charges while the engine is
  turning real revs, on a curve (quadratic in `rev_frac`, so time near
  the limiter counts for far more than the same time low in the band),
  spent all at once on a new `boost` input for an instant speed bump,
  and wiped by the next gear change of any kind — working it means
  holding a gear on purpose. New default bindings: F/H on the two
  keyboard players, GameCube/Xbox Y, Minus on a bare Wii Remote or
  Classic Controller. The AI uses it too, gated on real headroom
  before the next corner. Tunable in the `boost` block of
  `settings.json`. (WiiKart shipped an engine-trait boost system
  through v1.14–v1.15 and retired it in v1.16; this is an unrelated,
  simpler mechanic built from scratch.)
- **AI_YOLO**, a "no guts, no glory" strategy sheet — the highest
  overconfidence and attack rating and the lowest defend rating in the
  roster, riding every gear to the limiter. Assigned to TANAKA and
  CROSS. The AI field's overall skill multiplier is up (0.97 → 1.02),
  and the weakest couple of drivers got a modest skill bump so they're
  no longer plain slow, while HOLT and PETRAN keep the roster's real
  top and bottom.
- **Difficulty preset scaffolding.** `DifficultyPreset`,
  `difficulty_presets[]` and `GameConfig.difficulty` describe what an
  Easy/Normal/Hard menu choice would eventually set — lap count, AI
  aggressiveness, AI car choice, guardrails — as one step instead of
  by hand. Deliberately not wired into `game_init` or anywhere else
  yet; see `TODO.md` for exactly what real wiring needs.

### Changed

- **Corners softened, pavement brought to the guardrail.** Every
  barriered track's shoulder — the gap between the paved edge and the
  guardrail, previously up to 8 m of only lightly-penalized dirt —
  shrunk to about a 1.4 m curb, closing off the "run wide and cut the
  corner" line. Berthoud, Loveland, Monarch, and Guanella are scaled
  up 8–30% to open out their tightest turns without hand-editing their
  authored shape; Berthoud's summit switchbacks — its tightest corners
  and the closest the road comes to itself anywhere on the lap —
  specifically benefit. (A position-smoothing filter was tried first
  and discarded: it shrank every track and made Guanella's chained
  switchbacks tighter, not gentler.)

## [1.18.0] - 2026-08-20

### Added

- **Drivetrain**: every car is now front-wheel, rear-wheel, or full-time
  all-wheel drive, set from an optional `"drivetrain"` block in
  `cars.json` (`config/README.md` documents it; a car with no block is
  RWD, matching every pre-1.18.0 car). AWD also takes a `front_bias`
  (0–1) for how the drive splits between the axles. AWD cars get off
  the line faster than an FWD or RWD car with otherwise identical
  stats, because splitting the acceleration traction demand across two
  axles leaves more of the grip circle free; a front-driven car
  understeers a little more once back on the throttle, a rear-driven
  one gets correspondingly looser and easier to rotate. RALLY, TOURER,
  BUGGY, and TRUCK are AWD; RUBY and WAGON are FWD; the rest stay RWD.
  Covered by `test_drivetrain_traction`, `test_drivetrain_cornering_balance`,
  and `test_drivetrain_json_parsing` in `tests/test_game.c`.

### Changed

- **More grip, later breakaway.** Every car's `lateral_grip_g` is up
  about 25% across the board, and the understeer scrub that follows
  once a corner is driven past that limit is gentler (was pulling
  0.55/0.25 g out of the car per second of overcooked corner, now
  0.45/0.17), so grip loss reads as a progressive scrub deeper into a
  turn rather than an abrupt wall right at the edge.

### Fixed

- **New cars actually reach the garage now.** `main.c` falls back to a
  compiled-in roster (`default_kart_specs` in `game.c`) whenever there
  is no SD card for `cars.json` to be read from — the common case when
  a release DOL is opened directly in an emulator. That compiled
  roster still only had the original four cars, so RUBY (v1.15.0) and
  BUGGY/WAGON/FORMULA/TRUCK/HERITAGE/MUSCLE (v1.17.0) were invisible
  outside a setup with a virtual SD card, even though `cars.json`
  itself was correct and had been shipping in every release zip since
  they were added. The compiled roster now mirrors `cars.json` exactly
  (11 cars); `DEFAULT_SPEC_COUNT` moves from 4 to 11.
- **RUBY's gearbox could strand it in first gear.** Its `cars.json`
  gear ladder jumped from a 15 km/h first-gear limiter straight to 35
  km/h — a 2.33x ratio, versus 1.65–1.75x for every other car — which
  put RUBY right on the edge of the AI gearbox's anti-hunting margin
  for any driver with a `shift_down_frac` of 0.37 or higher. Those
  drivers could get stuck unable to satisfy the upshift condition and
  crawl the entire race at the gear-1 limiter. Never seen before
  because RUBY was never actually assigned to an AI driver until the
  compiled-roster fix above put every car into play. Regeared to the
  same decreasing-ratio ladder as TOURER (its 310 hp, 6-speed
  equivalent), scaled to the same 200 km/h top speed.

## [1.17.0] - 2026-08-20

### Added

- **Six new cars**: BUGGY (620 kg dune buggy, 0.85 dirt grip — the best
  off-road of the roster), WAGON (1550 kg practical all-rounder),
  FORMULA (720 kg open-wheel track car, 1.45 g lateral grip and 28 m
  brakes — both the best in the garage — but only 0.10 dirt grip),
  TRUCK (2100 kg pickup, 0.78 dirt grip, 48 m brakes), HERITAGE (890 kg
  vintage roadster, 85 hp and drum-brake-slow 45 m stops), and MUSCLE
  (1620 kg, 420 hp, modest 0.80 g grip to go with it). Eleven cars in
  `cars.json` now, still comfortably inside the 16-car cap. Validated
  against the same real-unit ranges every other car uses, so a bad edit
  is rejected before it reaches the garage.

### Changed

- **Berthoud Pass 2.0 rebuilt.** The v1.15.0 layout packed three
  ramp-and-hairpin switchbacks up one side and four down the other into
  a tight footprint, and it turned out several of those pieces of road
  passed within 1-2 m of each other in plan view — including the
  valley loop-back, which clipped both the climb and the base of the
  descent — and its hairpin apexes turned as sharp as 97-161° at a
  single control point, well past a right angle. The new layout uses
  two clearly separated corridors (a climbing side and a descending
  side, offset far enough apart that even their widest wiggle never
  gets close) joined by two wide, gradual turns — one at the summit,
  one at the valley floor doing the loop-back and the climb back up to
  the start together. Every corner on the new lap is a gentle 37-83 m
  radius (previously as tight as 7 m), and the closest any two
  non-adjacent pieces of road come to each other is 96 m — comfortably
  clear of the guardrails' ~26 m of combined clearance. The lap is
  shorter as a direct result of easing out the switchbacks (1411 m,
  down from 3263 m) and climbs less (67 m vs 86 m), which is the
  honest trade for a lap with nothing sharper than a highway curve on
  it anywhere.

## [1.16.0] - 2026-08-19

### Removed

- **Boost and the fresh-rubber power-up are both gone.** v1.15.0 moved
  boost off the track and into the engine as an `aspiration` system
  (`"turbo"` / `"supercharged"`); this release removes that system
  entirely rather than replacing it again. There is no `KartSpec`
  aspiration, no boost button, no boost gauge, and no roadside item
  panel — a car's power is just its plain horsepower figure, full stop.
  Tires still wear over a race exactly as before, but nothing ever
  refreshes them mid-lap any more: picking a compound is a bet on the
  whole race distance, not a resource collected and spent lap to lap.

  `TURBO` and `BLOWER` are removed from `cars.json` — both existed
  solely as worked examples of the aspiration mechanic, and stripped of
  it they were either a near-duplicate of `SPORT` or a strictly worse
  `TOURER`. `RUBY` keeps its own identity (six gears, 1080 kg, high
  horsepower) as a plain naturally-aspirated car; only its turbo
  characteristic is gone.

  Retuning the tire model was necessary once tires could no longer ever
  be refreshed: the previous wear rates were calibrated assuming
  periodic resets from the (now-removed) fresh-rubber panel, so without
  them softs wore out completely partway through even a short sprint.
  Wear rates for all three compounds are lower across the board
  (`tire_wear_rate`: medium 0.0080, soft 0.0200, hard 0.0018), tuned so
  softs still win a sprint and mediums/hards still win a long race with
  a real, measured margin (`test_tire_strategy_crossover`).

## [1.15.0] - 2026-08-19

### Changed

- **Boost is an engine trait now, not a track pickup.** The old fresh-tires
  *and* push-to-pass power-up pairing is gone; only fresh rubber remains as
  a track power-up. In its place every `KartSpec` has an `aspiration` —
  `"turbo"` or `"supercharged"`, or nothing for a naturally-aspirated car —
  set from a new `"aspiration"` block in `cars.json` (`config.c`'s
  `read_aspiration`, replacing `read_turbo`). A turbo keeps the old
  rechargeable-charge behavior, now with a spool: press the button and
  the boost ramps in over `spool_seconds` rather than snapping to full
  power, drains while held, and recharges off the throttle. A supercharger
  has no button, no charge and no lag at all — it is a flat power
  multiplier applied to the engine any time the driver is on the throttle,
  the way a stateless mechanical boost actually behaves. Both still only
  help while `kart_step` is computing drive power under `in->accel`, which
  is why the AI's own turbo heuristic keeps checking `in->accel` before
  firing. `TURBO` moved to the new block; `BLOWER`, a new supercharged
  car, is the worked example for the other kind.

  Tested: `test_boosted_lap_is_quicker` (turbo) and the new
  `test_supercharged_lap_is_quicker` (supercharger, on Kenosha, where
  constant extra power doesn't cost a corner-entry overshoot penalty).

### Added

- **Berthoud Pass 2.0**, an eighth circuit built from the real pass's own
  elevation profile rather than stylized from memory: three ramp-and-hairpin
  switchbacks climbing one side, a short summit esses, four more descending
  the other, a flat loop-back through the valley floor that turns the road
  around, and a gentle climb back up to a start/finish that sits at the
  lap's own middle elevation — below the summit, above the valley floor.
  Each switchback is a real straight-ish ramp before the road folds back on
  itself, the way an actual mountain road is built, rather than reversing
  direction every few meters. 3263 m, 86 m of climb, 33 corners, tightest
  radius 7 m.

  Fixing the AI on this track also fixed two latent bugs any tight,
  self-crossing track could have hit: `kart_place_on_grid` now hands
  `track_locate` the segment its own backward walk already found instead
  of asking for a global nearest-point search, since a switchback can loop
  back close enough to itself in world space that the global search could
  snap a grid slot onto the wrong pass entirely; and corner segmentation
  is checked against a plausible curvature range in
  `test_corner_segmentation`.

- **RUBY**, a lightweight turbocharged 4-cylinder car for `cars.json`: six
  gears (15/35/67/100/150/200 km/h), high horsepower for its 1080 kg curb
  weight, and a high-pressure turbo tune (1.55x power multiplier).

### Fixed

- **The minimap was mirrored.** `draw_minimap` mapped world +Z to screen
  "up" (`max_z - pz`); the same cross(forward, up) = right convention the
  v1.0.1 steering fix verified against `guLookAt` says a rightward offset
  on an eastward road is +Z, and +Z has to land *below* the line on a
  non-mirrored map for that to read as south rather than a mirrored north.
  Every point the minimap draws — the road, the finish-line gate, the
  kart dots — now maps world +Z to screen +Y (down) instead, which
  matches the actual track layout rather than its left-right mirror
  image. Most noticeable on a track with real asymmetry, like the new
  Berthoud Pass 2.0.

## [1.14.0] - 2026-08-19

### Added

- **A rechargeable turbo**, gated per car by an optional `"turbo"` block
  in `cars.json` (`power_multiplier`, `boost_seconds`,
  `recharge_seconds`) — a car with no block never gets the button. The
  charge starts full, drains while the button is held, and recharges
  while off the throttle, never while it: recharging costs the speed
  accelerating would have bought, which is the actual trade-off. The
  AI's own usage heuristic (`ai_control`) only fires it while also
  accelerating, since `kart_step` only spends the extra engine power
  inside the `in->accel` branch — holding it at a speed the corner ahead
  already caps would just drain the charge for nothing. A gauge sits
  next to the tire bar for a car that has one. The shipped
  `config/cars.json` adds a fifth car, `TURBO`, as the worked example.
  Reachable by keyboard (`B` / `N`), Classic Controller (D-pad up), and
  GameCube/Xbox pads (`DPAD_UP` by default, editable in `controls.json`)
  — a bare Wii Remote or Wii Remote + Nunchuk has no button left to
  spare for it.

  Tested: a car with no turbo block cannot boost no matter how hard the
  button is held; charge falls while boosting and climbs back while
  lifting; a turbo SPORT laps Berthoud at 72.6 s against a plain
  SPORT's 74.3 s, same skill, same learned corner confidence, with the
  AI deciding for itself when to spend it.

## [1.13.0] - 2026-08-19

### Changed

- **The racing line is a real racing line now, not a fixed lateral
  position.** Each AI strategy sheet's `line_bias` used to be a constant
  offset held for the entire lap, so a sheet came out quick or slow
  depending on which way a particular circuit's corners happened to bend
  — nothing to do with the driver holding the wheel. `Track.curv_signed`
  is a new per-sample field (alongside the existing `curv`, same
  smoothing window, but keeping which way a bend turns rather than just
  how sharp it is), and `ai_tactical_line` now looks 14 m up the road,
  reads the curvature there, and leans toward that apex — scaled by
  `line_bias`, repurposed as 0 to 1 commitment rather than a signed
  position. A straight reads near-zero curvature, so the lean relaxes
  back to the centerline between corners on its own.

  Measured on Berthoud: INSIDE and CRUISER — the two ends of the
  commitment range — given identical skill and identical learned corner
  confidence, land within 1.4% of each other. Skill on its own is still
  worth 7.3% (`test_skill_sets_pace`, unchanged). The new
  `test_racing_line_pace_is_not_the_sheet` is what proves it.

## [1.12.0] - 2026-08-19

### Added

- **A marshal helicopter for driving the wrong way.** Judged on net
  progress along the track centreline — the same signed-arc measurement
  `kart_step` already keeps for lap counting — not on heading, so a car
  that spins but is still net moving forward is left alone. 2.5 seconds
  of real backward progress and the helicopter drops in over your own
  viewport with a TURN AROUND message, the speed readout turns red, and
  the engine is cut to 35% until you turn around and drive it back off.
  Humans only: the AI's own reverse-out recovery is never touched by it.
  Both numbers are in the new `wrong_way` block of `settings.json`.

### Changed

- **The on-screen controls are quieter.** The raw-input debug panel
  (`show_input_overlay`) now defaults off — the small always-on
  leaderboard is what a race screen shows day to day — and turning it on
  to check a new mapping still works exactly as before. The setup
  screen's two-line control legend is one line.

## [1.11.1] - 2026-08-19

### Fixed

- **The rest of the DOL defect — section sizes, not just file length.**
  v1.11.0 padded the file out to the length a loader reads. That was half
  the problem. `elf2dol` also writes each section's exact ELF byte count
  into the header, so a loader working in 32-byte units wants bytes the
  header never described. `tools/pad_dol.py` now rounds every nonzero
  section size up to 32 as well.

  This is the one property that separates the builds that boot from the
  builds that do not. v1.4.0, the last release confirmed booting, is also
  the last one where every section size happened to be a multiple of 32
  already; v1.5.0, v1.6.0, v1.10.0, v1.5.1 and v1.5.2 all have an unaligned
  text section and all fail. v1.5.2 mattered because it was a bare
  video-and-console program built from the same tree and toolchain, and it
  failed too — which ruled the game's own code out and left only the shape
  of the binary.

  The change is a no-op on v1.3.0 and v1.4.0, the two builds known to work,
  and normalizes every other release to the same shape. (#1)
- `tools/validate_dol.py` treats a section reaching under 32 bytes into the
  start of BSS as a note rather than a fault. That is what correct rounding
  produces, and crt0 zeroes BSS before `main`.

### Note on the earlier diagnosis

v1.11.0's notes said the alignment theory in issue #1 was disproved. That
was wrong, and the reason is worth recording: the counterexamples used were
v1.0 through v1.2.1, which have unaligned sections and were reported
working. They were the wrong control — they were last run on an older
Dolphin, and they are separately 12 to 28 bytes short, which disqualifies
them on a current one. Held to builds actually tested on the reporter's
Dolphin, alignment tracks the failure exactly.

## [1.11.0] - 2026-08-19

### Fixed

- **A DOL that a loader would refuse to open.** `elf2dol` ends the file at
  the last section's real size, but a loader reads sections in 32-byte
  units, so the file can promise up to 31 bytes it does not contain.
  Dolphin's `DolReader::Initialize` rejects the whole executable for that
  and reports it as "Failed to init core", before a single instruction
  runs. Whether it bites is luck — it depends on the last section's size
  modulo 32 — and WiiKart shipped it in v0.1 (12 bytes short) and v1.0
  through v1.2.1 (28 bytes short). `tools/pad_dol.py` now runs from the
  Makefile after every build and appends exactly the missing zero bytes,
  and `tools/validate_dol.py` fails the build if anything is still short.
  Found by the validator refusing to publish a build that was 4 bytes
  short. (#1)

### Added

- **An AI takes over when you cross the line.** Finishing used to hand your
  car to a driver with no plan, which on a mountain pass meant it might
  simply drive off. Now a cool-down driver takes the wheel: it aims for the
  shoulder on the side you are already on, scans 55 m ahead and slows for
  whatever corner is coming, bleeds the target speed down over eleven
  seconds, and parks the car. It brakes only when it is actually going too
  fast, so the car rolls to a stop rather than stamping on the pedal. Every
  finisher gets it, not just the player, so the field comes home instead of
  scattering.
- Tests for the above: the cool-down driver brings the car home on Monarch,
  Guanella and Berthoud with no falls, no reversing, at least 30 m driven
  and a genuine standstill; and no finished car in a whole field falls off
  the mountain after the flag.

### Changed

- `tools/validate_dol.py` no longer treats the loader's 32-byte rounding
  reaching into the start of BSS as a fault. Every normal DOL does that and
  crt0 zeroes BSS before `main`, so it is now a note; only a section whose
  advertised extent runs into BSS is reported as a collision.
- The historical patch workflow pads the DOL too, so a fix built on an old
  tag gets a loadable binary even though that tag's Makefile predates this.

## [1.10.0] - 2026-08-19

### Added

- Driver identity, separate from driving style. An `AIDriver` table gives
  each of the eleven rivals a skill, a consistency, an aggression, a
  tire-care value, a colour and a one-word trait, all fixed to their grid
  slot so the same rival turns up every race. The field now has a sharp
  end, a scruffy middle, two drivers who overdrive the car and two who are
  content to follow.
- Each driver's trait is shown beside their name on the results screen.
- `docs/HANDOFF.md`: how to build and test the project, what every file
  does, the conventions that matter, and a start-here recipe for each
  remaining item on the TODO list.

### Changed

- Strategy sheets now decide *style* only — line, defending, attacking,
  power-up patience, shift habits and how fast a driver adapts. The pace
  ceiling and engine trim they used to carry are gone, so how quick a
  driver ultimately is comes from their own skill. Measured on Berthoud,
  the same driver profile is worth 65.0 s a lap at 0.86 skill and 60.7 s
  at 1.06.
- Consistency and aggression now drive behaviour rather than sitting in a
  table: a ragged driver carries a few percent more speed into a corner
  than the tires will take and loses nerve for it, and gambles on
  unguarded corners far more often. Measured on Monarch, the three wildest
  drivers gambled eight times against four from the seven steady ones.
- Tire care picks a driver's compound and scales their wear, so the
  drivers who are kind to their rubber run softs and the ones who abuse it
  run hards.

## [1.9.0] - 2026-08-19

### Added

- Finish-line marker on the minimap: a black and white gate drawn across
  the road at the line, with a flag beside it.
- Finish-line marker in the race view: a checkered flag and the distance
  to the line, fading in over the last 220 m of a lap and the last 400 m
  of the final one, where it also reads FINAL LAP.
- A **JSON CONFIG** screen on the main menu showing where the config files
  were read from, whether each of the three loaded (or the parse error
  that stopped it), and the resulting roster — a number you can check
  against the file you edited.

### Changed

- Config files are now searched for one at a time across several
  locations, in both the shipped layout (`config/` beside `boot.dol`) and
  the flat one people improvise (the file dropped straight in), so a
  reasonable guess at where to put them works.
- When no SD card is present the game says so on that screen instead of
  quietly using built-in defaults. In Dolphin an emulated SD card is what
  makes the JSON readable at all; the README and `config/README.md` now
  say that plainly.
- Breakneck Pass is spelled properly: the HUD font gained the missing
  letters in 1.3.0, so the name no longer has to be written BREAHNECH.

## [1.8.0] - 2026-08-19

### Added

- Tires that happen over a race instead of being a label. Each compound
  has a temperature it wants, a window either side of it, a heating and
  cooling rate, a wear rate, and how much grip that wear costs. Rubber
  heats with work, cools with speed, and loses grip when it is cold,
  overheated or worn.
- Tire readout on the HUD: compound, temperature (amber and then red as it
  leaves its window) and a life bar that empties as the tires wear.
- Every tire parameter is in the `tires` block of `settings.json`.

### Changed

- Compounds are now a decision. Measured over a whole race with the field
  on one compound: softs win the sprint round Classic (127.3 s against
  128.7 for mediums), and lose the long race round Kenosha (267.6 s
  against 260.0), where they wear out completely. Hards win round
  Guanella. A regression test fails if softs ever become the automatic
  choice again.
- The AI feel the rubber too — cold, worn or overheated tires slow their
  corner speeds — so a compound choice shows up in the race, not just on
  the player's car.
- The fresh-rubber power-up now fits a genuinely fresh set: wear goes back
  to zero and the tires arrive at their optimal temperature.

### Fixed

- A car that fell off the road just after the line, was recovered before
  it, and drove over it again was credited with the same lap twice — a
  1.8-second lap in the timing test.

## [1.7.0] - 2026-08-19

### Added

- Per-car automatic shift points in `cars.json`:
  `automatic_upshift_fraction` and `automatic_downshift_fraction` for the
  whole gearbox, or `automatic_upshift_per_gear` /
  `automatic_downshift_per_gear` to set them gear by gear. Points close
  enough to make the box hunt are refused, and a refused file changes
  nothing.
- A `hills` block in `settings.json` scaling how much a grade costs in
  speed and in grip.

### Changed

- Gravity along a road now uses `g*sin(theta)` rather than
  `g*tan(theta)` — a 4% difference on Breakneck's steepest — and the load
  on the tires falls off with `cos(theta)`, so a steep climb costs grip as
  well as speed. Measured from 43 km/h with the throttle flat for three
  seconds: 104 km/h down a 20% descent, 89 on the flat, 74 up a 20% climb.

### Fixed

- Built-in cars had no automatic shift points of their own once the field
  was added, which read as "change up immediately": a car would take top
  gear at walking pace and bog there. Every car now gets the standard
  points unless it says otherwise, applied when a race starts as well as
  when the roster is loaded.

## [1.6.0] - 2026-08-18

### Added

- **Breakneck Pass**: 945 m, 44 m of climb, grades to 29%, 7.4 m of road
  and no guardrails. The elevation arrives in steps rather than one smooth
  arc — shelves, kicks and a ledge on the way down.
- **Guanella Pass**: 1986 m of switchbacks — 15 corners, seven of them
  hairpins down to a 6 m radius — climbing 64 m on 7.8 m of unguarded
  road.
- Road width now varies along a lap. Each circuit can carry a width
  profile authored per control point and eased between them, so a hairpin
  can stay narrow and punishing while another tight corner is opened out
  enough to hold a second line.
- Berthoud, Loveland, Monarch, Breakneck and Guanella all have width
  profiles: the three tightest corners on each are pinched, two more are
  opened out.

### Changed

- The simulation, the AI and the renderer all use the width of the piece
  of road a car is on rather than the circuit's average: grip, guardrails,
  the cliff edge, checkpoints, power-up panels, the racing line, curbs,
  rails, cliff faces and the start line.

## [1.5.0] - 2026-08-18

### Added

- Lap timing for every driver, human and AI: the lap running now, the last
  one, and a personal best, kept by the simulation and shown on the HUD.
  A lap only counts when it is driven across the line, so the rollout from
  the grid and a checkpoint recovery are not timed as laps.
- A lap-complete popup inside that player's own viewport, showing the time
  and calling out a personal best, with its own chime.
- A live leaderboard down the right edge of each viewport: position,
  driver, and the gap to the leader in seconds. Split screens show the
  sharp end plus that player's own row.
- Names for the eleven AI drivers, fixed to their grid slots, replacing
  the strategy label as the way a rival is identified on the HUD, in the
  leaderboard, and in the results.
- A `LAPS` row in the pre-race menu: `AUTO` (the circuit's own count, shown
  alongside) or 1 to 9, validated the same way the JSON override is.
- A digital tachometer beside the rev bar, with an `instruments` block in
  `settings.json` for its idle and redline values.

### Changed

- The rev band now reads as four states rather than a gradient: grey while
  the engine is bogging below its torque band, green through the useful
  range, amber in the shift window, red at the limiter, with a mark on the
  bar where the shift window begins.

## [1.4.0] - 2026-08-18

### Added

- Rebuilt chase camera, now its own tested module rather than a few lines
  inside the renderer:
  - reversing past a dead zone swings the view round toward the nose so it
    looks where the car is actually going, rate-limited and smoothed so
    crossing through zero can neither snap it round nor set it hunting;
  - driving forward again brings it back the same way;
  - the camera copies a configurable share of the road's pitch, so climbs,
    crests and descents keep a consistent viewing angle instead of showing
    a wall of pavement or a lot of sky;
  - pitch, height and ground clearance are clamped, and look-ahead grows
    with speed up to a cap;
  - a checkpoint respawn re-places the camera instead of letting it streak
    across the mountain after the car.
- A `camera` block in `settings.json` exposing all of the above — distance,
  height, clearance, look-ahead and its speed gain and cap, aim height and
  floor, follow and aim smoothing, reverse dead zone, full-swing speed,
  orbit rate and smoothing, pitch influence, pitch smoothing, and pitch
  limits — documented in `config/README.md`.

### Changed

- Video output selects 480p progressive when a component cable is present
  and progressive scan is enabled, instead of always taking the interlaced
  mode: every line is drawn every frame, with no deflicker blur.
- Widescreen consoles now get a true 16:9 projection rather than a 4:3
  image stretched sideways by the TV. Split-screen viewports are corrected
  the same way.
- The camera holds still while the leave-race confirmation is up, matching
  the frozen simulation.

## [1.3.0] - 2026-08-17

### Added

- Editable `config/cars.json`, `config/settings.json`, and
  `config/controls.json` files, each validated independently with safe
  compiled-default fallback.
- JSON-defined garage roster: tune existing cars or append a new car without
  modifying C source. Up to 16 cars and six forward gears are supported.
- User-editable race, vehicle, steering, AI, recovery, track, and control
  settings in documented real-world or clearly labeled multiplier units.
- Live input translator showing keyboard input, recommended physical Xbox
  mapping, the logical GameCube input received from Dolphin, and the final
  in-game action.
- WiiKart manual-shift bindings: Wii Remote D-pad Up/Down and Classic
  Controller ZR/ZL, alongside configurable keyboard and GameCube mappings.
- Staged cliff recovery: visible fall, one-second black hold, configurable
  fade-in, and roughly five seconds of flashing collision immunity.
- Leave-race confirmation. The race-menu action now pauses instead of
  immediately abandoning the event; Confirm leaves, while Back or Menu again
  resumes from the frozen moment.

### Changed

- Every circuit is approximately 10–15% wider to give the twelve-car field
  more usable racing room.
- Monarch Pass is substantially longer and more difficult, with an extended
  summit/descent, linked switchbacks, tighter corners, a larger climb, and
  steeper grades.
- Aggressive AI can deterministically overcommit on unguarded corners. The
  quickest personalities remain dangerous, but they are no longer incapable
  of making the occasional gravity-assisted career decision.
- The input, package, Homebrew Channel, and README control descriptions now
  agree with the actual runtime mappings.
- Version metadata and release packaging are updated for v1.3.0, including
  the editable configuration files, this changelog, and the future-work list.

### Fixed and hardened

- Runtime car indexes now use the loaded JSON roster size instead of assuming
  the original compile-time roster.
- Falling and recovering cars are excluded from kart-to-kart collision
  resolution.
- Configuration parsing is transactional: malformed or out-of-range data
  cannot leave a partially applied roster or settings structure.
- Host tests cover shipped JSON, custom-car insertion, rollback after invalid
  input, control translation, recovery phases and immunity, track geometry,
  and fallible AI on the extended Monarch Pass.

## [1.2.1] - 2026-08-17

- Prevented a near-finish cliff fall from awarding most of a free lap.
- Let finished cars coast instead of braking and reversing through the field.
- Made keyboard menu taps reliable and improved connected-controller checks.
- Restored the visible falling phase before checkpoint recovery.

## [1.2.0] - 2026-08-17

- Added real gearboxes, per-driver shifting, Kenosha and Monarch Pass,
  unguarded mountain cliffs, checkpoint recovery, circuit-specific lap counts,
  expanded garage options, and two-player keyboard defaults.

## [1.1.0] - 2026-08-17

- Rebuilt the vehicle model around real specifications and added distinct AI
  strategies, learning, driver adaptation, Berthoud and Loveland Pass, and
  motorsport-style power-ups.

[1.10.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.10.0
[1.9.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.9.0
[1.8.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.8.0
[1.7.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.7.0
[1.6.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.6.0
[1.5.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.5.0
[1.4.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.4.0
[1.3.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.3.0
[1.2.1]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.2.1
[1.2.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.2.0
[1.1.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.1.0
