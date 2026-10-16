# EightMU source and local changes

Source: Chris Johnson's EightMU.h in Workshop Computer release 801_WaveSeq,
revision `545696badba02b9e345276ccc312826b899c796d`:
https://github.com/TomWhitwell/Workshop_Computer/blob/545696badba02b9e345276ccc312826b899c796d/releases/801_WaveSeq/EightMU.h

The header preserves Chris Johnson's 2026 MIT notice and the bundled
rppicomidi usb_midi_host driver's 2023 MIT notice. Original source and all local
changes remain MIT. USB host/device startup follows WaveSeq/main.cpp and its
CMake/TinyUSB configuration at the same revision. This is an adapted copy;
ComputerCard 0.4.0 remains unmodified.

Local changes by Adrian Vos (soveda), 2026:

- Poll uses zero-wait host task service and one extra bounded RX drain.
- RX callback reads at most 64 stream bytes, rather than draining indefinitely.
- TX reserves space for each complete 3-byte note or 6-byte SysEx; no retry loop
  or recursive USB task runs when the FIFO is full.
- Valid fader snapshot/CC masks prevent initial unreceived zeros claiming control.
- Motion values clear on disconnect; yaw rate expires 150 ms after its last CC.
- A connection generation resets pickup/motion even if a fast detach/attach skips
  the disconnected snapshot. Core 0 also expires yaw after 150 ms without a snapshot.
- Automatic deinit/reinit recovery is disabled while audio runs. If USB fails to
  enumerate after replugging, reset the card with the 8mu attached.

The existing identify handshake loads the 8mu's default assignments, switches it
to bank 1 and disables its bank selection until the 8mu is power-cycled. These
are inherited EightMU behaviours, not additional persistent writes by this card.
The controller's own customized setup may therefore be temporarily replaced
while attached. No controller configuration editing is implemented here.

Official documentation reviewed for USB MIDI, default faders and gyro CC pairs:
https://www.musicthing.co.uk/8mu_v1_docs/
https://www.musicthing.co.uk/8mu_v2_docs/
https://www.musicthing.co.uk/collateral/8mu_quickstart.pdf

The older manual/quick-start labels disagree about the pitch/roll CC pairs;
this pass uses the EightMU class's Roll (44/45) and Yaw (46/47) definitions.
Physical axis/polarity is a hardware-test item. Paired motion controllers are
subtracted; they are not centred 7-bit faders. Gyro is a rotation rate rather
than an absolute heading. TinyUSB host enumeration has long debounce/reset waits;
the application's delay hook services the audio worker during those waits without
recursively servicing USB. No hardware compatibility/timing claim follows from
building this code.

This attribution/change record: Adrian Vos (soveda), 2026, MIT.
