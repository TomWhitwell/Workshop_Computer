
# stretchcore

stretchcore is a Workshop Computer card for playing uploaded mono sample loops with tempo control, timestretch, position jumps, and browser-based audio loading.

## Quickstart

Get UF2 + instructions + sample manager at https://infinitedigits.co/stretchcore/

![stretchcore module image](web/public/stretchcore.png)

## Demo

22 minutes of pure stretchcore: https://www.youtube.com/watch?v=w-iyoTLxIP8


## Audio Loading

Audio is loaded from the web app in `web/` over Web Serial (public instance at [infinitedigits.co/stretchcore/](https://infinitedigits.co/stretchcore/)). The firmware only needs to be flashed once with loader support; after that, sample banks can be replaced from the browser without reflashing firmware.

Samples are stored in flash as 48 kHz mono signed 8-bit PCM. The loader can read the device bank back, detect device flash capacity, show transfer progress with ETA, preview samples, crop waveforms, and upload banks with up to 64 samples.

## Controls

| Control | Function |
| --- | --- |
| Main knob | Timestretch amount |
| X knob | Tempo when not externally clocked |
| Y knob | Position knob for jumps; sample selector when using Toggle Up |
| Toggle Down | Jump playback to the Y knob position; fire Pulse Out 1 |
| Toggle Up | Select sample from the Y knob position; fire Pulse Out 2 |
| CV In 1 | Adds bipolar modulation to Main knob timestretch when plugged in |
| CV In 2 | Bipolar offset for Pulse In 2 jumps |
| Pulse In 1 | External clock / BPM input |
| Pulse In 2 | Jump trigger: jumps to Y knob position plus CV In 2, or Y knob position if CV In 2 is unplugged |
| CV Out 1 | Smooth random slow bipolar LFO |
| CV Out 2 | Smooth random slow bipolar LFO |
| Pulse Out 1 | 20 ms trigger from Toggle Down |
| Pulse Out 2 | 20 ms trigger from Toggle Up |

## Build

```sh
make build
make web-build
```

The firmware artifact is `build/stretchcore.uf2`.

## Acknowledgements

Thanks for [Tom Whitwell](https://github.com/tomwhitwell) for this incredible tool which I find myself constantly referring to as a design of pure genius. Thanks to Chris Johnson for paving the way for the framework and huge thanks to [Dune](https://github.com/dessertplanet/) for continually pushing the boundaries and making incredibly inspirational music from such a small device. Also for letting me steal the 15_MLRws Sample Manager code!
