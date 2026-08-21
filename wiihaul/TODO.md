# WiiHaul future work

Scoped out of v0.1.0 on purpose, to ship a genuinely complete first
release rather than a half-finished bigger one. Roughly in the order
they'd pay off.

## Mirror-view camera insets

`camera.h`'s `mirror_camera` already computes a fixed, backward-and-
outward-facing eye/look pair per side (driver's-side and passenger-side),
matching how a real trucker actually watches their trailer while backing
— but nothing in `main.c` renders it yet. WiiKart's split-screen code
(`viewport_rect`, and `draw_scene_for_player` called once per viewport)
is the pattern to follow: two small inset `GX_SetViewport` rects in the
bottom corners, each doing its own `guPerspective`/`guLookAt`/draw pass
using `mirror_camera`'s eye/look instead of `CameraState`. Toggle-able
from `ControlConfig.show_input_overlay`-style setting, since it costs two
extra scene draws a frame.

## A full JSON CONFIG screen

`main.c` already tracks `config_file_status[3]`, `config_where` and
`config_proof` (mirroring WiiKart's), but only `config_banner` (and now
`config_detail` on an error) actually reaches the screen. WiiKart's JSON
CONFIG menu screen is the template: a per-file LOADED/error row, the
folder it read from, and one fact you can check against the file you
edited.

## More scenarios and rigs

The nine scenarios cover the real CDL maneuvers this project set out to
model; there is room for more without inventing new mechanics:

- A **serpentine backing** course (reversing through a longer weave, not
  just forward).
- A **loading dock ramp** (a short, steep grade right at the target
  zone — combines Hill Start's grade handling with a docking target).
- A **triples** combo once a second `TRAILER_PUP`-chained rig is wanted —
  `rig_step`'s N-trailer chain already generalizes past two, only
  `MAX_TRAILERS` (`source/truck.h`) needs raising and a third slot added
  to `RigSpec`.

## Weather / low-grip surfaces

WiiKart's weather system (snow → ice → puddle, different tire compounds
favored at each stage) doesn't have an equivalent here. A gravel-yard or
rain scenario lowering `rolling_resistance`/effective grip at low speed
would fit this game's "semi-realistic" goal well, but needs its own
tuning pass rather than a straight port — trucks don't have WiiKart's
tire-compound system to key it off of.

## Multiplayer

Deliberately single-player for v0.1.0 (see `docs/HANDOFF.md`). A second
player as a "spotter" giving directions (rather than a second rig) would
fit the game's theme better than head-to-head play, if this is ever
picked up — real yard work is usually a two-person job for exactly the
maneuvers this game covers.

## Persisted campaign progress

`CareerState` (`source/game.h`) lives only as long as the process does —
same limitation WiiKart's own `CareerState` has, and for the same reason:
no SD-card save/load I/O exists yet. `config_load_*_file`'s pattern in
`source/config.c` is the template for a `career_save`/`career_load` pair
if this is worth adding.
