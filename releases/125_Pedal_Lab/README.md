# Pedal Lab

Pedal Lab is a very experimental attempt to emulate the Spin FV-1 chip on Workshop Computer. 
Obviously entirely vibed and barely tested, but it seems to sound good and be very powerful. 

Spin FV-1 is a DSP chip used in hundreds of effects pedals including Death By Audio Rooms, Earthquaker Devices Rainbow Machine, OBNE Black Fountain, Red Panda Particle. 

FV-1 allows three potentiometers, which is why so many three-pot pedals exist, and fortunately Workshop Computer is a three-knob device. 

The idea of Pedal Lab is to compile Spin FV-1 programs and have them run as UF2.  You can select from a bunch of free-public algorithms, or create your own, you can test them in the web editor, then select 6 to store on the card. 

Tap Z to select a different algorithm, LED shows which you're on. 

You can play with the clock speed of the FV-1, which has interesting/weird effects. Hold down Z and turn the main knob. You can CV Clock speed (which may have interesting/weird effects)  with CV 2 input when Z is in the upward position.

Bigger algos don't like clock speed changes, so LED 5 will come on. 

There is another innovation here - the web editor compiles .uf2 in the browser, using web assembly witchcraft, presumably. 

Lots of things will break, have fun, rework it as you go. 

Presumably you can also decompile pedal firmware updates to extract the FV-1 code, but I haven't tried that. 

I'll let the robot explain the rest... 

Choose effects in the [editor](https://computer.musicthing.co.uk/programs/125-pedal-lab/web/), edit their code, listen, then build and install a UF2. Everything runs in your browser. The first preview or custom build downloads the compiler; later builds reuse it.

The supplied firmware starts with six Keith Barr reverbs.

| Control | Function |
|---|---|
| Main, X, Y | The selected effect’s three parameters |
| Tap Z down | Next effect |
| Hold Z down + Main | Change the effect clock |
| CV 1, CV 2 | Modulate Main and X |
| Z up + CV 2 | Modulate the clock |
| Pulse 1 | Raise Y by half its range |

Audio 1 feeds both channels when Audio 2 is unplugged. LEDs show the selected effect; LED 5 also indicates clock limiting. Heavy effects run at a lower clock, which changes their sound.

Use Chrome or Edge for USB installation, or download the UF2 and copy it to RPI-RP2.

Author credits and individual reuse terms are retained with each algorithm. See [notices](THIRD_PARTY_NOTICES.md).
