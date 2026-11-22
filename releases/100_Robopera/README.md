# Robopera

Build singing robots with your Workshop System.

Robopera is a fun, accessible way to share the wonders of the Workshop System with children. Flash the program card, connect your system to the web editor, and you'll be guided, one step at a time, from your first cable to a singing robot of your very own.

Although designed for little people (supervised by an adult), Robopera is also useful for anyone just starting out with the Workshop System (or anyone looking for a refresher). You can use it to check your instrument is working properly, to get familiar with the modules, and to learn the basics of sound design.

## You'll need

- A Music Thing Modular Workshop System.
- Some patch cables.
- A computer with Chrome, Edge or Opera, and a USB cable that carries data.
- Headphones or a speaker.

## Start

1. **Flash the card.** Pull off the Computer's big Main knob: the boot select button is behind it, on the right. Connect the Workshop System's front USB socket to your computer. Hold the boot select button down and tap the reset button (at the bottom, by the card slot). A drive called **RPI-RP2** appears: drag `robopera.uf2` onto it. Put the Main knob back on.
2. **Open the editor.** Use the **Web Editor** button on Robopera's page at [computer.musicthing.co.uk](https://computer.musicthing.co.uk/), or open `web/index.html` from this folder in Chrome, Edge or Opera.
3. **Follow the steps.**

If the editor can't find your Workshop System, check that your USB cable carries data (some only charge), close other music software, and plug it in again.

## Credits

Robopera by Joe Hedinger (isbn), built with Claude (Anthropic). Thanks to Tom Whitwell and Music Thing Modular.

Robopera's own code is © Joe Hedinger; a licence hasn't been chosen yet. It uses [ComputerCard](https://github.com/chrisgjohnson/ComputerCard) by Chris Johnson (MIT licence), the Raspberry Pi Pico SDK (BSD 3-Clause licence) and the fonts Inter and Newsreader (SIL Open Font Licence 1.1, see `web/fonts/`).
