# WiiHaul editable configuration

Put an edited copy of any of these next to `wiihaul.dol` on your SD card
(`sd:/apps/wiihaul/config/…`) and the game reads it instead of its
compiled-in defaults. The in-game JSON CONFIG screen on the main menu
tells you what was actually read. Opening the DOL directly with no
virtual SD card set up in Dolphin — the common case — falls back to the
compiled defaults, so the game is always playable either way.

- **rigs.json** — every tractor (`trucks`), trailer (`trailers`) and the
  rig combos built from them (`rigs`, which references a truck and one
  or two trailers by name). Add a truck or trailer here and it shows up
  in the garage; add a rig combining ones that already exist and it
  shows up in the rig picker.

  Trailer `type` is one of `box`, `flatbed`, `tanker`, `lowboy`, `pup` —
  it picks the trailer's shape when drawn and, for `tanker`, turns on
  the liquid-slosh handling (`slosh_strength`). `hitch_offset` only
  matters for a trailer used as the *second* trailer in a doubles combo
  (a converter dolly's tongue length) — the first trailer always hangs
  off the truck's own `hitch_setback` instead, since that is a property
  of the fifth wheel, not the trailer.

- **settings.json** — the tuning behind steering feel, engine/brake
  response, the jackknife thresholds, the tanker slosh, and the chase
  camera. See the comments on `HaulSettings` in `source/truck.h` for
  what each field actually does.

- **controls.json** — keyboard, GameCube-pad and on-screen Xbox-label
  bindings per action (`source/config.h`'s `CONTROL_*` enum), plus
  whether the input overlay is shown.

All three are validated as a whole document before anything is applied:
a broken file leaves the previous (or compiled-in) values untouched
rather than half-applying garbage, and the JSON CONFIG screen will say
so. `tools/gen_rigs_json.c` is how `rigs.json` was first generated from
the compiled defaults, kept in sync by
`test_rigs_json_matches_compiled_defaults` (and its settings/controls
counterparts) in `tests/test_config.c` — if you edit the compiled
defaults in `source/truck.c`, regenerate or hand-edit the JSON to match,
or that test will catch the drift.
