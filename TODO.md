# WiiKart future work

These are design targets for releases after v1.11.0. Details may change after
testing, because apparently cars, mountains, and tires all object to being
reduced to one convenient slider.

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

Left for later, now that the camera has somewhere to live:

- [ ] Optional cockpit and bumper views, sharing the same pitch and
  look-ahead settings.
- [ ] Let the camera lead into corners slightly rather than only along the
  car's nose.

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

## Roads, passes, and conditions

- [x] Add variable road width by segment or control point. Some sharp turns
  should remain narrow and punishing; others should be tight in radius but
  wide enough to support multiple lines and meaningful speed through the turn.
      *(v1.6.0: per-control-point width profiles, used by the physics, the
      AI and the renderer.)*
- [ ] Rework the mountain-pass levels so their scenery, landmarks, elevation
  transitions, roadside detail, and silhouettes are more visually distinctive.
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
- [ ] Add winter variants of the mountain passes. The weather system below
  covers CLASSIC only by design; extending zones to the seven passes is
  its own pass over each circuit's own geometry.
- [x] Add localized snow and ice hazards with visible boundaries and distinct
  grip behavior. *(v1.19.0: three zones on CLASSIC only, each independently
  aging from snow to ice to a puddle over the settings-tunable
  `snow_to_ice_seconds`/`ice_to_puddle_seconds`; `track_weather_at` in
  `track.c`, tire-compound grip multipliers in the `weather` block of
  `settings.json`. Soft is the tire for snow/ice, hard for a puddle,
  medium is deliberately never the best or worst choice. Rendered as a
  surface color change in `draw_track`. See `config/README.md`.)*
- [ ] Give weather zones a visible boundary/texture beyond the flat color
  tint `draw_track` uses today, and consider a HUD readout of the
  upcoming zone's condition, not just its color on the road itself.
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
  charges with revs on a curve — quadratic in `rev_frac`, so redline
  time counts far more than the same time low in the band — spent all
  at once on a dedicated `boost` button/input for an instant speed
  bump, and zeroed by the next shift of any kind so working it means
  holding a gear on purpose. Tunable in the `boost` block of
  `settings.json`; the AI fires it too, gated on real headroom before
  the next corner so it doesn't launch itself into a turn too hot.)*
- [x] Add per-car and optionally per-gear automatic shift ranges to `cars.json`,
  including configurable upshift/downshift points rather than only limiter
  speeds. *(v1.7.0.)*
- [x] Add a numerical/digital tachometer alongside the existing rev band.
      *(v1.5.0, with idle and redline in `settings.json`.)*
- [x] Make the tachometer band change colors as the engine moves through the
  useful range, shift window, and limiter. *(v1.5.0: grey bogging, green
  useful, amber shift window, red limiter, plus a shift-window mark.)*

## Drivers, AI, and persistent competition

- [x] Raise the AI field's overall competitiveness and add a "no guts, no
  glory" strategy for drivers who commit to everything. *(v1.19.0: a new
  `AI_YOLO` sheet — the highest `conf_start`/`attack` and lowest `defend`
  in the roster, and it rides every gear to the limiter, which also
  charges its boost meter fastest — assigned to TANAKA and CROSS.
  `ai_skill_mult` up from 0.97 to 1.02 for the whole field. The weakest
  couple of drivers (DELGADO, CROSS) got a modest skill bump so they are
  no longer plain slow, while HOLT and PETRAN stay the field's real top
  and bottom so the roster keeps a genuine spread
  (`test_driver_field_has_characters`). An earlier, more extreme YOLO
  tuning (`conf_start` 1.20) could strand a driver in a permanent
  fall/respawn loop on one of Monarch's tighter corners — see
  `test_yolo_can_finish_the_hardest_track`, which exists specifically to
  catch that again.)*
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
- [x] Add a continuously updating on-screen leaderboard based on current race
  order and keep it visible without covering the useful driving view.
      *(v1.5.0: right-edge column with position, driver and gap in seconds.)*
- [ ] Add persistent driver standings, records, and player progress across
  sessions.
- [ ] Record exceptional player laps and use their racing-line/braking data to
  improve selected NPC behavior on later runs. Include reset/export controls
  so a heroic accident does not become mandatory curriculum forever.
- [ ] Wire up a difficulty preset (Easy/Normal/Hard) that sets lap count, AI
  aggressiveness, AI car choice, and guardrails together as one menu
  choice, instead of a player tuning each one by hand. *(v1.19.0 added
  only the data shape — `DifficultyPreset`, `difficulty_presets[]` and
  `GameConfig.difficulty` in `game.h`/`game.c`, `test_difficulty_presets_
  scaffolding` checking the table itself — deliberately not read by
  `game_init` or anywhere else yet, so choosing a preset today has no
  effect on a race.)* Real wiring needs, at minimum:
  - a garage/setup menu control to pick a preset (currently none exists);
  - `game_init` reading `cfg.difficulty` to set `laps_override` and scale
    each AI driver's effective aggression/skill;
  - a way for `ai_no % kart_spec_count` car assignment to instead lean
    toward `DIFFICULTY_CARS_MATCHED`/`UNDERDOG` relative to the human's
    chosen car;
  - `track_init`'s `has_walls` becoming overridable per race rather than
    fixed per circuit, for `DIFFICULTY_GUARDRAILS_ON`/`OFF`;
  - a decision on what a zero-initialized `GameConfig` should mean for
    `difficulty` (today that's `DIFFICULTY_EASY`, likely not the intent —
    see the comment on the field).
- [ ] Wire up team mode: group humans and AI onto teams identified by paint
  colour, with a combined team score/ranking alongside each driver's own.
  *(v1.20.0 added only the data shape — `TeamDef`, `team_defs[]` and
  `team_name()` in `game.h`/`game.c`, `GameConfig.team_mode` and
  `GameConfig.team[]`, `test_team_mode_scaffolding` checking the table
  itself — deliberately not read by `game_init` or anywhere else yet, so
  setting `team_mode` today has no effect on a race.)* Real wiring needs,
  at minimum:
  - a garage/setup menu control to join a team (currently none exists);
  - `game_init` reading `cfg.team_mode`/`cfg.team[]` and forcing each
    human's `paint_idx` to match their team's colour instead of the
    individually-chosen one;
  - a way to put AI drivers on a team too — whichever team is short a
    car, or split evenly, rather than every AI keeping its own fixed
    `paint` from the `ai_drivers[]` table;
  - a combined per-team score or ranking computed alongside the existing
    per-kart `final_rank`, plus a HUD element to show it;
  - deciding whether team mates should get any in-race awareness of each
    other (e.g. `ai_control`'s attack/defend logic treating a team mate
    like a rival it should not fight).
- [ ] Wire up career/campaign mode: a human's finishing position in one
  race becomes their starting grid slot in the next, instead of always
  starting at the back. *(v1.20.0 added only the data shape —
  `CareerState`, `career_record_result()` and `GameConfig.career[]` in
  `game.h`/`game.c`, `test_career_mode_scaffolding` checking that the
  helper stores a result and that `game_init` still ignores it —
  deliberately not read anywhere else yet, so populating `career[]`
  today has no effect on a race.)* Real wiring needs, at minimum:
  - save/load I/O to persist `CareerState` across sessions (an SD card
    file, most likely) — today it only lives as long as the process;
  - `game_init`/`kart_place_on_grid` reading `cfg.career[i]` and
    starting that human somewhere other than the fixed back-of-grid
    slot it always uses now;
  - a decision on what "grid slot" means for a rank of 1 with a full
    twelve-car field — the front slot is presumably still shared with
    whichever AI would otherwise start there;
  - a menu flow that actually strings races together as one campaign
    (a "next race" button that reuses the same human/CareerState pair)
    rather than every race being launched fresh from the garage;
  - deciding what a first-ever race (`has_last_result == 0`) should do
    — likely fall back to today's back-of-grid start.

## Laps and timing — done in v1.5.0

- [x] Show a brief per-player lap-complete popup inside that player's viewport.
- [x] Track and display individual current, previous, and best lap times for
  every human and AI driver.
- [x] Expand lap-count customization beyond the existing per-track JSON
  override, including an accessible pre-race option and sensible validation.

Still open:

- [ ] Keep lap times between races and show a session best per circuit.

## Tires and surfaces

- [x] Replace the simple soft/medium/hard choice with compounds whose tradeoffs
  matter over an entire race. *(v1.8.0: temperature window, wear, rolling
  resistance and grip fall-off, all in `settings.json`.)*
- [x] Prevent soft tires from being the automatic best choice by modeling a
  useful combination of temperature, wear, rolling resistance, durability,
  and surface compatibility. *(v1.8.0: softs win sprints, mediums and hards
  win long races; a regression test fails if that stops being true. Surface
  compatibility waits for the winter work below.)*
- [ ] Define dry, snow, and ice performance deliberately. Investigate whether
  harder compounds should gain lower rolling resistance or durability on dry
  pavement while winter-oriented/softer compounds gain cold, snow, and ice
  grip; avoid granting extra acceleration or cornering grip merely because a
  label says "hard."

## Overall direction

- [ ] Move the feel away from pure go-kart arcade racing and toward a low-poly
  formula-car experience: more deliberate setup, braking, power delivery,
  race information, driver identity, and consequence without losing readable
  controls or quick races.
