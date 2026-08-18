# Changelog

Notable player-facing and development changes are recorded here. Release
artifacts and their longer descriptions remain available on the
[GitHub releases page](https://github.com/Nessie404/starwarsgame/releases).

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

[1.3.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.3.0
[1.2.1]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.2.1
[1.2.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.2.0
[1.1.0]: https://github.com/Nessie404/starwarsgame/releases/tag/v1.1.0
