# WiiKart future work

These are design targets for releases after v1.4.0. Details may change after
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

## Roads, passes, and conditions

- [ ] Add variable road width by segment or control point. Some sharp turns
  should remain narrow and punishing; others should be tight in radius but
  wide enough to support multiple lines and meaningful speed through the turn.
- [ ] Rework the mountain-pass levels so their scenery, landmarks, elevation
  transitions, roadside detail, and silhouettes are more visually distinctive.
- [ ] Add **Breakneck Pass**: a short circuit with large elevation changes,
  narrow pavement, abrupt grade transitions, and no guardrails.
- [ ] Add **Guanella Pass**: a medium-length circuit with relatively modest
  overall elevation change, narrow pavement, no guardrails, and a
  switchback-heavy layout.
- [ ] Add winter variants of the mountain passes.
- [ ] Add localized snow and ice hazards with visible boundaries and distinct
  grip behavior.

## Powertrain, boost, and instruments

- [ ] Revisit grade and power behavior so acceleration is reduced more
  convincingly uphill while cars retain or gain speed more naturally downhill.
- [ ] Add a rechargeable nitro/boost resource and a clearly readable boost
  gauge.
- [ ] Add a JSON `turbo` capability and tuning block available only to cars
  designed to use it.
- [ ] Add per-car and optionally per-gear automatic shift ranges to `cars.json`,
  including configurable upshift/downshift points rather than only limiter
  speeds.
- [ ] Add a numerical/digital tachometer alongside the existing rev band.
- [ ] Make the tachometer band change colors as the engine moves through the
  useful range, shift window, and limiter.

## Drivers, AI, and persistent competition

- [ ] Separate driver skill from personality. Build a field containing elite
  aggressive drivers, poor drivers who overcommit, overly passive drivers,
  and dependable safe drivers rather than eleven variations of "quite good."
- [ ] Give every AI driver a unique name, character, visual identity, strengths,
  weaknesses, and stable behavior profile.
- [ ] Add a continuously updating on-screen leaderboard based on current race
  order and keep it visible without covering the useful driving view.
- [ ] Add persistent driver standings, records, and player progress across
  sessions.
- [ ] Record exceptional player laps and use their racing-line/braking data to
  improve selected NPC behavior on later runs. Include reset/export controls
  so a heroic accident does not become mandatory curriculum forever.

## Laps and timing

- [ ] Show a brief per-player lap-complete popup inside that player's viewport.
- [ ] Track and display individual current, previous, and best lap times for
  every human and AI driver.
- [ ] Expand lap-count customization beyond the existing per-track JSON
  override, including an accessible pre-race option and sensible validation.

## Tires and surfaces

- [ ] Replace the simple soft/medium/hard choice with compounds whose tradeoffs
  matter over an entire race.
- [ ] Prevent soft tires from being the automatic best choice by modeling a
  useful combination of temperature, wear, rolling resistance, durability,
  and surface compatibility.
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
