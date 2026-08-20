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

Nothing queued right now — the three that were here (session-best lap,
the weather-ahead HUD readout, camera corner lead-in) shipped together in
v1.21.1; see the shipped-history section below. Roughly the order to work
through whatever lands here next: cheapest/most self-contained first.

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

**2. Winter, for real, across the roster.** The weather system has been
CLASSIC-only by design since v1.19.0; this is the release where that
becomes deliberate rather than just unfinished. Bundled because all three
depend on and inform each other — extending zones to new circuits is the
first real test of whether the tire compounds' snow/ice/puddle grip
numbers hold up outside the one track they were tuned on, and the visual
work only pays off once there's more than one circuit's worth of zone to
actually notice:
  - add zones (snow → ice → puddle, `track_weather_at`) to some or all of
    the seven mountain passes — its own pass over each circuit's own
    geometry, not a copy-paste of CLASSIC's three-zone layout;
  - define dry/snow/ice tire performance deliberately instead of only the
    grip-multiplier trade that exists today: investigate whether harder
    compounds should also gain lower rolling resistance or durability on
    dry pavement, whether winter-oriented/softer compounds should gain
    something beyond grip in the cold, and keep watching for a compound
    winning on label alone rather than on the modeled trade;
  - give weather zones a visible boundary/texture in `draw_track` beyond
    today's flat color tint, now that there is reason to actually notice
    a zone's edge on more than one circuit.

**3. Player history, and an AI that learns from it.** Both items are about
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

**4. A presentation pass: camera modes and pass scenery.** Lowest priority
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

### v1.24.0 — road banking, a bigger BULLRING, cars that hold a line, a rebuilt designer

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

### v1.21.0 — automatic turbo, two more challenging drivers, AI racing lines, and a follow-up patch

- [x] Reworked boost into a fully automatic turbo. `Kart.turbo_spool`
  builds and bleeds off on its own from real throttle and revs — no
  button — and applies straight to engine power every frame, tapered by
  how much grip is already spent cornering so it can't destabilize a car
  mid-corner. The "use" button is now an instantaneous full-throttle
  override instead of a resource to spend. Tunable in the `turbo` block
  of `settings.json` (was `boost`).
- [x] Added two more HOLT-tier drivers, KESSLER and DUARTE, replacing
  RENARD and SOLANO after checking which roster tests each removed
  driver was load-bearing for. See `docs/HANDOFF.md` §6 before growing
  the roster past eleven — the array-index scheme silently makes
  anything past `NUM_KARTS - 1` entries unreachable in a normal race.
- [x] Some AI drivers (OSEI, NORDLI, DUARTE) now genuinely hunt the
  racing line: `ai_line_curvature` blends in a second, farther curvature
  sample per driver's `AIDriver.line_lookahead_m`, so they set up for
  the corner after the one they're in, each reading a different
  distance ahead. That anticipation fades toward nothing the tighter
  the near corner already is — both realistic and needed, since an
  early, untapered cut of this briefly broke the same fragile
  TRUCK/AI_YOLO pairing noted below on Monarch and Berthoud Pass 2.0.
- [x] *(v1.21.1 patch)* Three small, self-contained additions landed
  together: a session-best lap per circuit that survives a `game_init`
  (in-memory only, not persisted between process runs —
  `session_best_lap_get`/`_record` in `game.c`, shown next to the
  in-race best on the HUD); a HUD readout of the next weather zone's
  condition on the approach to it, reusing `track_weather_at` with no
  new mechanic; and the chase camera leaning its aim point toward the
  signed curvature of an upcoming bend (new `cam_corner_lean` setting),
  fading out the more the view has swung round for a reverse and
  hard-clamped so a hairpin can't send it somewhere absurd.

### v1.20.0 — punishing oversteer/understeer, sharper AI, deeper weather

- [x] Understeer now scrubs speed progressively with slip instead of a
  flat rate; rear-driven cars get a new catchable power-oversteer/spin
  mechanic under throttle, separate from the handbrake drift. New
  `understeer`/`oversteer` settings blocks.
- [x] AI competitiveness pushed further: `skill_multiplier` 1.02 → 1.06,
  `braking_multiplier` 0.72 → 0.76.
- [x] Data-only scaffolding for a colour-based team mode (`TeamDef`,
  `GameConfig.team_mode`/`team[]`) and a career/campaign mode
  (`CareerState`, `GameConfig.career[]`) — neither wired into a race yet;
  see Release planning above for what finishing them needs.
- [x] Weather fleshed out further: standing water now drags at every car
  (`weather_puddle_drag_mult`), and the AI's own corner-speed lookahead
  discounts grip for whatever weather patch is ahead instead of
  assuming dry pavement everywhere.

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
