# 666 — WS-DOOM

Doom as a program card. The whole game — [kilograham's rp2040-doom](https://github.com/kilograham/rp2040-doom)
with the shareware DOOM1 WAD — runs standalone on the Computer at 270 MHz.
No screen: you play it from the panel and *listen* your way through. The
stereo mix pans enemies left/right as you turn, and the music itself pans
and swells toward the nearest enemy — a beacon you can hunt by ear. The
LEDs are an enemy radar.

Built by [incomputable.io](https://incomputable.io).

## Flashing

The easiest way is the *Program* button on the
[card's page](https://computer.musicthing.co.uk/programs/666-ws-doom/):
connect USB-C → remove the program card → press the boot/reset button on
the Computer Module → insert a blank program card (2 MB is ok) → press
*Program*. It flashes straight from the browser (Chrome).

The same page has the `.uf2` download (engine + WAD in one file) for
flashing by drag-and-drop onto the `RPI-RP2` drive or with picotool.
Heads-up if you drag-and-drop on macOS: Finder sometimes truncates large
UF2 copies (LEDs but no game) — the browser flasher avoids that entirely.

## Panel

| Control | Function |
| --- | --- |
| Main knob | Turn rate (centre = straight) |
| X knob | Forward / back (centre = stop) |
| Y knob | Weapon 1–7; fires once to confirm each change |
| Z switch down (momentary) | Fire — also respawns after death (auto after 10 s) |
| Z switch up (latched) | Auto-aim the nearest enemy, doors open hands-free |
| Pulse in 1 / 2 | Fire / Use |
| CV in 1 | Turn (sums with Main knob) |
| CV in 2 | Forward / back|
| Audio in 1 | Replaces the soundtrack (same enemy panning) |
| Audio in 2 | Gates open on every shot you fire |

| Output | Signal |
| --- | --- |
| Audio out L/R | Game audio — effects enemy-panned, music beacons toward the nearest enemy |
| CV out 1 / 2 | Health / ammo (0–5 V) |
| Pulse out 1 / 2 | Shot fired / damage taken |
| LEDs | Enemy radar: row = distance, side = bearing, dim = behind you |

The game starts at a random E1 level a few seconds after power-up, and a
spawn director keeps one enemy within earshot.

## Web display (optional)

**https://ws-doom.incomputable.io** — or open `index.html` from this folder
in Chrome/Edge. Connect the module over USB-C and press *Connect*: the
module streams the actual 320×200 game video onto a CRT-styled screen.
Extras: level warp, music/effects volume and turn/move sensitivity sliders
(savable to the card), pause, cheat codes, firmware download. The game
runs entirely on the module and never depends on the page.

## Source

The complete firmware source (the Workshop Computer backend plus the
vendored [rp2040-doom](https://github.com/kilograham/rp2040-doom) tree it
builds on) is in the `firmware/` subfolder; build instructions are in
`firmware/README.md`.

## License

Everything — firmware, `ws-doom-full.uf2` and the web display — is
**GPL-2.0** (see `firmware/doom/COPYING.md`). The bundled WAD is the DOOM
shareware episode, distributed under its original shareware terms.
