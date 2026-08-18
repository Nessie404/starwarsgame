# WiiKart configuration

These files are read independently at startup. If one is missing or
invalid, WiiKart keeps the compiled defaults for that file and shows a
short status on the setup screen. A broken car entry therefore cannot
leave the garage half-loaded.

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

## `settings.json`

This file exposes the main tuning surfaces:

- race countdown, target distance, and lap limits;
- drivetrain, rolling resistance, shift time, and engine bog point;
- steering response and speed sensitivity;
- tire and power-up multipliers;
- AI pace, braking, safe-road use, and overcommit behavior;
- cliff fall, blackout, fade, invincibility, and flash timing;
- per-track width, plan scale, elevation, and optional fixed lap count.

For a track, `laps: 0` means automatic. A positive lap count is an explicit
override (up to 20), independent of the automatic minimum and maximum.
Width/scale/elevation values are multipliers, so `1.10` means ten percent
more than the built-in geometry.

## `controls.json`

There are three columns to keep straight, because naturally an emulator
can turn one button into a small genealogy:

1. `keyboard` is what WiiKart receives directly from a USB keyboard.
2. `gamecube` is the logical controller input Dolphin presents to WiiKart.
3. `xbox_recommended` is the physical Xbox control you should map to that
   GameCube input in Dolphin. It is a display label, because a Wii program
   cannot discover which host-side Xbox button Dolphin used.

The in-race input panel shows all three beside the resolved game action and
lights the action when it is active. If you prefer a different physical
layout, change the Dolphin mapping and then change the matching
`xbox_recommended` label so the panel remains truthful. Set
`show_input_overlay` to `false` once everything agrees.

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
