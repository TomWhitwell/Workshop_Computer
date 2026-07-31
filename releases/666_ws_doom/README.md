# 666 — WS-DOOM

Doom as a program card. The whole game — [kilograham's rp2040-doom](https://github.com/kilograham/rp2040-doom)
with the shareware DOOM1 WAD — runs standalone on the Computer at 270 MHz.
No screen: you play it from the panel and *listen* your way through. The
stereo mix pans enemies left/right as you turn, and the music itself pans
and swells toward the nearest enemy — a beacon you can hunt by ear. The
LEDs are an enemy radar.

Built by [incomputable.io](https://incomputable.io).

## Flashing

Flash `ws-doom-full.uf2` (engine + WAD in one file) to a blank
**2 MB** program card: hold the card's boot button while powering up, then
copy the file onto the `RPI-RP2` drive.

> **macOS note:** Finder sometimes truncates large UF2 copies (the card
> then boots to LEDs but no game). Copy from Terminal instead:
> `cp ws-doom-full.uf2 /Volumes/RPI-RP2/` — or use
> `picotool load -f -v ws-doom-full.uf2`.

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
