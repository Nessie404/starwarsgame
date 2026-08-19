# WiiKart configuration

These files are read independently at startup. If one is missing or
invalid, WiiKart keeps the compiled defaults for that file and shows a
short status on the setup screen. A broken car entry therefore cannot
leave the garage half-loaded.

## Is it working?

Open **JSON CONFIG** on the main menu. It shows where the game read from,
whether each of the three files loaded, and the car roster it ended up
with — for example `4 CARS  RACER 48 HP`. Edit `power_hp` for that car,
restart, and if the screen shows the new number, your file is being used.
If it says `BUILT IN CONFIG` or `NO SD CARD FOUND`, the game never saw a
file, and the lines on that screen say where it looked.

**In Dolphin this needs an emulated SD card.** Opening `wiikart.dol` on
its own gives the game no filesystem at all, so the JSON beside it cannot
be read and the compiled-in defaults are used. Turn on
**Config → Wii → Insert SD Card** (Dolphin's SD image lives in its `Load`
folder; recent versions can also sync a folder into it), put the `apps`
folder from the release on that card, and launch
`sd:/apps/wiikart/boot.dol`. On a real Wii through the Homebrew Channel it
works with no setup — the files sit next to the app on your own SD card.

## Where the files go

On a real Wii or Dolphin virtual SD card, keep this directory beside the
Homebrew Channel executable:

```text
apps/
  wiikart/
    boot.dol
    meta.xml
    config/
      cars.json
      settings.json
      controls.json
```

Dolphin must have its Wii SD card enabled. Current Dolphin builds support
a folder-synced virtual SD card; its official setup guide is
<https://dolphin-emu.org/docs/guides/virtual-sd-card-guide/>. Opening a
standalone `wiikart.dol` without an accessible Wii SD/USB filesystem is
still supported, but it necessarily uses the compiled defaults—Wii code
cannot read an arbitrary host folder merely because the DOL came from it.

## `cars.json`

The `cars` array is the garage roster. To add a car, duplicate an object,
give it a unique 1–15 character name (letters, numbers, spaces, `-`, or
`.`), and edit its real-unit values. The game supports up to 16 cars and
6 forward gears. Gear values are road speeds at the rev limiter, in km/h,
and must increase from one gear to the next.

The whole file is validated before it replaces the built-in roster. Useful
accepted ranges are deliberately broad: 100–5000 kg, 5–2500 hp, 0.2–3.0 g
lateral grip, 0.5–6.0 m wheelbase, and 7.2–504 km/h for each increasing
gear-limiter speed.

Optional per-car keys, on top of the required ones:

| Key | Default | What it does |
|---|---|---|
| `automatic_upshift_fraction` | 0.95 | Where the automatic box changes up, as a fraction of the gear's own limiter speed |
| `automatic_downshift_fraction` | 0.38 | Where it changes down |
| `automatic_upshift_per_gear` | — | The same, one value per gear (the list length must match the gearbox) |
| `automatic_downshift_per_gear` | — | As above |

A car can short-shift out of first and hold second to the limiter by
giving a per-gear list. The upshift and downshift points must stay at
least 0.20 apart, otherwise the box would change up into its own
downshift point and hunt; a file that asks for that is refused whole.

## `settings.json`

This file exposes the main tuning surfaces:

- race countdown, target distance, and lap limits;
- drivetrain, rolling resistance, shift time, and engine bog point;
- steering response and speed sensitivity;
- tire compounds: grip, drag, rolling resistance, wear rate and how much
  grip that wear costs, the temperature each wants and how wide its window
  is, how fast it heats and cools, and how much grip is left outside the
  window — plus the ambient air temperature they cool towards;
- power-up multipliers;
- AI pace, braking, safe-road use, and overcommit behavior;
- cliff fall, blackout, fade, invincibility, and flash timing;
- how long a human can drive the wrong way before the marshal helicopter
  arrives, and how much engine it leaves them (see below);
- how much hills matter, the chase camera, and the tachometer's rev
  range (see below);
- per-track width, plan scale, elevation, and optional fixed lap count
  (`classic`, `berthoud`, `loveland`, `kenosha`, `monarch`, `breakneck`,
  `guanella`).

For a track, `laps: 0` means automatic. A positive lap count is an explicit
override (up to 20), independent of the automatic minimum and maximum.
Width/scale/elevation values are multipliers, so `1.10` means ten percent
more than the built-in geometry. A width multiplier scales the whole
circuit; the *shape* of the width — which corners are pinched and which
are opened out — is part of the track itself.

### The `hills` block

Gravity along a road at angle theta is `g*sin(theta)`, and the load
pressing the tires down is `m*g*cos(theta)` — so a climb costs speed and
also, slightly, grip. `gravity_multiplier` (default 1.0) scales the first:
raise it to make every pass feel more mountainous than it is.
`load_effect` (0 to 1, default 1.0) scales the second: drop it to zero if
you would rather steep ground did not cost grip at all.

### The `instruments` block

The simulation has no crankshaft — engine output comes from where the car
is in its current gear — so the tachometer maps that position onto a rev
range you can read. `tacho_idle_rpm` (default 1200) is the bottom of the
gear, `tacho_redline_rpm` (default 7800) is the limiter. The redline must
be above idle.

### The `wrong_way` block

Measured against net progress along the track's centerline, not heading —
so a car that spins around but is not actually travelling backward is
left alone. `seconds` (default 2.5) is how long that has to hold before
the marshal helicopter drops in; `power` (default 0.35) is the fraction
of engine left while it is overhead. Recovers at the same rate it built
up: turn around and drive forward for as long as you were going backward
and the helicopter leaves. Only ever applies to the human players — the
AI's own reverse-out recovery is never touched by it.

### The `camera` block

Distances are meters and rates are per second. Every `*_smoothing` value is
the fraction of the remaining error still left one second later, which is
frame-rate independent: smaller is quicker, and `0` snaps.

| Key | Default | What it does |
|---|---|---|
| `distance_m` | 9.0 | How far behind the car the camera sits |
| `height_m` | 3.6 | How high above it |
| `min_height_above_road_m` | 1.6 | Clearance it will not go below, whatever the ground does |
| `look_ahead_m` | 6.0 | How far up the road it aims when stopped |
| `look_ahead_per_mps` | 0.30 | Extra meters of aim per m/s of speed |
| `look_ahead_max_m` | 22.0 | Cap on the above |
| `look_height_m` | 1.2 | Height of the aim point above the car |
| `look_min_height_m` | 0.8 | Keeps the aim point off the pavement |
| `follow_smoothing` | 0.006 | How loosely the camera body follows |
| `look_smoothing` | 0.0015 | How loosely the aim point follows |
| `reverse_deadzone_mps` | 1.2 | Reverse speed below which the view is left alone |
| `reverse_full_mps` | 6.0 | Reverse speed at which it has fully swung to the nose |
| `reverse_orbit_rate_deg_per_s` | 150 | Hard limit on how fast it may swing |
| `reverse_smoothing` | 0.02 | How eagerly it chases the swing |
| `pitch_influence` | 0.65 | How much of the road's pitch it copies (0 = stays level) |
| `pitch_smoothing` | 0.05 | How much a bump is allowed to nod the camera |
| `pitch_min_deg` | -20 | Furthest it may tilt up |
| `pitch_max_deg` | 20 | Furthest it may tilt down |

Reversing swings the camera round to the nose so the view faces the way the
car is actually going, and driving forward brings it back. The dead zone and
the rate limit are what stop a car rocking around a standstill from spinning
the view, so raise `reverse_deadzone_mps` if you want to reverse further
before the view moves at all, and lower `reverse_orbit_rate_deg_per_s` if
the swing feels abrupt.

`reverse_full_mps` must be above `reverse_deadzone_mps`, and
`look_ahead_max_m` at or above `look_ahead_m`; the file is rejected as a
whole if either is not true, and the built-in camera stays active.

## `controls.json`

There are three columns to keep straight, because naturally an emulator
can turn one button into a small genealogy:

1. `keyboard` is what WiiKart receives directly from a USB keyboard.
2. `gamecube` is the logical controller input Dolphin presents to WiiKart.
3. `xbox_recommended` is the physical Xbox control you should map to that
   GameCube input in Dolphin. It is a display label, because a Wii program
   cannot discover which host-side Xbox button Dolphin used.

The in-race input panel shows all three beside the resolved game action and
lights the action when it is active. It is off by default — the small
always-on leaderboard is what a race screen shows day to day — so turn
`show_input_overlay` on only while checking a new mapping: if you prefer a
different physical layout, change the Dolphin mapping and then change the
matching `xbox_recommended` label so the panel remains truthful, then set
`show_input_overlay` back to `false` once everything agrees.

During a race, `race_menu` pauses and opens a leave-race confirmation
instead of abandoning the event immediately. `menu_confirm` accepts it;
`menu_back` or `race_menu` a second time cancels it. The GameCube mappings
and Xbox display labels for those actions are editable here just like the
driving controls.

Wii Remote inputs are native rather than a Dolphin host translation. AUTO
gearbox mode keeps the existing tilt/D-pad steering. In SHIFT mode, a
sideways Remote uses D-pad Up/Down for gears and Left/Right for optional
digital steering; a Classic Controller uses ZR/ZL.

Keyboard actions accept a single name or up to three alternatives. Valid
names are `A`–`Z`, `SPACE`, `ENTER`, `ESC`, `LEFT`, `RIGHT`, `UP`, and
`DOWN`. GameCube names are `A`, `B`, `X`, `Y`, `Z`, `L`, `R`, `START`,
`DPAD_LEFT`, `DPAD_RIGHT`, `DPAD_UP`, `DPAD_DOWN`, `STICK_LEFT`, and
`STICK_RIGHT`.
