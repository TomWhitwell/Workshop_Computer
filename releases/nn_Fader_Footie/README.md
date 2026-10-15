# Fader Footie

Table football (*Tischfußball*) for two players and a
[Music Thing 8mu](https://www.musicthing.co.uk/8mu_page/), played in a web
page, with the Workshop Computer turning the game into voltages: who touched
the ball, where the ball is, every touch and every goal.

The 8mu sits on the table between the two players, its buttons towards the
screen. Each player has the four faders nearest them, one for each of their
rods, and the two buttons nearest them, a hard and a soft spin shot.

**Status: 0.1.0, beta.** The firmware builds and the web page has been tested
in Chrome against a simulated card and 8mu, with the CPU playing itself. It
has not yet been tried on a Workshop Computer with a real 8mu.

## What you need

- A Workshop Computer, plugged into a computer's USB port with a data cable.
- Chrome or Edge on that computer, to open `web/index.html`.
- An 8mu on its factory settings (faders CC 34-41, buttons notes C2, C3, C4,
  C5), plugged into the same computer. Without one, the page's on-screen
  faders and the keyboard work too, and either team can be played by the CPU.

## Quick start

1. Copy `UF2/fader_footie.uf2` to the card (hold BOOTSEL, plug in, drop the file).
2. Plug the computer into the Computer's front USB socket and **power-cycle
   the module**. The card appears as a MIDI device called **Fader Footie**.
3. Plug the 8mu into the computer.
4. Open `web/index.html` in Chrome, press **Connect card & 8mu**, and allow
   MIDI with SysEx.
5. Team 2 starts as the CPU: set it to **Human** (next to its score) for two
   players.

The page plays without a card (no voltages, of course) and without an 8mu.

## Playing

| 8mu | Team 1 (left, red) | Team 2 (right, blue) |
|---|---|---|
| Fader 1 / 8 | Goalie | Goalie |
| Fader 2 / 7 | Defence | Defence |
| Fader 3 / 6 | Midfield | Midfield |
| Fader 4 / 5 | Attack | Attack |
| Button A / D | Hard spin shot | Hard spin shot |
| Button B / C | Soft spin shot | Soft spin shot |

So the fader nearest each player is their goalie, and the two attacks meet in
the middle of the 8mu. **Attack nearest the player** turns that round, and
**Swap hard and soft** swaps the buttons. A fader pushed up moves its rod up the
screen; tick **faders upside down** for a team if it feels backwards from
where that player sits.

The teams are the classic 1-2-5-3: one goalie, two defenders, five midfielders
and three attackers, numbered as in football, 1 to 11, top to bottom along each
rod. The single goalie can cover the whole width, which leaves the goal open
when he goes chasing into a corner.

**Spin shots.** A shot spins a rod right round, feet first. The feet strike a
ball in front of the man, and come round from behind fast enough to drag a ball
lying just behind him through to the front. While the rod is over the top the
men's feet are lifted (drawn faded), so a ball can roll underneath. A shot
spins the rod nearest the ball, a ball in front counting nearer; **Shot spins:
all four rods** spins every rod at once, goalie included, which is more
dramatic and leaves the goal open.

**Serving.** The ball is served from the side of the table at the halfway line,
rolling across it. A ball that stops, or rolls
around where nobody can reach it, is nudged or served again after a few
seconds (**Unstick after**). A match is first to 5 (or 3, 10, or endless);
the next starts by itself.

**8mu tilt.** Turn up **8mu tilt → slope** and tipping the 8mu tips the table.

**Keyboard.** Team 1 rods `Q`/`A` `W`/`S` `E`/`D` `R`/`F`, shots `Z` hard and
`X` soft; team 2 rods `U`/`J` `I`/`K` `O`/`L` `P`/`;`, shots `,` soft and `.`
hard. `Space` serves, `Esc` pauses. The on-screen faders and buttons can be
dragged and pressed too.

**CPU.** Either team can be played by the CPU: easy, medium or hard. Two CPUs
will happily play each other all day, which makes a good generative
modulation source with nobody at the table.

## Physics

Everything is tweakable while you play, and kept in the browser. Presets:
Classic, Ice rink, Pinball, Mud, Beach ball, Marble, Giants, Barn door and Slow
motion.

| Setting | What it does |
|---|---|
| Game speed | Slows down or speeds up time, 0.25x to 2x |
| Friction | How quickly the ball slows, in proportion to its speed |
| Rolling resistance | A constant drag that brings a slow ball to rest |
| Wall bounciness | 0 dead, 1 perfectly springy, above 1 the walls kick back |
| Player bounciness | The same, off the men |
| Player grip | How much the men grab the ball sideways: high to trap it, low to let it skid off |
| Hard / soft shot power | How fast the feet move in a hard and a soft spin |
| Max ball speed | A speed limit |
| Ball size | Radius of the ball |
| Player size | Size of the men |
| Goal width | Width of both goal mouths |
| Kick reach | How far the feet swing; also sets how long a spin takes |
| Rod speed | How fast a rod can follow its fader |
| Corner ramps | The diagonal corners, which keep the ball from sitting in a corner |
| Serve speed | How hard the ball comes in at a serve (and a nudge) |
| 8mu tilt → slope | How much tipping the 8mu tips the table (off by default) |
| Unstick after | Seconds a ball may sit still before it is nudged (off: never) |

## Outputs

| Jack | What it does |
|---|---|
| **CV Out 1** | The number (1-11) of the last team 1 player to touch the ball, as a voltage |
| **CV Out 2** | The same for team 2 |
| **Audio Out 1** | How far the ball is from the goal team 1 attacks (the right-hand one): about 0V in the goal mouth to about +5V at the far corners |
| **Audio Out 2** | How far the ball is from the goal team 2 attacks (the left-hand one) |
| **Pulse Out 1** | A gate each time team 1 touches the ball; a burst of fast triggers when team 1 scores |
| **Pulse Out 2** | The same for team 2 |

**Players to CV.** The page chooses how a player's number becomes a voltage,
and lists all eleven for both teams:

- *Notes of a scale* (the default, major): player 1 is the root, 2 the next
  note of the scale, and so on up, 1V/oct. Ten scales.
- *Semitones*: one semitone per player, 1V/oct.
- *Spread 0-5V*: player 1 at 0V to player 11 at 5V, for modulation.
- *Rods, 1V apart*: goalie 0V, defence 1V, midfield 2V, attack 3V.

**Player 1 at** shifts everything up or down, a semitone at a time, and **Team 2
transpose** moves team 2 against team 1, so the two teams can play in
harmony. The CV outs are calibrated, so they play in tune. Patch them into
two oscillators' pitch inputs, the pulse outs into their envelopes, and the
game plays a duet.

**Distance.** Straight-line distance from the goal by default; **along the
table only** ignores the side-to-side position. The page sends it about 60
times a second, and the card smooths it (Main knob). The two audio outs
cross over as play moves up and down the table, which is a lovely pair of
crossfade or panning voltages. With the switch up they show nearness instead
(+5V at the goal), so a voltage rises as an attack builds.

**Pulses.** A touch is a new contact between the ball and a man; a ball held
against a man is one touch. The gate length is set by the X knob. A goal
fires a burst of triggers (12 by default, set on the page from 1 to 64) at
the rate set by the Y knob, and the burst wins over any touch gates. A gate
that is already high goes low for 1ms first, so every touch is a fresh
edge.

## The card's panel

| Control | What it does |
|---|---|
| **Main** | Smoothing of the audio outs, from about 1ms to about 1s |
| **X** | Touch gate length, 2ms to 1s |
| **Y** | Goal burst rate, 4Hz to 60Hz |
| **Switch up** | Audio outs show nearness instead of distance |
| **Switch middle** | Audio outs show distance |
| **Switch down** (momentary) | Tap to serve the ball again; hold for a second to start a new match |
| **Pulse In 1** | Serve the ball again |
| **Pulse In 2** | Start a new match |

The page shows the switch and the three knob settings under Outputs.

| LED | Meaning |
|---|---|
| 0 / 1 | Audio Out 1 / 2 |
| 2 / 3 | CV Out 1 / 2 (0-5V) |
| 4 / 5 | Pulse Out 1 / 2 |

With no page linked (it pings the card every second), LEDs 4 and 5 blink
slowly in turn. The outputs hold their last values.

## How it works

The physics runs in the page: a fixed 720Hz step, the ball against the
walls, the posts and the men's feet. Each man is a rounded box that moves
with its rod; a spinning man's feet swing forwards and back as the rod turns
(so their speed is what kicks the ball) and only collide while they are
down. The card is a USB MIDI device and does the timing that matters on the
jacks: the page sends it the voltages and the touch and goal events as
SysEx, and the card makes the gates, bursts and smoothing at the audio rate.
The protocol is in [`sysex.h`](sysex.h).

The card is always a USB device, never a host: the game needs the screen,
so the 8mu goes into the computer.

## Building

```
cmake -B build -G Ninja
cmake --build build
```

Needs the Pico SDK. `ComputerCard.h` is copied from
`Demonstrations+HelloWorlds/PicoSDK/ComputerCard`. The binary is committed at
`UF2/fader_footie.uf2`.

## Credits

- **Chris Johnson**: the ComputerCard library.
- **Tom Whitwell / Music Thing Modular**: the Workshop System and the 8mu.

Written with [Claude](https://claude.com/claude-code) (Anthropic) in
collaboration with the card's author.

## License

MIT.
