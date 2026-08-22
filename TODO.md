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
  hairpins and straights instead of three evenly-spaced patches; a
  deliberate dry/snow/ice tire trade beyond today's single grip
  multiplier (rolling resistance, durability); a visible zone
  boundary/texture in `draw_track` beyond the flat color tint.

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

### v1.22.0 — difficulty, team and career wired into `game_init`

- [x] `game_init` now reads `GameConfig.difficulty`: a preset sets
  `laps_override` (when the menu hasn't already), scales AI `aggression`
  and `ai_skill` together by `DifficultyPreset.ai_aggressiveness`, and
  overrides `track.has_walls` per `DIFFICULTY_GUARDRAILS_ON`/`OFF`
  (`TRACK_DEFAULT`, NORMAL's setting, leaves a circuit's own value
  alone). `DIFFICULTY_NORMAL` is enum value 0 on purpose, so every
  existing zero-initialized `GameConfig` — every call site today,
  including every menu path in `main.c` — keeps behaving exactly as it
  always has.
- [x] AI car assignment now leans toward `DIFFICULTY_CARS_MATCHED` or
  `UNDERDOG` once a preset asks for it: `ai_choice_spec` builds a pool of
  specs judged close to, or weaker than, the human's own power-to-weight,
  falling back to the full roster if nothing qualifies. `CARS_ANY`
  (NORMAL) keeps the plain `ai_no % kart_spec_count` untouched.
- [x] `GameConfig.team_mode`/`team[]` now force each human's `paint_idx`
  to their chosen team's colour and spread the AI round-robin across the
  four teams (new `Kart.team` field, `-1` off a team-mode race); a new
  `game_team_scores` sums a combined per-team total from `final_rank`.
- [x] `game_init`'s grid placement reads `GameConfig.career[]`: a human
  with a recorded `last_finish_rank` claims that starting slot instead of
  always starting at the back; two humans claiming the same slot resolve
  to adjacent ones instead of overlapping. A human with no result yet —
  career mode unused, or their first race in it — starts at the back
  exactly as before.
- [x] None of the above is reachable in the shipped game yet — no menu
  sets any of these `GameConfig` fields away from their defaults. See
  Bundle 1 above for what's still open (the garage menu itself, a
  per-team HUD element, and `CareerState` save/load).

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
