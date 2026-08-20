# WiiKart future work

These are design targets for releases after v1.11.0. Details may change after
testing, because apparently cars, mountains, and tires all object to being
reduced to one convenient slider.

## How this list is organized

Open work below is sorted by **release size**, not by system, because that's
the decision that actually needs making next: what ships alone in a quick
patch versus what waits to go out bundled with related work in a real
feature release. The shipped-history sections further down (Camera, Roads,
Powertrain, Drivers/AI, Laps, Tires) are kept as-is for engineering context —
they are where the *next* round of `[x]` entries and implementation notes
will land once an item below ships — but the open `[ ]` items themselves now
live only here, so "what's left" doesn't require reading eight sections to
find.

**Small (patch, x.y.Z) releases** are for a single narrow, low-risk,
self-contained change — the kind v1.19.1 (Berthoud's switchbacks) and
v1.19.2 (a roster-drift bug fix) already were. Even so, don't cut a release
for just one of these the moment it's done: let two or three land on `TODO.md`
as finished, cumulative changes, and ship them together. A version bump is
not free — it's a changelog entry, a release-notes doc, a version/date bump
in three files, and a reader's attention — so it should carry more than one
line of value.

**Big (minor, x.Y.0) releases** are for a handful of related items landed
together on purpose, the way v1.19.0 bundled weather + boost + a wilder AI +
wider corners, v1.20.0 bundled the oversteer rework + AI competitiveness +
two scaffolds + weather depth, and v1.21.0 bundled the turbo rework + two
new drivers + AI racing lines. The bundles below are grouped because the
items in each one touch the same code, the same menu surface, or the same
underlying subsystem — doing them together means paying the "learn this
part of the codebase again" cost once instead of three times, not just
batching unrelated work to make a bigger changelog.

### Small releases — ship alone, batch two or three before pushing

Roughly in the order they're worth doing (cheapest/most self-contained
first):

- [ ] **Session-best lap per circuit, in-memory only.** Not full persistent
  standings (that's a big-release item below, and needs real save/load
  I/O) — just remembering, for the life of the running process, each
  circuit's best lap across however many races get run before the game is
  closed, and showing it next to the current best on the results screen.
  No new subsystem: `best_lap_time` already exists per kart per race, this
  only needs one array of "best ever seen this session" indexed by
  `track_id` that survives a `game_init` instead of resetting with it.
- [ ] **HUD readout of the upcoming weather zone's condition**, not just the
  color change on the road surface itself. `track_weather_at` already
  answers "what condition is segment N in" for any segment and any time;
  this is a look-ahead call against the player's own position plus a
  `weather_name()` HUD line, no new mechanic and no rendering work beyond
  one more `hud_text` call.
- [ ] **Let the camera lead into corners slightly** rather than only aiming
  along the car's nose. A small extension of the existing speed-scaled
  look-ahead in `source/camera.c` — bias the aim point toward the signed
  curvature of the road a little further up, the same `Track.curv_signed`
  the AI's own racing line already reads. Self-contained to the camera
  module and its existing tests.

### Big releases — bundle before shipping, roughly in priority order

**1. Wire up the three scaffolds (difficulty, team, career).** Highest
priority of the big items: v1.19.0 and v1.20.0 already shipped the full
data shape for all three (`DifficultyPreset`, `TeamDef`, `CareerState` and
their `GameConfig` fields) specifically so this would be cheaper later, and
right now none of it does anything — that's a standing cost (three
scaffolds a reader has to learn are inert) that only gets paid off by
finishing the wiring. All three also converge on the same real gap in the
game: there is no pre-race setup/garage menu screen to add controls to yet,
so building that once serves all three instead of three separate menu
efforts. Concretely:
  - a garage/setup menu screen with controls for a difficulty preset, a
    team, and (once a campaign exists) continuing one;
  - `game_init` reading `cfg.difficulty` to set `laps_override` and scale
    AI aggression/skill, and `cfg.team_mode`/`cfg.team[]` to force each
    human's `paint_idx` to their team's colour and assign AI to teams;
  - `ai_no % kart_spec_count` car assignment leaning toward
    `DIFFICULTY_CARS_MATCHED`/`UNDERDOG` relative to the human's car;
  - `track_init`'s `has_walls` becoming overridable per race for
    `DIFFICULTY_GUARDRAILS_ON`/`OFF`;
  - a combined per-team score/ranking alongside each driver's own
    `final_rank`, plus a HUD element for it, and a decision on whether team
    mates get any in-race awareness of each other in `ai_control`;
  - save/load I/O (an SD card file, most likely) to persist `CareerState`
    across sessions, `kart_place_on_grid` reading it to start a human
    somewhere other than the back of the grid, and a "next race" menu flow
    that actually strings races together as one campaign;
  - explicit decisions on what a zero-initialized `GameConfig` should mean
    for `difficulty` (today: `DIFFICULTY_EASY`, likely not the intent) and
    what a first-ever career race (`has_last_result == 0`) should do
    (likely: fall back to today's back-of-grid start).

**2. Winter, for real, across the roster.** The weather system has been
CLASSIC-only by design since v1.19.0; this is the release where that
becomes deliberate rather than just unfinished. Bundled because all three
depend on and inform each other — extending zones to new circuits is the
first real test of whether the tire compounds' snow/ice/puddle grip
numbers hold up outside the one track they were tuned on, and the visual
work only pays off once there's more than one circuit's worth of zone to
actually notice:
  - add zones (snow → ice → puddle, `track_weather_at`) to some or all of
    the seven mountain passes — its own pass over each circuit's own
    geometry, not a copy-paste of CLASSIC's three-zone layout;
  - define dry/snow/ice tire performance deliberately instead of only the
    grip-multiplier trade that exists today: investigate whether harder
    compounds should also gain lower rolling resistance or durability on
    dry pavement, whether winter-oriented/softer compounds should gain
    something beyond grip in the cold, and keep watching for a compound
    winning on label alone rather than on the modeled trade;
  - give weather zones a visible boundary/texture in `draw_track` beyond
    today's flat color tint, now that there is reason to actually notice
    a zone's edge on more than one circuit.

**3. Player history, and an AI that learns from it.** Both items are about
capturing and persisting player performance over time, and the first is
close to a prerequisite for the second (you need a place to keep lap data
before you can mine it):
  - persistent driver standings, records, and player progress across
    sessions — the real save/load subsystem the career scaffold above also
    wants, so worth designing once for both if bundle 1 hasn't already
    landed it;
  - record exceptional player laps and use their racing-line/braking data
    to improve selected NPC behavior on later runs, with reset/export
    controls so a heroic accident does not become mandatory curriculum
    forever.

**4. A presentation pass: camera modes and pass scenery.** Lowest priority
of the four — both items are about how the game looks rather than how it
races, which has consistently been this project's second priority behind
simulation depth (see Overall direction, below). Bundled because both are
rendering-heavy, `main.c`-side work rather than `game.c` simulation work,
so it's one release spent in rendering instead of splitting that context
switch across two:
  - optional cockpit and bumper camera views, sharing the existing pitch
    and look-ahead settings;
  - rework the mountain passes' scenery, landmarks, elevation transitions,
    roadside detail, and silhouettes to be more visually distinctive from
    each other — right now they mostly read as the same road at different
    grades.

---

## Camera — done in v1.4.0

The whole camera block shipped in v1.4.0, in `source/camera.c` with host
tests in `tests/test_game.c` and settings in the `camera` block of
`config/settings.json`.

- [x] Add a smooth reversing camera. As reverse speed increases, orbit the
  chase camera progressively toward the front of the car so the view looks in
  the direction the car is actually backing. Use speed-based interpolation,
  damping, and a small dead zone so crossing through zero does not snap the
  camera 180 degrees or make it hunt back and forth.
- [x] Return the camera smoothly to its normal forward chase position as the
  car slows in reverse or begins moving forward again.
- [x] Make camera pitch follow the road/car pitch enough to preserve a
  consistent viewing angle on climbs, crests, and descents. Smooth the response
  so every small surface change does not become an involuntary camera nod.
- [x] Clamp pitch and vertical movement so the camera never settles too low,
  points mostly into the pavement on a descent, or loses the road over a crest.
- [x] Keep the car's direction of travel and useful road-ahead area visible at
  all times. Add speed-sensitive look-ahead while preserving terrain avoidance
  and a readable amount of horizon.
- [x] Expose reverse-orbit speed, smoothing, pitch influence, pitch limits,
  height, distance, and look-ahead as documented camera settings rather than
  burying the final feel in constants.

Open work for this area (cockpit/bumper views, corner lead-in) is tracked
under Release planning above.

## Race end and marshals

- [x] Hand the car to an AI when the player finishes, instead of leaving it
  under a driver with no plan — it used to be able to drive off the
  mountain. Done in v1.11.0: `cooldown_control` in `source/game.c` aims for
  the shoulder, slows for the corner ahead, winds its target speed down
  over eleven seconds and parks.
- [x] Send a marshal helicopter down when the player is going the wrong way.
  Done: detected from net progress along the track centreline (the same
  signed arc `kart_step` already computes for lap counting), not from
  heading, so a spin that is still net moving forward is left alone.
  Humans only, gated on `k->human >= 0 && !k->finished`; the AI's own
  reverse-out recovery is untouched. `wrong_way.seconds` (2.5s to trigger)
  and `wrong_way.power` (0.35 of engine left) are in `settings.json`. The
  helicopter and TURN AROUND message draw in the player's own viewport.
- [x] Tone the on-screen control hints down and give the space to the small
  leaderboard instead. Done: `show_input_overlay` now defaults to 0 in
  both `control_config_defaults` and `config/controls.json`, and the setup
  screen's two hint lines are one.

Fully shipped — nothing open here.

## Roads, passes, and conditions

- [x] Add variable road width by segment or control point. Some sharp turns
  should remain narrow and punishing; others should be tight in radius but
  wide enough to support multiple lines and meaningful speed through the turn.
      *(v1.6.0: per-control-point width profiles, used by the physics, the
      AI and the renderer.)*
- [x] Add **Breakneck Pass**: a short circuit with large elevation changes,
  narrow pavement, abrupt grade transitions, and no guardrails.
      *(v1.6.0: 945 m, 44 m of climb, 29% grades, 7.4 m wide.)*
- [x] Add **Guanella Pass**: a medium-length circuit with relatively modest
  overall elevation change, narrow pavement, no guardrails, and a
  switchback-heavy layout. *(v1.6.0: 1986 m, seven hairpins, 7.8 m wide.)*
- [x] Add **Berthoud Pass 2.0**: built from the real pass's own elevation
  profile — a stack of switchbacks up, a summit, a stack down, a flat
  loop-back through the valley floor, and a gentle climb back to a
  start/finish at the lap's own middle elevation. *(v1.15.0: 3263 m, 86 m
  of climb, 33 corners, tightest radius 7 m. Rebuilt in v1.17.0 to a
  smooth sine-wiggle shape after a plan-view check found the original
  layout's loop-back passing within 1-2 m of the climb — which fixed
  that check but flattened the track's whole character in the
  process (1411 m, 67 m of climb, 20 corners, nothing tighter than
  37 m) and turned out not to have been an actual gameplay bug:
  `track_locate` always uses a windowed hint during driving and grid
  placement, so two switchback tiers close in plan view but far apart
  in elevation were never confusable at the wheel. v1.19.1 restored
  the original v1.15.0 hairpin layout and scaled it up further (1.30
  → 1.50) for more room everywhere — 3704 m, 85 m of climb, 36
  corners, tightest radius 8 m — with
  `test_berthoud2_keeps_its_switchbacks` guarding against smoothing it
  away a third time.)*
- [x] Add localized snow and ice hazards with visible boundaries and distinct
  grip behavior. *(v1.19.0: three zones on CLASSIC only, each independently
  aging from snow to ice to a puddle over the settings-tunable
  `snow_to_ice_seconds`/`ice_to_puddle_seconds`; `track_weather_at` in
  `track.c`, tire-compound grip multipliers in the `weather` block of
  `settings.json`. Soft is the tire for snow/ice, hard for a puddle,
  medium is deliberately never the best or worst choice. Rendered as a
  surface color change in `draw_track`. Standing water also drags at every
  car regardless of tire, and the AI's own corner-speed lookahead
  discounts grip for whatever weather patch is ahead of it — v1.20.0. See
  `config/README.md`.)*
- [x] Bring paved edges out to the guardrail on every barriered circuit and
  widen the tightest turns across the roster, closing off the "run onto
  the shoulder to cut a corner" line. *(v1.19.0: barriered tracks'
  shoulder shrunk to ~1.4 m everywhere (`test_pavement_reaches_guardrail`);
  Berthoud, Loveland, Monarch and Guanella scaled up 8-30% to open out
  their tightest turns without touching their hand-authored shape — a
  uniform scale keeps grade% and clearance both correct automatically,
  unlike the position-smoothing filter tried and discarded first (it
  shrank the whole track and made Guanella's chained switchbacks worse,
  not better). Berthoud's summit switchbacks, the tightest corners and
  closest self-approach on that circuit, specifically benefit.)*

Open work for this area (scenery rework, winter variants beyond CLASSIC,
weather zone visuals/HUD) is tracked under Release planning above.

## Powertrain, boost, and instruments

- [x] Revisit grade and power behavior so acceleration is reduced more
  convincingly uphill while cars retain or gain speed more naturally downhill.
      *(v1.7.0: gravity uses sin rather than tan of the road angle, tire load
      falls off with cos, and both are scalable from the `hills` block.)*
- [x] Add a rechargeable nitro/boost resource and a clearly readable boost
  gauge; add a JSON `turbo` capability and tuning block available only to
  cars designed to use it. Done in v1.14.0, replaced by a broader
  `aspiration` system in v1.15.0.
- [x] Move boost off the track entirely and onto the engine: naturally
  aspirated, turbocharged, or supercharged, tunable per car instead of a
  power-up to collect. Done in v1.15.0: `KartSpec.aspiration` is
  `"turbo"` (rechargeable charge, a spool ramp, a button) or
  `"supercharged"` (stateless flat power multiplier while on the
  throttle, no button, no charge) or absent (naturally aspirated). Only
  the fresh-tires power-up remains on track. `BLOWER` is the shipped
  supercharged car.
- [x] Retire boost and the fresh-tires power-up entirely. Done in v1.16.0:
  no aspiration system, no roadside item panel, no boost button. A car's
  power is just its plain horsepower figure; tires only wear over a
  race, with no mid-lap refresh. `TURBO` and `BLOWER`, which existed
  solely to demonstrate the removed engine mechanic, are gone from the
  garage; `RUBY` stays as a plain naturally-aspirated car.
- [x] Bring boost back as its own, simpler mechanic instead of a per-car
  engine trait. *(v1.19.0: a universal meter (`Kart.boost_meter`) that
  charged with revs on a curve and spent all at once on a button for an
  instant speed bump, zeroed by the next shift. Reworked again in
  v1.21.0 into a fully automatic turbo: `Kart.turbo_spool` builds and
  bleeds off on its own from real throttle and revs — no button — and
  applies straight to engine power every frame, tapered by how much grip
  is already spent cornering so it can't destabilize a car mid-corner.
  The "use" button is now an instantaneous full-throttle override
  instead of a resource to spend. Tunable in the `turbo` block of
  `settings.json`.)*
- [x] Add per-car and optionally per-gear automatic shift ranges to `cars.json`,
  including configurable upshift/downshift points rather than only limiter
  speeds. *(v1.7.0.)*
- [x] Add a numerical/digital tachometer alongside the existing rev band.
      *(v1.5.0, with idle and redline in `settings.json`.)*
- [x] Make the tachometer band change colors as the engine moves through the
  useful range, shift window, and limiter. *(v1.5.0: grey bogging, green
  useful, amber shift window, red limiter, plus a shift-window mark.)*

Fully shipped — nothing open here.

## Drivers, AI, and persistent competition

- [x] Raise the AI field's overall competitiveness and add a "no guts, no
  glory" strategy for drivers who commit to everything. *(v1.19.0: a new
  `AI_YOLO` sheet — the highest `conf_start`/`attack` and lowest `defend`
  in the roster, and it rides every gear to the limiter, which also
  spools its turbo fastest — assigned to TANAKA and CROSS. `ai_skill_mult`
  up from 0.97 to 1.02 in v1.19.0, then 1.06 in v1.20.0; `braking_multiplier`
  up from 0.72 to 0.76 in v1.20.0. The weakest couple of drivers (DELGADO,
  CROSS) got a modest skill bump so they are no longer plain slow, while
  HOLT and PETRAN stay the field's real top and bottom so the roster keeps
  a genuine spread (`test_driver_field_has_characters`). An earlier, more
  extreme YOLO tuning (`conf_start` 1.20) could strand a driver in a
  permanent fall/respawn loop on one of Monarch's tighter corners — see
  `test_yolo_can_finish_the_hardest_track`. v1.21.0 added two more
  HOLT-tier drivers, KESSLER and DUARTE, replacing RENARD and SOLANO after
  checking which roster tests each removed driver was load-bearing for —
  see `docs/HANDOFF.md` §6 before growing this roster past eleven, the
  array-index scheme silently makes anything past `NUM_KARTS - 1` entries
  unreachable in a normal race.)*
- [x] Separate driver skill from personality. Build a field containing elite
  aggressive drivers, poor drivers who overcommit, overly passive drivers,
  and dependable safe drivers rather than eleven variations of "quite good."
      *(v1.10.0: an `AIDriver` table in `source/game.c` gives each rival a
      skill, a consistency, an aggression and a tire-care value, separate
      from the strategy sheet that says how they drive. Skill is worth
      4.3 s a lap on Berthoud; wild drivers gamble on unguarded corners
      about five times as often as steady ones.)*
- [x] Give every AI driver a unique name, character, visual identity, strengths,
  weaknesses, and stable behavior profile. *(v1.10.0: name, colour and a
  one-word trait shown on the results screen, all fixed to the grid slot,
  so the same rival is the same rival every race.)*
- [x] Make pace come from skill rather than from the racing line. `line_bias`
  used to be a constant lateral offset held all lap, so the INSIDE sheet was
  quick because its line was literally shorter and the CRUISER sheet was
  slow because its line was longer — on whichever circuit that particular
  offset happened to net out as a shortcut, whoever was driving. Fixed by
  `Track.curv_signed` (new field alongside `curv`, same smoothing window,
  keeps which way a bend turns) and `ai_tactical_line` (`source/game.c`),
  which now looks 14 m up the road and leans toward whatever apex is
  actually there, scaled by `line_bias` reused as 0..1 commitment rather
  than a signed position. A straight reads near-zero curvature, so the
  lean relaxes back to the centerline on its own. Measured on Berthoud:
  INSIDE and CRUISER, same skill, same learned corner confidence, land
  within 1.4% of each other; skill alone is still worth 7.3% (0.86 vs 1.06,
  `test_skill_sets_pace`). Test: `test_racing_line_pace_is_not_the_sheet`.
  *(v1.21.0: some drivers — OSEI, NORDLI, DUARTE — now go a step further
  and genuinely hunt the racing line, blending in a second, farther
  curvature sample (`ai_line_curvature`, `AIDriver.line_lookahead_m`) so
  they set up for the corner after the one they're in, each reading a
  different distance ahead. That anticipation fades toward nothing the
  tighter the near corner already is, both because a driver mid-hairpin
  realistically isn't still planning the next bend and because it turned
  out to be needed: an early, untapered cut of this briefly broke the
  same fragile TRUCK/AI_YOLO pairing noted above on Monarch and Berthoud
  Pass 2.0.)*
- [x] Add a continuously updating on-screen leaderboard based on current race
  order and keep it visible without covering the useful driving view.
      *(v1.5.0: right-edge column with position, driver and gap in seconds.)*

Open work for this area (persistent standings, learning from player laps,
and wiring the difficulty/team/career scaffolds) is tracked under Release
planning above.

## Laps and timing — done in v1.5.0

- [x] Show a brief per-player lap-complete popup inside that player's viewport.
- [x] Track and display individual current, previous, and best lap times for
  every human and AI driver.
- [x] Expand lap-count customization beyond the existing per-track JSON
  override, including an accessible pre-race option and sensible validation.

Open work for this area (a session-best lap per circuit) is tracked under
Release planning above.

## Tires and surfaces

- [x] Replace the simple soft/medium/hard choice with compounds whose tradeoffs
  matter over an entire race. *(v1.8.0: temperature window, wear, rolling
  resistance and grip fall-off, all in `settings.json`.)*
- [x] Prevent soft tires from being the automatic best choice by modeling a
  useful combination of temperature, wear, rolling resistance, durability,
  and surface compatibility. *(v1.8.0: softs win sprints, mediums and hards
  win long races; a regression test fails if that stops being true. Surface
  compatibility landed in v1.19.0 for CLASSIC's weather: soft is the
  snow/ice tire, hard is the puddle tire, medium is deliberately never the
  best or worst choice.)*

Open work for this area (deliberately defining dry/snow/ice performance
beyond the current grip trade) is tracked under Release planning above.

## Overall direction

- [ ] Move the feel away from pure go-kart arcade racing and toward a low-poly
  formula-car experience: more deliberate setup, braking, power delivery,
  race information, driver identity, and consequence without losing readable
  controls or quick races.

This is the standing north star, not a release-sized item on its own — it's
the reason simulation-depth work (tires, drivetrain, oversteer/understeer,
weather, AI) has consistently outpaced presentation work (camera modes,
scenery) in priority, and it's worth weighing any new idea against directly:
does this make the car and the mountain feel more real, or just add a knob.
