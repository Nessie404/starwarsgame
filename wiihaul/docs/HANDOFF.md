# WiiHaul — handoff notes

Written so somebody else (a person, or another AI assistant) can pick
this up cold. Follows the same shape as WiiKart's own `docs/HANDOFF.md`
one directory up, which is worth reading too — this project reuses its
bones (build pipeline, GX rendering primitives, the reverse-swing chase
camera, the editable-JSON pattern) throughout.

If you are an assistant reading this: **the golden rule is the same as
WiiKart's — every change to the simulation gets a test in `tests/` that
fails without it.** Several of the bugs this project's own history
already caught (see "Practical things that will bite you" below) were
exactly the kind that "looks obviously right" until a test runs it for
more than a couple of seconds.

---

## 1. What this is

An original tractor-trailer simulator for the Nintendo Wii, homebrew in
C99, built next to WiiKart (a racing game) in the same repository. It is
not a race — it is a solo skills test modeled on real commercial
driver's license (CDL) yard-exam maneuvers: back a rig into a target
zone, or pull it through a course, without hitting cones, crossing
boundary lines, or jackknifing.

The driving model is semi-realistic: a tractor is a standard bicycle-
model car; each trailer behind it follows the classic "car towing N
trailers" kinematic ODE, run honestly with negative speed for reverse —
see the file comment at the top of `source/truck.h` before touching
anything in `rig_step`. Real mass, power, braking, a hard jackknife
limit, and tanker liquid slosh all follow from that one equation plus a
straightforward longitudinal force model.

---

## 2. How to build and test

**The tests run on any PC with gcc. You do not need a Wii toolchain.**
This is the main way to work on the game:

```sh
./tools/run-host-tests.sh
```

(or `make test`, which calls the same script). It builds and runs five
separate suites — `test_truck`, `test_yard`, `test_camera`,
`test_config`, `test_game` — one per module, rather than WiiKart's single
`test_game.c`, since the domains split more cleanly here (physics /
course geometry+scoring / camera / JSON / top-level state). If you add a
new `.c` file under `source/`, add it to whichever `run()` line(s) in
`tools/run-host-tests.sh` need it.

**The Wii build needs devkitPPC**, not installed in most dev sandboxes.
CI (`.github/workflows/wiihaul-build.yml`, at the repo root — GitHub only
discovers workflows there, not under `wiihaul/`) builds it in the
`devkitpro/devkitppc` container on every push touching `wiihaul/**`. To
check Wii-only code (`source/main.c` and the `.inc` files it pulls in)
without the toolchain, syntax-check it against stub headers the way
WiiKart's HANDOFF.md describes — real devkitPPC headers weren't
available while writing this, so `main.c`'s first real compile happens
in CI; watch that job on the first push of any change there.

**No auto-release.** Unlike WiiKart, this project's CI does not publish a
GitHub release on every push — it builds, tests and uploads a downloadable
artifact only. Wire up a release job the same way WiiKart's `build.yml`
does, if that's wanted later.

---

## 3. Map of the code

| File | What lives there |
|---|---|
| `source/truck.h` / `.c` | The physics: `TruckSpec`/`TrailerSpec`/`RigSpec` (static data), `Rig` (dynamic state), `rig_step` (the whole simulation step). Platform-independent, depends only on libm. |
| `source/yard.h` / `.c` | Course geometry and scoring: `Yard` (cones, boundary lines, obstacles, target zone, optional grade — compiled in, one `build_*` function per scenario), `OBB` collision primitives, `YardAttempt` (live score/pass-fail). |
| `source/camera.h` / `.c` | The chase camera, adapted from WiiKart's — see its own file comment for what's different (distance scales with rig length, aim biases toward the trailer's tail while reversing, corner lean reads steer angle instead of track curvature) and what's the same (the reverse-swing mechanism, verbatim). |
| `source/config.h` / `.c` | A small hand-rolled JSON reader (not WiiKart's — this one is its own, simpler recursive-descent parser sized for WiiHaul's smaller schema) for `rigs.json`/`settings.json`/`controls.json`. |
| `source/game.h` / `.c` | Ties it together: `Game` (yard + rig + camera + attempt + state), `game_init`/`game_update`, `CareerState` (campaign unlock progress). |
| `source/main.c` | Everything libogc-specific: video/GX setup, input, audio, `main()`. Pulls in the five `.inc` files below via `#include` — they are not separate translation units, just main.c split up for readability. |
| `source/draw_rig.inc` | Drawing the tractor and each trailer type (box/flatbed/tanker/low-boy/pup). |
| `source/draw_yard.inc` | Drawing the ground, obstacles, cones, boundary lines, target zone. |
| `source/hud.inc` | The 7-segment-style HUD font (copied verbatim from WiiKart — pure GX drawing, no game-specific coupling) plus WiiHaul's HUD content: speed, gear/direction, the articulation gauge, score, the pass/fail overlay. |
| `source/menu.inc` | Scene camera setup (`draw_scene`) and the three menu screens (title, scenario select, rig select) plus the pause overlay. |
| `source/frames.inc` | The two per-frame entry points `main()`'s loop calls: `menu_frame`, `attempt_frame`. |
| `config/` | Editable JSON + `README.md` explaining each field. |
| `tests/` | One host-testable suite per module — see section 2. |
| `tools/` | `run-host-tests.sh`, `pad_dol.py`/`validate_dol.py` (copied verbatim from WiiKart — pure DOL-format tooling, nothing game-specific), `run-dolphin.sh`, `gen_rigs_json.c` (regenerates `config/rigs.json` from the compiled defaults). |

---

## 4. Why this is single-player

WiiKart supports up to four humans in split screen because racing is
inherently comparative. Backing a trailer into a dock is not — there is
one rig, one driver, one score. Building four-way input translation and
split viewports for a game with nothing to race would have been pure
unused surface area, so `main.c` (and `Input`, unlike WiiKart's, which
has no player index anywhere) is single-player throughout. If two-player
support is ever wanted, `docs/HANDOFF.md`'s TODO entry on multiplayer has
the reasoning for why a co-op "spotter" role would fit the game better
than a second rig, and WiiKart's `read_player_input`/`viewport_rect`
pattern is exactly what to copy back in if a second *rig* were wanted
instead.

---

## 5. Design decisions worth knowing before you change them

**Accel, reverse and brake are three separate inputs, not two.** An
earlier draft tried WiiKart's arcade convention (one brake pedal that
becomes reverse throttle once you're stopped). It broke the moment a
test tried to *sustain* reverse motion while steering — holding "brake"
while already backing up kept re-triggering the standstill-only reverse
branch's guard and fighting itself. A trailer-backing game's entire point
is holding reverse for as long as the maneuver takes, exactly like accel
works forward, so `Input.reverse` is its own field
(`source/truck.h`). Brake is purely a decelerator now: it slows whatever
direction the rig is currently moving and never by itself creates motion.

**The target zone checks the *last* trailer, not every body.** Real
docking is graded on where the trailer ends up, not the tractor — and a
target zone sized to contain both a tractor's center point and a
trailer's center point simultaneously would have to be nearly as long as
the whole rig, since those two points sit a trailer-length-or-so apart.
`yard_attempt_update` checks `boxes[n_boxes - 1]` (the rearmost body)
against the target footprint; heading tolerance is still the tractor's
own, since that's what "aligned with the dock" means for the unit doing
the steering.

**A trailer's hitch offset is ignored for the first slot.**
`TrailerSpec.hitch_offset` is the kingpin-to-reference-point distance a
trailer hangs from — but a trailer sitting directly behind the tractor
hangs from the *tractor's own* `hitch_setback` (a property of the fifth
wheel, not the trailer), while a second trailer in a doubles combo hangs
from a converter dolly, which genuinely is a property of that trailer
slot. `rig_step` reads `TruckSpec.hitch_setback` for joint 0 and
`TrailerSpec.hitch_offset` only for joint ≥ 1 — see the comment on the
field in `truck.h` before assuming it's used uniformly.

**Steady cornering does not always have a stable off-tracking angle.**
The articulation ODE's steady-state equation reduces to (see the ODE
comment in `truck.h`) `k·L ≈ sin(articulation)` where `k` is the
tractor's curvature and `L` the trailer's length — which has no solution
once `k·L > 1`. That's not a bug: a long trailer genuinely has no stable
angle in a *sustained*, *sufficiently tight* turn, and will keep winding
out toward the jackknife limit for as long as you hold that turn. Truck
lock angles were tuned (`truck_specs_reset_defaults`) so this only
becomes reachable near full lock, not at a gentle highway curve — see
`test_offtracking_settles`'s comment in `tests/test_truck.c` for the
worked numbers, and don't be surprised if a *very* short-wheelbase
tractor (`SHORTHAUL`) pulling the long `DRY VAN 53` trailer needs more
correction than a longer-wheelbase one would for the same steering input.

**Jackknife recovery is a clamp with hysteresis, not a locked state
machine.** `rig_step` simply refuses to let `|articulation|` exceed each
joint's `jackknife_limit_deg` every frame — driving forward naturally
unwinds it because that's what the ODE does at those angles, no special
"you are now in jackknife recovery mode" branch needed. `Rig.jackknifed`
only gates two things while it's set: reverse throttle is refused (you'd
just re-trip it) and top speed is capped low (`JACKKNIFE_RECOVER_SPEED_MPS`),
and it only clears once every joint is back under
`jackknife_warn_frac` of its limit — deliberately not all the way to
zero, so recovery has a little hysteresis rather than flapping right at
the edge.

---

## 6. Practical things that will bite you

**The HUD font is missing punctuation beyond `- . : /`.** No comma, no
apostrophe. `glyph_mask`/`hud_glyph` (`source/hud.inc`, copied from
WiiKart) silently draw nothing for an unhandled character rather than
crashing — harmless, but check any new UI string against the supported
set (digits, `A–Z` including `V`/`W`/`X`/`K`/`M`/`Q`, which are drawn as
special cases before falling through to the segment-mask lookup) before
assuming it will render. `yard.c`'s scenario blurbs deliberately avoid
em dashes and apostrophes for this reason — plain hyphens only.

**`Yard` is rebuilt from scratch on every `yard_init` call, including
just to read `.name`.** `menu.inc`'s scenario-list screen calls
`yard_init` once per scenario per frame just to display its name — cheap
in absolute terms (a few dozen float ops, no allocation), but if a
scenario's `build_*` function ever grows expensive, that's the place
it'll be felt first, not gameplay.

**Config file parity is enforced, not just hoped for.**
`test_rigs_json_matches_compiled_defaults` (and its settings/controls
counterparts) in `tests/test_config.c` `memcmp`s the shipped
`config/*.json` against `truck_specs_reset_defaults()` /
`haul_settings_defaults()` / `control_config_defaults()` field for field
— run it after *any* edit to the compiled defaults in `source/truck.c`
or `source/config.c`, not just after editing the JSON. `tools/
gen_rigs_json.c` regenerates `rigs.json` from the compiled roster if
they've drifted; settings/controls don't have a generator (few enough
fields to hand-edit) but must still match exactly.

**Trailer `length` doubles as both a physics distance and a visual/
collision half-extent.** `TrailerSpec.length` is defined as the
hitch-to-axle distance for the kinematic ODE, but `rig_obbs`
(`source/yard.c`) and `draw_trailer` (`source/draw_rig.inc`) both also
use it as the trailer's visual/collision half-length. That's a
deliberate simplification (real trailers extend a bit past their axles
in both directions) rather than an oversight — if a future scenario needs
tighter collision tolerances, this is the first place the approximation
will show.
