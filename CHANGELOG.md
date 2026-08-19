# Changelog

Notable player-facing and development changes are recorded here. Release
artifacts and their longer descriptions remain available on the
[GitHub releases page](https://github.com/Nessie404/starwarsgame/releases).

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

[1.6.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.6.0
[1.5.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.5.0
[1.4.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.4.0
[1.3.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.3.0
[1.2.1]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.2.1
[1.2.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.2.0
[1.1.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.1.0
