# ii (I2C)

Blackbird speaks ii, the I2C protocol that crow, Teletype and Whimsical Raps / monome / Mannequins modules use to talk to each other. It works as crow's does: a script can **lead** (send commands and queries to other modules) and blackbird always **follows** (answers commands and queries from a leader such as Teletype or another crow).

## Wiring

The Computer has no ii jack. It uses the two **UART pads** on its circuit board, which are usually unpopulated, so you'll need to solder wires or a header to them.

| Computer | ii bus |
|---|---|
| TX (GPIO 0) | SDA |
| RX (GPIO 1) | SCL |
| ground | ground |

If the Computer and the other modules share a powered case, ground is already shared.

**Pull-ups.** An I2C bus needs pull-up resistors, and the Computer has none strong enough on its own. crow and Teletype provide them (crow by default), so with either on the bus you need nothing else. Otherwise fit about 2.2-4.7k from SDA and from SCL to **3.3V**. Never pull up to 5V: the RP2040 isn't 5V tolerant. `ii.pullup(true)` only switches on the RP2040's weak internal pull-ups, which are far too weak to run a bus by themselves.

### Running other cards while wired up

The UART pads belong to the Computer, not the card, so the wiring stays connected whatever card is running, and other cards don't know an ii bus is there:

- **Most cards** built on ComputerCard so far drive both pads low, which **jams the bus** for every other module (crow, Teletype, Just Friends) while they run. Nothing is damaged, and ii works again once blackbird is back. (Newer versions of ComputerCard leave the pads alone.)
- **A card that uses the pads as a serial port** (UART) drives the SDA line high, against any device that pulls it low. That's out of spec for both chips. It's unlikely to cause instant damage, but it shouldn't be left running on a busy bus. Of the published cards, 45_bends does this.

So either disconnect the ii wires (a switch or a plug-in header makes this easy) when you run other cards, or fit a **220-330Ω resistor in series** with each of SDA and SCL, close to the Computer. That limits the current if any card drives the lines, and doesn't stop ii from working.

Wire the pads to an ii bus *or* to a serial (UART) partner, never both at once.

## Leading

The syntax is crow's: see crow's [ii reference](https://monome.org/docs/crow/reference/#ii). For example:

```lua
ii.jf.mode(1)
ii.jf.play_note(0, 5)          -- Just Friends: a note at 0V, velocity 5

ii.crow.volts(1, 2.5)          -- another crow's output 1 to 2.5V

ii.crow.event = function(e, value)
  print(e.name, e.device, value)
end
ii.crow.get('input', 1)        -- ask that crow for its input 1: the answer arrives in .event
```

Supported devices: Ansible (and Kria, Meadowphysics, Levels), crow, disting EX, ER-301, 16n Faderbank, Just Friends, TELEXi, TELEXo, W/Delay, W/Syn and W/Tape. `ii.help()` lists them, and `ii.<device>.help()` lists a device's commands.

- `ii.raw(address, bytes, [read_length])` sends raw bytes; a reply arrives in `ii.event_raw(address, command, data, arg)`.
- `ii.fastmode(true)` runs the bus at 400kHz (100kHz by default, as crow).
- Messages to an address nothing answers are dropped quickly and don't hold up the bus.

## Following

Blackbird answers at **crow address 4**: to a Teletype it's crow 4 (select it with `CROW.SEL 4`, or prefix a crow op with `CROW4:`), and to another crow it's `ii.crow[4]`. A script can move it to crow 1-3 with `ii.set_address(n)` (`ii.get_address()` reads it back).

With no script, blackbird already answers crow's built-in commands: `volts`, `slew`, `reset`, `pulse`, `ar` and `lfo` act on outputs 1-4, and the `input` and `output` queries report its inputs and outputs. A script handles the rest:

```lua
ii.self.call = function(args)         -- call1 .. call4 from the leader
  print('called with', #args, 'arguments')
end

ii.self.query = function(args)        -- query0 .. query3: answer with a whole number
  return math.floor(bb.knob.main * 100)
end
```

Query answers are 16-bit whole numbers, as on crow. `^^k` (and loading a new script) puts the handlers back to these defaults.
