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
  of climb, 33 corners, tightest radius 7 m.)*
- [ ] Add winter variants of the mountain passes.
- [ ] Add localized snow and ice hazards with visible boundaries and distinct
  grip behavior.

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
- [x] Add per-car and optionally per-gear automatic shift ranges to `cars.json`,
  including configurable upshift/downshift points rather than only limiter
  speeds. *(v1.7.0.)*
- [x] Add a numerical/digital tachometer alongside the existing rev band.
      *(v1.5.0, with idle and redline in `settings.json`.)*
- [x] Make the tachometer band change colors as the engine moves through the
  useful range, shift window, and limiter. *(v1.5.0: grey bogging, green
  useful, amber shift window, red limiter, plus a shift-window mark.)*

## Drivers, AI, and persistent competition

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
