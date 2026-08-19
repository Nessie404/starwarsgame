# Changelog

Notable player-facing and development changes are recorded here. Release
artifacts and their longer descriptions remain available on the
[GitHub releases page](https://github.com/Nessie404/starwarsgame/releases).

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
