# WiiKart future work

These are design targets for upcoming releases. Details may change after
testing, because apparently cars, mountains, and tires all object to being
reduced to one convenient slider.

## How this list is organized

Open work below is sorted by **release size**, not by system, because that's
the decision that actually needs making next: what ships alone in a quick
patch versus what waits to go out bundled with related work in a real
feature release.

The shipped-history section further down is a rolling window of the last
five **minor** releases (x.Y.0) only, by version rather than by system —
engineering context for what just landed and why, not a full project
history (`CHANGELOG.md` and the `docs/release-notes/` files are the
permanent record of everything back to v1.0). A patch release (x.y.Z) does
not get its own entry there — it folds into the entry for whichever minor
release it followed, same as it folds into that release's bundle of
changes below. When a new minor release ships, add it to the top of that
section and drop the now-sixth-oldest minor release off the bottom, so it
stays exactly five deep; a patch release just adds to the current top
entry in place.

**Small (patch, x.y.Z) releases** are for a single narrow, low-risk,
self-contained change — the kind v1.19.1 (Berthoud's switchbacks) and
v1.19.2 (a roster-drift bug fix) already were. Even so, don't cut a release
for just one of these the moment it's done: let two or three land on `TODO.md`
as finished, cumulative changes, and ship them together. A version bump is
not free — it's a changelog entry, a version/date bump in three files, and
a reader's attention — so it should carry more than one line of value. It
also doesn't get its own release-notes doc or shipped-history entry (see
above) — its changes just get folded into the most recent minor release's.

**Big (minor, x.Y.0) releases** are for a handful of related items landed
together on purpose, the way v1.19.0 bundled weather + boost + a wilder AI +
wider corners, v1.20.0 bundled the oversteer rework + AI competitiveness +
two scaffolds + weather depth, and v1.21.0 bundled the turbo rework + two
new drivers + AI racing lines. The bundles below are grouped because the
items in each one touch the same code, the same menu surface, or the same
underlying subsystem — doing them together means paying the "learn this
part of the codebase again" cost once instead of three times, not just
batching unrelated work to make a bigger changelog.

### Small releases — ship alone, batch two or three before pushing

- **Weather depth.** Every circuit now runs the same three-zone layout
  (v1.26.0 — see below), reusing CLASSIC's original snow → ice → puddle
  schedule scaled by each track's own point count rather than a bespoke
  pass over each circuit's geometry. Left for later, once there's reason
  to revisit it: hand-placed zones that actually respect each pass's own
  hairpins and straights instead of three evenly-spaced patches; now that
  v1.27.0 gave dry-road traction its own compound split (see below), a
  further wet-weather durability/rolling-resistance trade beyond today's
  single grip multiplier per condition; a visible zone boundary/texture
  in `draw_track` beyond the flat color tint.
- **A real per-wheel suspension model.** v1.27.0's "shocks" (see below)
  are a single chassis-wide low-pass filter on bank and grade — no
  vertical wheel travel, no spring rate or damper coefficient, no
  per-corner weight transfer under braking or turn-in. A genuine
  suspension model (four independent wheel loads, pitch/roll from
  weight transfer feeding back into per-tire grip) is a much bigger
  undertaking than the filter that stands in for it today, and would
  probably want its own designer stats (spring rate, damping) rather
  than reusing `chassis_settle_rate`.
- **A real downforce/aero stat.** `designer_grip()`'s drag term got its
  sign flipped in v1.27.0 (lower `cd_a` now means more grip, matching a
  road car's blunt-frontal-area drag rather than a wing), but drag area
  is still the only aero number a car has. There's no separate downforce
  stat for a hypothetical open-wheel/wing car that actually wants more
  drag in exchange for more grip — today every car is implicitly a
  road car aerodynamically. Worth a dedicated stat only if a future car
  archetype actually wants that trade-off back.

- **Carry the switchback-wide/straight-taper width blueprint to the rest
  of the roster.** BERTHOUD and LOVELAND (`W_BERTHOUD`/`W_LOVELAND` in
  `track.c`) now follow a specific rule: every named switchback/hairpin
  apex gets a wide multiplier (1.32), every control point whose own
  3-point circumradius reads as a genuine straight (>= 150 m) tapers
  (0.85), everything else stays nominal (1.00) — see the comment above
  `W_BERTHOUD` for the exact reasoning. MONARCH, BREAKNECK and GUANELLA
  still run the older width philosophy the blueprint replaced (tightest
  corners narrow, a couple of others opened out, straights nominal) —
  each already has a `cp_width` array and a "tightest control points"
  comment identifying its own switchback indices, so converting them is
  mostly re-deriving the same wide/taper split from radii already
  measured, not remeasuring from scratch. KENOSHA, BERTHOUD 2.0 and
  BULLRING have no `cp_width` at all yet (`NULL`, constant width the
  whole lap) — BULLRING's two long straights and constant-radius sweeps
  are the most obviously suited to a taper/no-taper split of the four
  once someone gets to it. Whatever multiplier any of them ends up with,
  v1.27.1's absolute `TRACK_MIN_FULL_WIDTH_M`/`TRACK_MAX_FULL_WIDTH_M`
  clamp (`game.h`, enforced in `track_init_with_settings`) now backstops
  it automatically — converting these three is lower-risk than it was,
  since no width choice can actually leave the 3-10 car-width band by
  mistake. KENOSHA and BERTHOUD 2.0 both already sit at the 10-car-width
  ceiling uniformly (their unvaried `road_half` was already right at the
  edge); giving them their own wide/taper profile would be the first
  time either circuit's width varies within a lap at all.

Roughly the order to work through whatever lands here next: cheapest/most
self-contained first.

### Big releases — bundle before shipping, roughly in priority order

**1. Finish wiring up the three scaffolds (difficulty, team, career).**
Highest priority of the big items. v1.22.0 did the `game.c`/`game.h` half —
`game_init` genuinely reads `cfg.difficulty`, `cfg.team_mode`/`team[]` and
`cfg.career[]` now (see the shipped-history entry below for what that
covers). What's left is the reason none of it does anything in the shipped
game yet: there is still no menu path that ever sets any of those fields
away from their zero/off defaults. Concretely:
  - a garage/setup menu screen with controls for a difficulty preset, a
    team, and (once a campaign exists) continuing one — the one piece that
    actually lets a player reach any of this;
  - a HUD element for the per-team score (`game_team_scores` computes it
    already), and a decision on whether team mates get any in-race
    awareness of each other in `ai_control` (today a team mate is just
    another rival to the strategy code);
  - save/load I/O (an SD card file, most likely) to persist `CareerState`
    across sessions — right now it only survives as long as the calling
    code keeps carrying it from one `game_init` to the next in memory —
    and a "next race" menu flow that actually strings races together as
    one campaign.

**2. Player history, and an AI that learns from it.** Both items are about
capturing and persisting player performance over time, and the first is
close to a prerequisite for the second (you need a place to keep lap data
before you can mine it):
  - persistent driver standings, records, and player progress across
    sessions — the real save/load subsystem the career scaffold above also
    wants, so worth designing once for both if bundle 1 hasn't already
    landed it;
  - record exceptional player laps and use their racing-line/braking data
    to improve selected NPC behavior on later runs, with reset/export
    controls so a heroic accident does not become mandatory curriculum
    forever.

**3. A presentation pass: camera modes and pass scenery.** Lowest priority
of the four — both items are about how the game looks rather than how it
races, which has consistently been this project's second priority behind
simulation depth (see Overall direction, below). Bundled because both are
rendering-heavy, `main.c`-side work rather than `game.c` simulation work,
so it's one release spent in rendering instead of splitting that context
switch across two:
  - optional cockpit and bumper camera views, sharing the existing pitch
    and look-ahead settings;
  - rework the mountain passes' scenery, landmarks, elevation transitions,
    roadside detail, and silhouettes to be more visually distinctive from
    each other — right now they mostly read as the same road at different
    grades.

---

## Shipped in the last five minor releases

A rolling window, newest first — see "How this list is organized" above.
Any patch release that followed a minor release is folded into that
release's entry rather than getting its own. For anything older,
`CHANGELOG.md` and `docs/release-notes/` have the full record back to v1.0.

### v1.27.0 — the combined friction circle, engine braking, traction control, and a fourth aspiration (plus a v1.27.1 follow-up patch)

- [x] Braking and cornering now share one grip budget instead of two
  unlimited ones: `kart_step`'s `yaw_cap` shrinks with however much
  longitudinal force (braking, accelerating, or engine braking) a car
  is asking of its tires that same frame (`mu_lat = sqrt(max(0, mu_a² -
  (a_long_use * friction_circle_strength)²))`). Brake late and turn in
  hard at the same time and the car pushes wide; get the braking done
  early and there's more grip left for the corner. One physics change,
  no AI decision-logic changes — every driver on the grid, human or AI,
  is subject to it identically.
- [x] Lifting off the gas now triggers real engine braking
  (`engine_brake_decel * clamp(rev_frac, 0, 1)`, whenever `!accel`,
  stacking with the brake pedal) instead of coasting on drag and
  rolling resistance alone — no more golf-cart coasting down a
  descent. A simple traction control system rides on the acceleration
  cap itself (`traction_frac * mu_trac * (1 - tc_strength * slip)`),
  trimming power automatically the instant the tires read as already
  sliding.
- [x] A new `mu_trac` grip budget, parallel to the existing cornering
  `mu_a`, drives acceleration/braking/engine-braking specifically: it
  reads the new dry-specialist `tire_traction_now` table instead of
  `tire_grip_now`, and (unlike `mu_a`) has no banking term, since
  banking only ever helped cornering.
- [x] Braking distance is no longer a free designer dial: `kart_step`
  doesn't read `KartSpec.brake_dist_100` at all any more — braking
  deceleration is `mu_trac` directly, the same traction figure
  everything else already uses. Because braking force and inertia both
  scale with mass and cancel, this makes stopping distance
  mass-independent, same as a real car's. `spec_brake_dist_100_with_
  settings` (mirrors the existing `spec_top_speed_with_settings`/
  `spec_accel_time_with_settings` pattern) derives the number the
  garage and designer display; `brake_dist_100` stays in `KartSpec`/
  `cars.json` for backward compatibility only, always overwritten by
  the derived value.
- [x] `designer_grip()`'s drag term flipped sign: less drag area now
  means more grip (previously more `cd_a` read as more assumed
  downforce). Justification: these are road cars and karts, where drag
  is just blunt frontal area disturbing the air around the contact
  patches too, not a wing — the opposite trade would fit an open-wheel
  car with real downforce, which this roster doesn't have (see the new
  TODO item above). Mass's side of the grip formula is unchanged:
  lighter is still grippier, which matches real tire load
  sensitivity — peak coefficient of friction measurably drops as
  per-tire normal load rises.
- [x] Tires now specialize for real: the existing `tire_grip_mult`
  (cornering) is joined by a new `tire_traction_mult` (dry-road
  accel/braking) that inverts the ranking — hard is now the best dry
  tire in a straight line, soft the worst, the genuine trade for less
  outright cornering stick. Every weather table (`weather_snow_grip`/
  `weather_ice_grip`/`weather_puddle_grip`) now uniformly favors soft
  and disfavors hard, including a flip of the puddle table specifically
  (hard used to be the best standing-water tire; it's now the worst,
  same as snow and ice — no tread to fall back on once the dry-road
  advantage stops applying).
- [x] A fourth aspiration, twin-turbo: shares turbo's exact spool/lag
  curve but with a bigger bonus multiplier (`twin_turbo_power_bonus_
  mult`, 1.45x a single turbo's). Every non-NA aspiration now costs
  curb weight in `designer_mass()` (turbo +35 kg, supercharged +28 kg,
  twin-turbo +65 kg heaviest of the four) — on top of whatever the
  underlying horsepower already costs — finally making "build a bigger
  NA engine instead" a real, competitive choice rather than a strictly
  worse one.
- [x] The "use" button now floors the throttle instead of spending a
  resource: an instant turbo boost if one's already spooled, or — on
  an automatic gearbox with nothing spooled to lean on
  (`turbo_spool < boost_kickdown_spool_thresh`) — a genuine kickdown,
  forcing a downshift only when a lower gear's power-curve position
  (`gear_power_scale_rpm`) actually makes strictly more power right now
  than the current gear does, the way a real automatic's passing gear
  works. Manual-gearbox shift controls already defaulted to the left/
  right bumpers (GameCube L/R, Xbox-via-Dolphin LB/RB, Classic
  Controller ZL/ZR) from an earlier session — verified, not changed.
- [x] Brake lights: a new one-frame `Kart.braking` flag (the resolved
  brake input, after any FLOOR-IT override cancels it) drives small red
  rear quads in `draw_car_model` — bright under real braking, dim
  otherwise, and specifically not lit for engine braking or a
  kickdown-cancelled brake press.
- [x] A rudimentary "shocks" system: `Kart.bank_filt`/`grade_filt` now
  low-pass-filter the track's actual bank/grade at each point
  (`chassis_settle_rate`, 8.0/s) before either feeds `mu_a`'s banking
  term or the grade/load calculation, instead of reading the raw
  track values every frame. Deliberately not a real per-wheel
  suspension model — no vertical wheel travel, no spring rate, no
  weight transfer — see the new TODO item above for what a fuller
  version would need.
- [x] *(v1.27.1 patch)* BERTHOUD and LOVELAND's width profiles reworked:
  wide at every named switchback/hairpin apex, tapered on genuine
  straights, nominal in between — documented as a blueprint in TODO.md
  for the rest of the roster. Alongside it, a new absolute floor and
  ceiling on road width itself (`TRACK_MIN_FULL_WIDTH_M`/`_MAX_`, 3 and
  10 car widths, `game.h`), enforced per-point in
  `track_init_with_settings` regardless of any `cp_width` profile,
  `road_half`, or `track_width_mult` setting — catching three circuits
  (KENOSHA, BERTHOUD 2.0, BULLRING) that were already sitting right at
  the 10-car-width edge unclamped, and holding LOVELAND's new wide
  switchbacks to that same ceiling.

### v1.26.0 — weather everywhere (with a toggle), and skill that shows up on track (plus a v1.26.1 follow-up patch)

- [x] Weather is no longer CLASSIC-only. `track_init` builds the same
  three-zone snow → ice → puddle layout for every circuit, scaled by
  each track's own point count instead of a fixed segment count, so a
  longer or shorter lap just spreads the same three patches over more
  or less road. A new `GameConfig.weather` toggle (`WEATHER_TOGGLE_OFF`/
  `_ON`, off by default) decides whether a race actually sees any of
  it: `game_init` blanks every zone back to bare pavement when it's
  off, so `track_weather_at` never finds anything regardless of what
  `track_init` built. Wired into the setup menu as a WEATHER row next
  to LAPS.
- [x] AI skill now visibly shows up as line quality, not just cornering
  speed. `ai_skill01()` normalizes `Kart.ai_skill` (already folding in
  the driver's own rating, `ai_skill_mult`, and the difficulty preset)
  to 0..1 across the roster's practical range, and two places now read
  it: the rate `k->line_target` eases onto `ai_tactical_line`'s ideal
  apex line (1.1..2.6 /s — a low-skill driver is still visibly settling
  into a corner a sharp one already committed to), and the `capitalize`
  multiplier (0.55..1.20) on the defend/attack tactical-line
  contributions (a low-skill DEFENDER still tries to cover the door,
  just half-heartedly). The base racing line itself is unchanged: every
  driver already read the track's own signed curvature rather than a
  hardcoded line (see v1.21.0's `ai_line_curvature`) — this is skill
  determining how well that read gets executed, not a new line source.
- [x] *(v1.26.1 patch)* A second no-guts-no-glory driver, VOSS, replaces
  PETRAN (BALANCED, no test depended on it): same YOLO commitment as
  TANAKA, genuinely quick (skill on par with DUARTE/IBARRA), but
  deliberately sloppier (lower consistency) — falls more, sends it
  anyway. HOLT, KESSLER, IBARRA, DUARTE and CROSS all nudged a little
  more toward that same commitment (aggression up, consistency down);
  BASTIEN and OSEI — the roster's only two `test_driver_field_has_characters`
  "dependable" drivers — moved too, just short of that bin's aggression
  ceiling and with consistency left alone to protect its floor. NORDLI
  and DELGADO, the field's two cautious CRUISERs, are untouched.

### v1.25.0 — grip until it lets go, real gear physics, editable cars, imperial units

- [x] The escalating power-oversteer/forced-spin mechanic (v1.20.0) is
  gone. Holding the wheel over used to boost `yaw_cap` by up to 40% over
  about a second and then force an uncontrollable spin if it wasn't
  "caught" — reported as the car suddenly turning far harder than asked
  and snapping off the road. Grip now just holds (understeer scrub +
  the tire "shoulder" from v1.24.0) until the driver deliberately breaks
  it loose with the handbrake (`k->drifting`) — that is drifting now,
  not something that could happen by accident. `yaw_cap`'s speed floor
  also moved 0.5 → 6.0 m/s: below a walking pace, steering geometry is
  the real limit, not grip, and dividing by a near-zero speed let the
  cap run away as a car scrubbed off nearly all its speed (e.g. pinned
  against a guardrail). The guardrail's own speed penalty was softened
  2.5 → 1.2 /s to match.
- [x] Gears are RPM-based now, not a percentage of each gear's own span.
  `KartSpec.auto_up`/`auto_down` (per-gear shift-point fractions) are
  gone, replaced by a single `nominal_rpm` — one engine curve, same RPM
  peak in every gear, exactly like a real engine (`gear_power_scale_rpm`
  in game.c). Output peaks at `nominal_rpm` and falls away — steeply,
  not gently — to both idle and redline, floored at 22% so a car can
  still launch and limp, with a real hard cliff at the limiter itself.
  Automatic shifts now derive straight from where `nominal_rpm` sits
  relative to redline — there is no separate shift-point dial to tune
  any more.
- [x] Cars can be naturally aspirated (default), turbocharged, or
  supercharged (`KartSpec.aspiration`). NA makes no forced-induction
  bonus at all. A turbo keeps the existing spool mechanic (builds and
  bleeds off over real time — genuine lag) and has the biggest bonus of
  the three. A supercharger is driven off the engine directly: instant,
  proportional to revs right now, no lag building up or bleeding off,
  for a smaller bonus than a turbo's — the real trade-off between them.
- [x] Grip is no longer a free dial in the car designer: `designer_grip()`
  derives `lat_g` from the mass the power trade-off already settled on
  (lighter is grippier) and from drag (more `cd_a` reads as more assumed
  downforce, not just more drag), the same "trade-off, not an
  independent slider" treatment mass got in v1.24.0. The GRIP row still
  shows the number, it just can't be dragged around any more.
- [x] The garage has a new EDIT CAR row: pick any car, including a
  built-in, and it opens in the same designer view with every stat
  editable — the only difference from a new build is that SAVE (now
  labelled APPLY, SESSION ONLY) patches that car's live `kart_specs[]`
  entry in place instead of adding a new one, and nothing is written to
  disk. Restarting the game restores the original.
- [x] The slowing-down lap no longer ends in a dead stop: a finished
  car ramps down to a ~30 mph cruise (`COOLDOWN_CRUISE_MPS`) over
  `COOLDOWN_SECONDS` and holds it indefinitely, actively driven rather
  than left to coast. `Kart.parked`, which used to force the car to a
  standstill, is gone — nothing needs it any more.
- [x] Every in-game speed, distance and weight readout is imperial now
  (mph, feet, pounds) — the HUD speedometer, gear top speeds and
  nominal RPM in the designer, brake distance, car mass, and track
  length/elevation/finish-line countdown on the menu and HUD. The
  simulation itself stays entirely in SI underneath; only the display
  layer (`main.c`) converts, via `MPS_TO_MPH`/`KPH_TO_MPH`/`M_TO_FT`/
  `KG_TO_LB`.

### v1.24.0 — road banking, a bigger BULLRING, cars that hold a line, a rebuilt designer (plus a v1.24.1 follow-up patch)

- [x] Every circuit now cants into its curves: a small, realistic
  crown on the mountain passes (`bank_mult` defaults to 0.3), a real,
  deliberately much stronger bank on BULLRING's own turns (`bank_mult`
  8.0). `Track.bank` is derived straight from `curv_signed` in
  `track_init_with_settings`, so it ramps in and out with the bend
  rather than switching on at its edges. Not just cosmetic: `kart_step`
  adds `GRAVITY * tan(bank)` to the cornering grip budget (`mu_a`), the
  same reason a real banked turn lets you carry more speed through it.
- [x] BULLRING's two straights doubled, 200 m to 400 m each
  (`CP_BULLRING` in `track.c`) — total lap length 838 m to 1238 m.
- [x] Cornering physics reworked in `kart_step`: past the nominal grip
  limit there is now a "tire shoulder" (`TIRE_SHOULDER`, 12%) where a
  car pushed harder genuinely turns tighter instead of being clamped,
  before it truly lets go. A real spin (not just understeer) now has
  two distinct causes: big torque on lock in a tight turn (RWD at a
  modest commitment, AWD only at a much harder one, FWD never — it
  pushes wide instead), or carrying too much speed into a corner,
  independent of drivetrain or throttle.
- [x] The in-game car designer reworked: mass is no longer a free
  dial — it now derives from power (`DESIGNER_MASS_BASE +
  power_hp * DESIGNER_MASS_PER_HP` in `main.c`), a real trade-off
  instead of an independent choice. Gear count and each gear's own top
  speed and upshift/downshift point are now directly, granularly
  editable (`RK_DES_GEARCOUNT`/`GEAR_SEL`/`GEAR_TOP`/`GEAR_UP`/
  `GEAR_DOWN`), replacing the old fixed, auto-derived five-gear ladder.
- [x] `draw_car_model` now varies a car's on-track and in-garage
  proportions with its actual spec (wheelbase, mass, drag, drivetrain)
  instead of one fixed shape in different paint; the driven axle also
  carries visibly bigger tires.
- [x] The local player's own kart on the minimap now carries a pulsing
  white ring (`draw_minimap`'s new `highlight` parameter), reachable
  in the single-player case where "the player" is unambiguous.
- [x] **v1.24.1 patch:** three follow-up fixes once the above landed.
  Cars now roll with the road on a banked section (`draw_box` gained a
  `roll` parameter; `draw_kart` feeds it `t->bank[k->seg]`, the same
  value the road surface itself is tilted by) — previously only yaw and
  pitch were ever applied to a car model, so it stayed dead level
  through a bank. The flat background "ground" quad no longer clips
  through BULLRING's banked turns, where the road's inside edge can dip
  more than a metre below it and the outside edge rises clear above it
  — non-alpine tracks now get a per-segment fill quad closing both gaps
  (collapses to nothing on an unbanked straight). And BULLRING/CLASSIC
  finally render guardrails: the drawing code lived entirely inside
  `if (t->alpine)` even though both tracks set `has_walls = 1`, so the
  flag never actually drew anything; guardrail/cliff-edge drawing is
  now gated on `has_walls` on its own.

### v1.23.0 — BULLRING, two oval cars, smoother corners, and a car designer

- [x] A ninth circuit, BULLRING: flat, wide, barriered, generated from
  its own geometry (two straights, two constant-radius sweeping turns)
  rather than hand-drawn, so it has no abrupt curves anywhere on the
  lap. New `Track.grandstands` flag (`main.c`'s `place_scenery`/
  `draw_grandstands`) draws grandstands along both straights.
- [x] Two new cars tuned for it, STOCKER and SLIPSTREAM — huge power
  and low drag for the straights, enough grip to hold BULLRING's
  sweepers without banking to lean on. Both had to be retuned down
  from their first cut (more power, less grip) after it stranded an
  AI in a fall loop on MONARCH's tightest hairpin; trading some power
  for more grip fixed that *and* made them faster on BULLRING, not
  slower — see `docs/HANDOFF.md` §6.
- [x] Every circuit but MONARCH got a wider corner-easing pass in
  `track_init_with_settings` (a 5-point blend instead of 3), softening
  how sharply curvature ramps into and out of a bend without eroding
  a genuinely tight apex — Berthoud 2.0's and Guanella's hairpins are
  unchanged. MONARCH keeps the original pass: the wider blend is the
  other thing that stranded an AI in a fall loop there, for the same
  underlying reason as the paragraph above (see `docs/HANDOFF.md` §6
  — MONARCH has essentially no margin for any geometry change).
- [x] An in-game car designer, reachable from the main menu as DESIGN
  A CAR: pick a name, dial in mass, power, brakes, grip, drag, dirt
  grip and drivetrain, see a live preview of the derived top speed and
  0-100 time, then SAVE it into the garage for the session and — if
  `cars.json` was actually found — back to disk too.
  `kart_spec_validate`/`kart_specs_add_custom` (`game.c`) do the
  validate-and-append; `config_write_cars_text`/`config_save_cars_file`
  (`config.c`) do the write. `MAX_KART_SPECS` raised 16 → 24 for
  headroom.

## Overall direction

- [ ] Move the feel away from pure go-kart arcade racing and toward a low-poly
  formula-car experience: more deliberate setup, braking, power delivery,
  race information, driver identity, and consequence without losing readable
  controls or quick races.

This is the standing north star, not a release-sized item on its own — it's
the reason simulation-depth work (tires, drivetrain, oversteer/understeer,
weather, AI) has consistently outpaced presentation work (camera modes,
scenery) in priority, and it's worth weighing any new idea against directly:
does this make the car and the mountain feel more real, or just add a knob.
