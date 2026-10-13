# Processing choice

Audio remains per sample in ComputerCard 0.4.0 on core 0. Motor targets and UI
run at 1 kHz. Geometry targets update every 32 samples and the fractional delays
slew toward those targets every sample. The geometry calculation has its own
worst-case callback cost; updating it less often does not enlarge that deadline.
The DSP is independent of hardware so a later transport change can reuse it.

The user suggested examining Bib's block implementation. Repository reference:
Workshop_Computer/releases/818_Bibesque/main.cpp at revision
7e2b0416093b397b5512de3f111472bd91084c6e. Its README credits the original Bib DSP
to Plinky Synth and its hardware conventions to Chris Johnson's ComputerCard.
We inspected this architecture; no Bib code was copied into this card.

Bib uses its own sample DMA service, four 64-frame buffer slots, a second-core
block worker and a two-block output delay. At 48 kHz each block spans 1.333 ms
and the scheduled output lag is about 2.667 ms, before algorithmic delays.
This is appropriate for retaining Bib's existing 64-frame DSP API. It does
not stop servicing Workshop hardware at sample cadence.

The Leslie's small IIR filters and modulated delays require sequential sample
state either way. A block wrapper alone would not remove that work. We keep
ComputerCard and profile the complete interrupt on hardware before considering
a custom transport. Never process an entire expensive audio block in a single
ProcessSample callback; that callback still has the same deadline.

For Spatial Disorientation, long HRTF convolution could justify FFT/block
processing. The initially proposed short delays and head-shadow filters do not
require it. Revisit once its actual spatial model is selected; development of
that card waits until the user approves this card for releases.
