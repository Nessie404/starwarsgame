# WiiKart

An original racing game built as **Nintendo Wii homebrew**. It compiles to
a real Wii executable (`wiikart.dol`) that runs in the
[Dolphin](https://dolphin-emu.org/) Wii emulator on your PC, and on real
Wii hardware through the Homebrew Channel.

**Download:** grab `wiikart.zip` from the
[latest release](https://github.com/Nessie404/starwarsgame/releases/latest),
unzip, and open `wiikart.dol` in Dolphin. Older lines are published too:
[v1.2.1](https://github.com/Nessie404/starwarsgame/releases/tag/v1.2.1),
[v1.1.0](https://github.com/Nessie404/starwarsgame/releases/tag/v1.1.0),
[v1.0.1](https://github.com/Nessie404/starwarsgame/releases/tag/v1.0.1) and
the original arcade racer as
[`wiikart-0.1.1.zip`](https://github.com/Nessie404/starwarsgame/releases/download/v0.1/wiikart-0.1.1.zip)
on the [v0.1 release](https://github.com/Nessie404/starwarsgame/releases/tag/v0.1)
(see `patches/README.md` for why 0.1.1 has no tag of its own).

See [CHANGELOG.md](CHANGELOG.md) for the release-by-release inventory and
[TODO.md](TODO.md) for proposed future work that is explicitly not part of
v1.10.0, and [docs/HANDOFF.md](docs/HANDOFF.md) if you are picking this
project up to work on it.

WiiKart is a semi-sim mountain racer: pick a car in the garage — its
gearbox, tires and paint, all defined by real-world performance numbers —
then race a **field of eleven AI drivers who each race differently, shift
differently, learn from their mistakes, and adapt to you**, over eight
circuits including seven stylized Colorado passes with real grades,
switchbacks, cliffs and gravity. Lap counts are set per circuit (2–4 laps
by default) so short circuits remain multi-lap. Think arcade fun with a
driving model that expects you to brake for the hairpins.

**Version history**, oldest first:

- **v0.1** — the original arcade kart racer. **v0.1.1** patches it with
  sound and moves steering onto WASD, including the steering fix.
- **v1.0** — the driving-model rewrite: real physics, the garage, WASD,
  the Colorado passes. **v1.0.1** fixes its inverted steering.
- **v1.1.0** — the twelve-car field, each AI with its own strategy,
  learning from its mistakes and adapting to you; Berthoud rebuilt as a
  19-corner technical circuit; nitro and boost pads replaced with
  motorsport power-ups; the remaining arcade cheats removed; and
  **inverted steering fixed** — left really is left now, on every device.
- **v1.2.0** — gearboxes and per-driver shifting strategies, two more
  passes (Kenosha and Monarch), guardrails gone from Loveland and
  Monarch, cliffs with checkpoint recovery, per-circuit lap counts, a
  rebuilt menu you can back out of, a controller-count check, more garage
  options, and two-player keyboard defaults.
- **v1.2.1** — the stability pass: falling off the edge just before the
  line no longer hands out most of a free lap, a finished car coasts
  instead of standing on the brakes and reversing back through the field,
  keyboard menu keys register every tap, the controller check notices
  whether a keyboard is actually plugged in, and the drop off a cliff is
  visible again.
- **v1.3.0** — editable JSON car/settings/control files, a live
  keyboard/Xbox/Dolphin input translator, wider roads, intentionally
  fallible aggressive AI, a longer and harder Monarch, and a staged
  blackout/fade/invincibility recovery after a cliff fall.
- **v1.4.0** — a rebuilt chase camera that swings round to the nose when
  you reverse, follows the road over crests and down descents, and is
  configurable end to end; plus 480p progressive output and true 16:9
  rendering instead of a stretched 4:3 picture.
- **v1.5.0** — lap times for every driver with a per-viewport lap popup, a
  live leaderboard, names for the eleven rivals, a pre-race lap-count
  option, and a digital tachometer with a rev band that means something.
- **v1.6.0** — roads whose width varies along the lap, so a hairpin can be
  pinched while another is opened out for two lines, plus Breakneck Pass
  (short, steep, stepped) and Guanella Pass (seven hairpins).
- **v1.7.0** — hills that behave: gravity and tire load taken from the
  real road angle, tunable from `settings.json`, and automatic shift
  points a car can set for itself, per gear if it likes.
- **v1.8.0** — tires with temperature and wear, so softs win sprints and
  lose long races, with a readout on the HUD and every parameter in
  `settings.json`.
- **v1.9.0** — a finish line you can see, on the minimap and in the race
  view, and a JSON CONFIG screen that tells you whether your edited files
  were actually read.
- **v1.10.0** — the rivals become people: skill, nerve, aggression and
  how hard they are on their tires, separate from the way they drive.
- **v1.11.0** — the fix for a build defect that could make a whole release
  refuse to boot on some Dolphin installs (unrounded DOL section sizes),
  plus an AI takeover so a finished car coasts to a stop instead of being
  left with no driver. **v1.11.1** rounds the section sizes properly —
  v1.11.0 only fixed the file-length half of the same defect.
- **v1.12.0** — a marshal helicopter for driving the wrong way, judged on
  net progress along the track rather than heading, and a quieter HUD.
- **v1.13.0** — the AI drives an actual racing line now instead of holding
  a fixed lateral offset all lap, so pace comes from skill and not from
  which strategy sheet a driver happened to be dealt.
- **v1.14.0** — a rechargeable turbo: hold the button for real extra
  engine power while the charge lasts, and it only recharges off the
  throttle. Gated per car by a `turbo` block in `cars.json`.
- **v1.15.0** — boost moves off the track and into the engine entirely:
  every car has an aspiration (naturally aspirated, turbocharged, or
  supercharged) instead of a push-to-pass power-up to collect. Adds
  Berthoud Pass 2.0, an eighth circuit built from the real pass's
  elevation profile, and RUBY, a lightweight turbocharged car.
- **v1.16.0** — boost and the fresh-rubber power-up are both retired.
  There is no aspiration system and no roadside item panel any more:
  every car is defined by its plain horsepower figure, and tires only
  get worse over a race, never refreshed mid-lap. `TURBO` and `BLOWER`,
  which existed solely to demonstrate the removed engine mechanic, are
  gone from the garage; `RUBY` stays as a plain naturally-aspirated car.
- **v1.17.0** — six new cars (BUGGY, WAGON, FORMULA, TRUCK, HERITAGE,
  MUSCLE), for eleven in the garage covering everything from a 620 kg
  dune buggy to a 2100 kg pickup. Berthoud Pass 2.0 rebuilt: the old
  layout let its own valley loop-back run close enough to the climb to
  clip it, and packed in hairpins sharp enough to bite; the new one
  keeps every piece of pavement a comfortable 90+ m from every other
  piece, and nothing on the whole lap is tighter than a 37 m radius.
- **v1.18.0** — every car now has a real drivetrain (front, rear, or
  full-time all-wheel with a tunable bias), grip is up across the
  board with a later, more progressive breakaway, and a compiled-roster
  bug that had been keeping every car added since v1.15.0 out of the
  garage whenever the game runs without an SD card is fixed.
- **v1.19.0** — weather on CLASSIC (snow melts into ice, then a
  puddle, with soft/medium/hard tires each suited to a different
  stage), a new boost meter that charges with revs and spends on a
  button, a no-guts-no-glory AI strategy for two drivers and a field-
  wide competitiveness bump, and every barriered track's shoulder
  brought in to the guardrail with the tightest corners on the roster
  opened out.
- **v1.19.1** — Berthoud Pass 2.0's real hairpin switchbacks are back.
  A v1.17.0 rebuild had smoothed them into a gentle sine-wiggle shape
  to satisfy a plan-view self-intersection check that, it turns out,
  was never reachable during actual driving — the real switchback
  stack, summit, and valley loop-back are restored and spread out
  further for room without losing the hairpins themselves.
- **v1.19.2** — fixed a real (if inert) drift bug between the compiled
  fallback roster and `cars.json`, backed by a new test that diffs
  every field of every car between the two instead of just the count.
  No feature work; also clarified that there is no
  turbocharger/supercharger system and that v1.19.0's boost is an
  unrelated, universal mechanic tuned in `settings.json`.
- **v1.20.0** — oversteer and understeer are both far more
  punishing/rewarding (progressive understeer scrub, plus a new
  catchable power-oversteer/spin mechanic for rear-driven cars under
  throttle); another AI competitiveness pass; standing water now drags
  at every car and the AI slows for weather the same way it slows for
  a corner it has learned to respect; and the data-only bones of a
  colour-based team mode and a career/campaign mode that carries a
  finishing position into the next race's starting grid — neither
  wired into an actual race yet.
- **v1.21.0** — boost reworked into an automatic turbo: it spools up
  and bleeds off on its own with real throttle and revs, applies
  straight to engine power (tapered off mid-slide, like a traction
  control), and the "use" button is now an instantaneous full-throttle
  stab instead of spending a meter. Two more genuinely hard-to-beat
  drivers, KESSLER and DUARTE. Some AI drivers now hunt the real
  racing line, reading past the corner they're in to set up for the
  next one, each by a different distance — and backing off that
  anticipation the tighter the corner they're actually in gets. A
  follow-up patch added a session-best lap per circuit, a HUD readout
  of the weather ahead, and a chase camera that leans into corners.
- **v1.22.0** — the difficulty, team and career data shapes from
  v1.19.0/v1.20.0 now actually do something when a race starts: a
  difficulty preset sets the lap count and leans AI skill, aggression
  and car choice; team mode paints every car by team and totals a
  combined score; career mode starts a human at the grid slot matching
  their last race's finish. There's still no menu control that sets
  any of it, so nothing changes in the game you can actually play yet.

> **Note on Nintendo content:** this is 100% original homebrew. It contains
> no Nintendo code or assets and does not require (or include) any game
> disc or ROM.

## Features

- **A grid of twelve, and nobody drives like anyone else.** Eleven AI
  rivals share out seven strategy sheets — BALANCED, LATE (brakes far too
  late), INSIDE (tight and defensive), DEFENDER (covers your favourite
  passing side), CHARGER (dives for every gap, brakes late), DRAFTER
  (sits in your mirrors, waits to pounce), CRUISER (cautious, wide,
  smooth). Each sheet sets its racing line, how it brakes, how it
  overtakes, **and how it shifts** — where in the rev band it changes up,
  how early it comes down the box under
  braking, and how crisply it does it. CRUISER short-shifts and rides the
  torque; LATE hangs on to the limiter and pays for it in shift time.
  On unguarded corners the attack-minded sheets also have a small,
  deterministic chance to overcommit to the outside line. Sometimes they
  save it. Sometimes gravity files the protest.
- **They learn from their mistakes.** Every driver keeps its own nerve
  rating for each corner on the track. Run wide, clip a barrier, or spin
  and that corner's rating drops, so it arrives slower next lap; take it
  cleanly and the rating creeps back up. Over a race the field visibly
  tidies up — in the current deterministic test run, mistakes across the
  grid fall from 71 in the first third of a race to 9 in the last. The
  late-brakers start
  over the limit and talk themselves down; the cruiser starts timid and
  finds pace.
- **They adapt to you.** The game keeps a model of each human: which
  side you complete your passes on, how your pace compares with the
  leading AI, and how often you lean on people. Defenders move over to
  cover the side you keep using, a quick human raises the whole field's
  ambition (and a slow one lets it relax), and drivers you keep banging
  into start leaving you more room.
- **Physically-based driving.** Acceleration comes from engine power
  (F = P/v, traction-capped), top speed emerges from aerodynamic drag,
  braking matches the car's quoted 100-0 km/h stopping distance, and
  cornering is limited by lateral grip (v²/r ≤ μg) — push past it and the
  car understeers wide, scrubbing speed. Gravity acts along the road
  grade: climbs cost speed, descents give it back.
- **Eleven cars, and no two drive alike.** RACER (a 260 kg superkart) and
  TRUCK (a 2100 kg pickup) sit at opposite ends of the roster; between
  them are a hot hatch (RUBY), a rally car (RALLY) and a dune buggy
  (BUGGY) that both actually use their dirt grip, a GT tourer (TOURER), a
  wagon (WAGON), an open-wheel FORMULA car with the most grip and the
  least dirt grip in the garage, a vintage roadster (HERITAGE) with drum
  brakes to match, and a MUSCLE car with more horsepower than chassis to
  match it. Every one is defined by horsepower, curb weight, stopping
  distance, lateral g, drag area, wheelbase, dirt grip and per-gear
  limiter speeds — the menu derives 0-100 time and top speed from the
  same equations the physics uses. The roster comes from `cars.json`, so
  another car is an added JSON object, not a C surgery.
- **Eight circuits**, all measured rather than guessed. Lap counts are set
  per circuit so every race covers a similar distance:

  | Circuit | Length | Width | Corners | Tightest | Climb | Steepest | Rails | Laps |
  |---------|--------|-------|---------|----------|-------|----------|-------|------|
  | CLASSIC | 555 m | 11.2 m | 10 | 24 m | flat | — | yes | 4 |
  | BERTHOUD (US-40) | 1421 m | 9.6 m | 19 | 11 m | 48 m | 15% | yes | 2 |
  | LOVELAND (US-6) | 1699 m | 12.0 m | 11 | 12 m | 61 m | 14% | **no** | 2 |
  | KENOSHA (US-285) | 2172 m | 13.2 m | 43 | 15 m | 58 m | 10% | yes | 2 |
  | MONARCH (US-50) | 2167 m | 8.6 m | 33 | 9 m | 118 m | 19% | **no** | 2 |
  | BREAKNECK | 945 m | 7.4 m | 13 | 29 m | 44 m | 29% | **no** | 3 |
  | GUANELLA | 1986 m | 7.8 m | 15 | 6 m | 64 m | 19% | **no** | 2 |
  | BERTHOUD 2.0 | 1411 m | 13.2 m | 20 | 37 m | 67 m | 21% | yes | 2 |

  **Kenosha** ties Berthoud 2.0 as the widest and is still the longest of
  the original five. **Monarch** climbs the most and has no rails.
  **Breakneck** is the steepest anywhere on the roster and the shortest
  pass; **Guanella** has the tightest hairpin (6 m). **Berthoud 2.0** is
  the opposite end of that: built from the real pass's own elevation
  profile, but with every bend eased out so nothing on it is sharper than
  a 37 m radius — no hairpins, no near-right-angle corners, just a fast,
  flowing mountain road with no piece of pavement anywhere close enough
  to another to clip it.
- **Gearboxes.** Every car has a real gearbox — 4 to 6 gears, each with a
  road speed at the limiter. Where you are in the band decides your power:
  bog it below a third of the band and it pulls badly, sit on the limiter
  and it stops pulling at all, and a shift cuts drive for a moment, so
  short-shifting out of a hairpin is a genuine decision. Pick **AUTO** or
  **SHIFT** (manual) per player in the garage.
- **Cliffs and checkpoints.** On the unguarded passes the shoulder is the
  edge: go over it and the car drops away, then gets set back down at the
  last checkpoint it passed without handing out any free progress. A
  recovery now holds that player's view on black for one second, fades the
  road back in, and flashes the car with five seconds of contact immunity.
- **Garage.** Each player gets their own: car, one of eight paint colours,
  **gearbox** (auto or manual) and **tire compound** (soft grips more and
  drags more, hard is slipperier and faster), all on a lit 3D turntable
  with the full spec sheet and a bar chart of the car's gearing.
- **Steering with weight.** Every input runs through a virtual analog
  stick: the wheel winds on over a few tenths of a second, self-centers
  faster than it winds on, calms down as speed rises, and passes through
  a progressive curve. Keyboards feel like a stick, not a switch.
- **Inputs without divination.** An optional live panel shows the keyboard
  key, recommended physical Xbox control, logical GameCube input Dolphin
  reports, and final game action on one line. It can be disabled in
  `controls.json` once the mapping behaves.
- **Editable tuning.** `settings.json`, `cars.json`, and `controls.json`
  are loaded independently from the app's `config` directory and safely
  fall back to compiled defaults if a file is missing or invalid.
- **Split-screen multiplayer** for up to 4 players (horizontal split for
  2, quadrants for 3-4), with view culling so a full field still runs at
  frame rate in four-way split.
- **No collectible power-ups, ever** — no roadside panel, no random
  pickup. A car's turbo is its own: it spools up and bleeds off
  automatically with real throttle and revs, no button required, and
  applies straight to engine power for as long as it stays lit. The
  "use" button is an instantaneous full-throttle stab instead of a
  resource to spend. Tires only ever wear over a race, with no mid-lap
  refresh; picking a compound is a bet on the whole distance, not a
  resource to manage lap to lap.
- **No arcade cheats.** There is no rubber-banding (a test proves an AI
  left behind gets exactly the same power: 17.05 m/s either way), no
  floor boost pads, and no mini-turbo reward for sliding — the handbrake
  rotates the car and scrubs speed, because that is what a handbrake
  does.
- **Procedural audio:** engine note that follows revs, tire squeal at the
  grip limit, countdown beeps — synthesized at runtime via ASND, no
  sound assets.
- Chase cameras with terrain avoidance, minimap, 7-segment HUD with
  km/h speedo, rumble on every supported controller.

## Running it in Dolphin

1. Install Dolphin: <https://dolphin-emu.org/download/>
2. Download `wiikart.zip` from the
   [latest release](https://github.com/Nessie404/starwarsgame/releases/latest)
   (or build it — see below), unzip.
3. **File → Open…** → `wiikart.dol`. Or: `tools/run-dolphin.sh`

### Pick your controls (all natively supported)

- **Xbox / any gamepad:** Dolphin → **Controllers → GameCube Controller
  Port 1 → Standard Controller → Configure**, select your pad (XInput
  for Xbox), then use the recommended map below. The in-race translator
  shows the physical Xbox label beside the GameCube input Dolphin is
  presenting to WiiKart, so a crossed wire is visible immediately.
- **Keyboard (WASD):** Dolphin → **Config → Wii → Connect USB Keyboard**,
  and that's it — no key mapping needed. **A** steers left, **D** right,
  **W** is the throttle, **S** the brake (arrow keys mirror all four).
- **Wii Remote (emulated or real):** sideways grip, tilt to steer —
  Nunchuk and Classic Controller also work.

### Editing the JSON in Dolphin

`config/cars.json`, `config/settings.json` and `config/controls.json` are
read at startup — but **Dolphin only gives the game a filesystem when an
SD card is inserted**. Opening `wiikart.dol` by itself means the JSON next
to it cannot be read at all, and the built-in defaults are used silently.

1. Dolphin → **Config → Wii → Insert SD Card**.
2. Put the release's `apps` folder onto that card, so the game finds
   `sd:/apps/wiikart/config/cars.json`.
3. Launch `boot.dol` from there.

Then open **JSON CONFIG** on the main menu: it shows where the files were
read from, whether each one loaded or why it did not, and the roster that
resulted (`4 CARS  RACER 48 HP`) — change a number in the file, restart,
and watch that line change. On a real Wii through the Homebrew Channel
there is nothing to set up; the files sit beside the app.

### Getting a sharper picture

The Wii's framebuffer is 640 pixels wide and no software can change that —
it is the video hardware. What v1.4.0 does is stop throwing half of it
away, and stop stretching what is left:

- **Progressive scan.** With a component cable and progressive scan enabled
  in the Wii's own settings, WiiKart now selects a 480p mode instead of
  480i, so every line is drawn every frame. No interlace shimmer on the
  guardrails, and no deflicker blur applied on purpose to hide it. In
  Dolphin, enable **Config → Wii → Aspect Ratio / Progressive Scan (480p)**
  to get the same mode.
- **Real 16:9.** If the console is set to widescreen, WiiKart renders a
  16:9 field of view rather than letting a 4:3 image be pulled sideways by
  the TV. Wheels stay round. There is no need for Dolphin's "widescreen
  hack" — set the emulated Wii to 16:9 and the game does it properly.
- **In Dolphin, go further.** **Graphics → General → Internal Resolution**
  renders this scene at 2x, 4x, or higher — 1080p, 1440p, 4K — and
  **Graphics → Enhancements** adds anti-aliasing and anisotropic
  filtering. That is where the big visual win is on a PC, and none of it
  costs anything on the Wii itself.

## Running on a real Wii

Copy the `apps` folder from `wiikart.zip` onto an SD card (so you have
`SD:/apps/wiikart/boot.dol`) and launch WiiKart from the Homebrew Channel.

## Controls and Dolphin translation

**Two players share one keyboard.** A second physical keyboard cannot be
told apart — libwiikeyboard merges every attached keyboard into one event
stream — so players 3 and 4 use pads (an Xbox pad appears as a GameCube
pad under Dolphin) or Wii Remotes.

| Action | Player 1 keyboard | Player 2 keyboard | Dolphin presents | Recommended Xbox control | Wii Remote (sideways) |
|--------|-------------------|-------------------|------------------|--------------------------|------------------------|
| Steer left / right | **A** / **D** | **J** / **L** | GC stick or D-pad | Left stick | Tilt or D-pad |
| Accelerate | **W** | **I** | GC A or X | Right trigger | 2 or A |
| Brake / reverse | **S** | **K** | GC B | Left trigger | 1 |
| Up a gear | **E** | **O** | GC R | Right bumper | D-pad ↑ / Classic ZR |
| Down a gear | **Q** | **U** | GC L | Left bumper | D-pad ↓ / Classic ZL |
| Handbrake | Space | **P** | GC Z | A | Hold B |
| Back to menu | **R** | **R** | GC Start | Start | + |
| Quit | (EXIT row) | — | GC Z + Start | A + Start | HOME |

Back to menu now pauses the race and opens a **LEAVE RACE / ARE YOU
SURE** guard. Use the mapped menu-confirm action to leave, or press Back
or the race-menu button again to keep racing. Any active racer can answer
the prompt; the HOME/emergency full-app quit remains immediate.

Arrow keys mirror player 1's WASD. Most of these are just defaults —
edit `config/controls.json` to move them. The Xbox column is a recommended
host-side Dolphin map, not something Wii code can detect directly: WiiKart
receives the logical GameCube column. Keep the `xbox_recommended` labels
in the JSON aligned with your Dolphin profile and the live panel becomes
an exact translation chart.

Mario Kart Wii itself has no manual transmission: its karts shift
automatically. WiiKart therefore defaults each garage gearbox to AUTO. If
you select SHIFT with a plain Wii Remote, tilt steers, D-pad Up/Down shifts,
and D-pad Left/Right remains available for digital steering. A Classic
Controller uses ZR/ZL instead.

**Menus never advance on a stray press.** Every screen is a list of rows
with a cursor: **W/S** (or ↑/↓, or the D-pad) moves the cursor,
**A/D** (or ←/→) changes the value of the row you are on, **Enter**
(or GC A/Z, or Wii A/2) acts on it, and **Esc** (or GC B, or Wii B/1)
backs out. With the recommended Xbox map, A or RT confirms and LT backs
out. Only the action rows
— GO, a player's GARAGE, DONE, EXIT — actually do anything when
activated, so you can look around without being thrown forward. Esc no
longer quits the game; EXIT does.

**You cannot start a race without enough controllers.** The setup screen
shows how many pads it can see, and GO refuses with `NEED n PADS FOUND m`
rather than starting a race with a player who cannot steer. If a
controller disappears mid-race, the race is abandoned after a moment's
grace and you land back in the menu with `CONTROLLER LOST` — no freeze,
no dead player.

Driving notes: brake before hairpins — the grip circle is real. The
handbrake rotates the car but costs you speed, so use it to place the car,
not to go faster. Watch the rev bar next to the gear number: shift at the
top of the band, and come down a gear before a hairpin so you are not
bogged on the exit. RALLY keeps 72% of its grip on dirt, TOURER only 35%.
Pick a tire compound for the whole race rather than the corner you happen
to be on — softs are quickest early and give it back late. On Loveland
and Monarch there is nothing holding you on the road.

The HUD names the car you are chasing by its strategy, and the finish
screen prints the full classification, so you can see whether DEFENDER or
CHARGER actually got the job done. A quick AI laps Berthoud in about 62 s
and Loveland in about 68 s, against 26 s round Classic. The extended
Monarch takes roughly 105 s in the deterministic host simulation.

## Editable configuration

The three files in [`config/`](config/) are the supported tuning surface:

- `cars.json` contains the complete garage roster in real-world units;
  duplicate an object to add a car (up to 16 cars and 6 gears each).
- `settings.json` covers race length, driving physics, steering, tires,
  AI risk, recovery timing, and per-track scale, elevation, width, and
  lap count.
- `controls.json` covers keyboard bindings, logical GameCube bindings,
  recommended Xbox labels, and the live translator toggle.

Each file is validated and loaded independently. A missing or invalid file
falls back to its compiled defaults instead of partially applying. See
[`config/README.md`](config/README.md) for accepted names, ranges, and the
Dolphin virtual-SD layout.

## The cars

| Car    | Power | Curb  | 100-0 | Lateral | Dirt grip | Gears | 1st tops at |
|--------|-------|-------|-------|---------|-----------|-------|-------------|
| RACER  | 48 hp | 260 kg | 30 m | 1.30 g  | 30% | 4 | 50 km/h |
| SPORT  | 150 hp | 950 kg | 37 m | 0.95 g  | 45% | 5 | 47 km/h |
| RALLY  | 220 hp | 1180 kg | 40 m | 0.88 g  | 72% | 6 | 43 km/h |
| TOURER | 310 hp | 1350 kg | 34 m | 1.02 g  | 35% | 6 | 54 km/h |

0-100 times and top speeds shown in the menu are derived from these
numbers by the same equations the physics uses.

## Building

You need devkitPro's **devkitPPC** toolchain (`wii-dev` package group).

```sh
export DEVKITPPC=/opt/devkitpro/devkitPPC
make            # wiikart.dol + wiikart.elf
make dist       # dist/apps/wiikart for SD cards

# or via the official container
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkitppc:latest make
```

Every push builds on GitHub Actions and publishes a release with
`wiikart.zip`; tags (e.g. `v1.0.0`) publish named version releases.

## Project layout

```
source/game.h     shared types, kart spec sheets, simulation API
source/track.c    3D circuit geometry (Catmull-Rom, elevation, curvature)
source/game.c     vehicle dynamics, AI strategies/learning/adaptation, items
source/config.c   dependency-free JSON config loader and validation
source/main.c     Wii layer: GX renderer, menus, split screen, audio, input
config/           editable cars, game settings, controls, and instructions
tests/            host-side physics/AI tests (plain gcc, no Wii SDK)
hbc/              Homebrew Channel metadata
tools/            run-dolphin.sh launcher
```

The simulation is platform-independent C99, tested on the host:

```sh
gcc -std=c99 -O2 -Wall -Werror -Isource \
    tests/test_game.c source/game.c source/track.c source/config.c \
    -lm -o wiikart-test
./wiikart-test
```

Tests verify JSON parsing and transactional fallback, the physics against
the spec sheets (measured stopping
distance vs quoted, grip-capped yaw rate, gravity on grades), the
steering filter (ramp shape, self-centering, speed sensitivity, and that
left really is left), track geometry and corner segmentation for all
circuits, that a twelve-car grid fits on the road, and that the AI
completes laps on every track — including recovering from a botched
hairpin by backing out, since a real car can't rotate in place.

Steering polarity is tested the way you experience it: the test
re-derives the chase camera's right-hand axis (`cross(forward, up)`, the
same vector `guLookAt` uses) and asserts that a left input moves the car
toward the left of the screen. The earlier test only checked that the
heading angle changed, which is exactly how the controls shipped
mirrored in v1.0.

The v1.1 AI behaviour is tested as behaviour, not just as code: that the
field spreads out across distinct racing lines and error counts, that
mistakes fall as a race progresses, that overconfident strategies end up
believing less than they started and timid ones more, that a defender
covers the side a human has been passing on (and the mirror-image setup
produces the mirror-image line), and that an overtake is logged with the
side it happened on. A dedicated test proves the AI get no rubber-band
power boost.

v1.2.0 adds tests for the gear model (the power curve's shape, that a
held key shifts exactly one gear, that a gear's limiter really caps
speed, and that neither the automatic box nor any AI strategy *hunts* —
this caught a real bug where short-shifting drivers changed gear 360
times in 90 seconds because an upshift landed on their own downshift
point), for the circuit roster (which tracks are unguarded, that Kenosha
is the longest/widest/turniest and Monarch the narrowest/tightest/
steepest), for lap counts scaling with circuit length, and for cliff
recovery: that a car off an unguarded edge falls, comes back at the last
checkpoint it passed, keeps its lap, gains no free progress, and is
drivable afterwards — while a barriered track still holds cars in.

v1.2.1 came out of a stability audit and every fix in it is pinned by a
test that fails without the fix. The worst find was scoring: rebuilding a
respawning car's progress as `lap * segments + checkpoint` looks right,
but the lap counter ticks over while the car is in the air, so going over
the edge in the last two segments before the line returned it to a
checkpoint a lap behind and credited it with **96% of a lap** — enough to
win a two-lap race from the middle of lap one. The respawn now moves the
car by the signed arc it actually gave up, and the test sweeps twelve
segments either side of the line on both unguarded circuits. The others:
a finished human kart was falling through to the AI driver and running on
AI fields it never had (zero skill, zero corner confidence read as "brake
for everything") so it stopped and reversed back down the circuit at
21 km/h — it now coasts; the cliff drop was written to `k->y` and
overwritten with the road height on the same frame, so the car slid along
at road level and teleported (it now visibly falls ~11 m); a negative
checkpoint index could read off the front of `checkpoint_seg[]`, since
`%` keeps the sign of its left operand; and NaN now dies at the door —
`game_clampf` returns the low bound for it, `game_angle_wrap` bounds its
input (at 1e9 a float's step exceeds 2*pi, so the loop could never end),
and a non-finite frame time is refused.

v1.3.0 adds coverage for the shipped JSON files, custom car insertion,
invalid-roster rollback, partial settings overrides, and control
translation. Recovery tests now verify the black hold, fade, five visible
seconds of invincibility, collision immunity, and continued drivability.
The deterministic Monarch stress run also proves that aggressive AI can
overcommit and fall while the full field can still finish every circuit.

v1.4.0 puts the chase camera in its own module so its behaviour is
testable rather than something you have to see to believe: that reversing
swings the view round to the nose and driving forward brings it back, that
rocking around a standstill neither snaps it nor sets it hunting (measured
at 0.005 rad per frame against a 2.5 rad/s limit), that it tilts up for a
17% climb and down for a 19% descent, that look-ahead grows with speed and
stops at its cap, that a checkpoint respawn re-places it instead of letting
it streak across the mountain, and that over 90 seconds of driving on every
circuit it never drops below its ground clearance, never aims more than
15 degrees off level, and never produces a NaN.

## Ideas for later

- Open-road mode down the passes with traffic to overtake
- Ghost laps and lap-time records
- Weather (rain lowers μ; snow on the passes)
- More passes (Independence, Pikes Peak hill climb)
- Online time-trial leaderboards
