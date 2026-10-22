# Local ComputerCard 0.4.0 flash-pause adaptation

Original ComputerCard ©2024–2026 Chris Johnson, MIT. Full original notice is
preserved in ComputerCard.h and LICENSE. Local additions ©2026 Adrian Vos
(soveda), MIT. Source baseline: repository vendor/ComputerCard/ComputerCard.h,
version0.4.0 dated14June2026. This is a local adaptation, not a new upstream version.

Why: the old card faded and locked out core0 while ADC continued running. Its
FIFO/DMA sampling pipeline can lose channel/mux alignment during flash stalls.
A user reported failed repeated saves, then rapid false CV and stopped audio.
This is a plausible sequencing defect; hardware reproduction/confirmation remains
pending. Do not call the candidate hardware-validated from host tests.

Four added public methods: RequestAudioPauseForFlash, AudioPausedForFlash,
ResumeAudioAfterFlash, AudioRunningAfterFlash. Only core1 writes the aligned
flashPauseRequested word. Core0 alone owns hardware/run mode. The existing mute
handshake precedes a pause request. BufferFull stops ADC, disables DMAIRQ0, aborts
ADC/SPI DMA, clears the pending ADC IRQ and drains FIFO at the end of a sample.
The main Run loop stays alive, permitting FIFO flash lockout while audio is paused.

Release clears the request. Foreground Run drains FIFO, clears FIFO errors,
restarts existing ADC/SPI DMA buffers (without claiming new channels), resets ADC
round robin/input0 and enables ADCIRQ/sampling, then publishes Running. Existing
mux state, double-buffer phase, pickup/control history and block transport state
are preserved. Save holds the output fade/timing suppression until restart.
A canceled request also clears cleanly if core0 has not paused yet. Existing Abort
semantics and upstream API access levels remain. Original enum values0–3 are
preserved; local state4 means flash-paused. No hardware-input read moves outside
ProcessSample; only the sampling engine's stop/restart is controlled here.

The normal BufferFull path gains a pause-request check outside measured user
ProcessSample duration. Hardware must validate total framework timing as well as
reported callback/block peaks; no claim of zero scheduling overhead is made.
The root vendor header and all beta/alpha1 code/UF2 remain unchanged.
