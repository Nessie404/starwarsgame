# WiiHaul changelog

## v0.1.0 — first release

The initial build: a tractor-trailer backing/pulling simulator for the
Wii, built next to WiiKart.

- Kinematic tractor + up-to-two-trailer physics (`source/truck.c`):
  bicycle-model tractor, the classic "car towing N trailers" articulation
  ODE (run honestly in reverse, so backing behavior is emergent, not
  scripted), a hard jackknife limit with recovery, real mass/power/brake
  numbers per rig, and tanker liquid-slosh handling.
- Nine scenarios modeled on real CDL skills-test maneuvers (`source/
  yard.c`): Straight Back, Offset Back Left/Right, Parallel Park, Alley
  Dock, Cone Slalom, Hill Start (real grade gravity), Haul Route, Doubles
  Dock — each with cones, boundary lines, obstacles, a graded target
  zone, and a pass/fail/Bronze-Silver-Gold score.
- A chase camera adapted from WiiKart's (`source/camera.c`): the same
  reverse-swing-to-the-nose behavior, here doubling as a backing
  simulator's "watch your trailer" view, plus distance that scales with
  rig length and a look-at that biases toward the trailer's tail while
  reversing.
- Six rigs across five trailer shapes (`config/rigs.json`): dry van,
  flatbed, tanker, low-boy, and doubles pups.
- A short campaign: each scenario unlocks after passing the one before it
  (`source/game.c`).
- Editable JSON config (`source/config.c`, `config/`): rigs, physics/
  camera tuning, and controls, with a parity test guarding against the
  compiled defaults and the shipped JSON drifting apart.
- Wii Remote (sideways grip), Nunchuk, Classic Controller, GameCube pad
  (Xbox pad through Dolphin) and USB keyboard input; procedural engine/
  event audio.
- Host-testable simulation core with five test suites (`tests/`,
  `tools/run-host-tests.sh`) and a devkitPPC CI build
  (`.github/workflows/wiihaul-build.yml`).

Scoped out for now — see `TODO.md`: mirror-view camera insets, a full
JSON-config debug screen, and further scenarios/rigs.
