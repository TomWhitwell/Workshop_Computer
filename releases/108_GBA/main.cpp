// GBA — PSG voice for the Music Thing Workshop Computer
//
// Boots a cartridge-less Game Boy Advance over the pulse jacks via BIOS Multiboot, then runs a
// live SPI link so the GBA is a synth voice played by the modular — audio out of its own
// headphone jack, sound editing on its own screen.
//
//   Pulse Out 1 (GPIO 8) -> GBA SC  (serial clock, master-driven)
//   Pulse Out 2 (GPIO 9) -> GBA SI  (MOSI)
//   Pulse In  1 (GPIO 2) -> GBA SO  (MISO)
//   (common ground)
//
// THE DIVISION OF LABOUR: the Workshop senses, the GBA is the instrument. This side reads the
// five inputs and streams them down raw; the GBA owns pitch tracking, the modulation matrix,
// the envelope and the UI. That is what makes the on-screen editor free — re-mapping an input
// is a change to a table in GBA RAM, not a protocol change and not a firmware rebuild.
//
// Panel:
//   CV In 2      1V/oct pitch          Pulse In 2   gate / trigger
//   CV In 1      timbre (pulse duty)   Audio In 1   channel-2 detune
//   Audio In 2   noise level           (all four are re-assignable on the GBA's MAP page)
//   CV Out 2     quantised pitch, calibrated 1V/oct
//   CV Out 1     gate out, 5 V
//   Audio Out 1  GBA A button gate       Audio Out 2  GBA B button gate
//
// Architecture: ComputerCard's 48 kHz audio/CV loop runs on core 0 (ProcessSample below). The
// entire GBA link engine runs on core 1 (gba_link_core1) and communicates only through the
// lock-free GbaShared struct — the audio path is never blocked by the link. This mirrors
// 96_cathode's core-0/core-1 split for the pulse pins.

#include "ComputerCard.h"
#include "pico/multicore.h"

#include "gba_link.h"
#include "gba_proto.h"
#include "patch_store.h"
#include "gba_multiboot.h"

#include "pico/flash.h"
#include "gba_payload.h"   // baked multiboot .mb image: gba_payload[] / gba_payload_size

// ─────────────────────────────────────────────────────────────────────────────
// Core 1 trampoline — hands the baked payload to the link engine.
// ─────────────────────────────────────────────────────────────────────────────
static void core1_entry()
{
    gba_link_core1(gba_payload, gba_payload_size);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputerCard subclass — Core 0 (48 kHz)
// ─────────────────────────────────────────────────────────────────────────────
class GbaCard : public ComputerCard
{
public:
    GbaCard()
    {
        // Tell the GBA whether the quantised pitch out can be trusted, BEFORE core 1 starts:
        // the EEPROM calibration is read by ComputerCard's own constructor, which has already
        // run by the time we get here, and the link sends its first HELLO within milliseconds
        // of being launched below.
        gGba.caps      = CVOutsCalibrated() ? GBA_CAP_CVOUT_CAL : 0;
        gGba.patchMask = patch_store_used();

        // Core 1 is NOT launched here any more — see StartLink(). The flash lockout has to be
        // armed on core 0 first, and that has to happen before anything on core 1 could ask for
        // a patch save.
    }

    // Launch the link engine on core 1. Core 1 immediately reassigns GPIO 8/9/2 to PIO;
    // ComputerCard's own gpio use of those pins (PulseOut1/2, PulseIn1) is then simply superseded
    // on the pad. Core 1's PIO/DMA claims don't collide with ComputerCard, which claims its
    // resources later inside Run() (see 96_cathode for the same pattern).
    void StartLink() { multicore_launch_core1(core1_entry); }

    void __not_in_flash_func(ProcessSample)() override
    {
        // ---- Workshop -> GBA : the five inputs ----
        // Signed -2048..2047 biased to 12-bit unsigned for the wire. The GBA subtracts 2048
        // again, so 0 V is 0 V at both ends and there is exactly one place the convention
        // lives (gba_proto.h).
        gGba.inputs[GBA_IN_CV1]  = (uint16_t)(CVIn1()    + 2048);
        gGba.inputs[GBA_IN_CV2]  = (uint16_t)(CVIn2()    + 2048);
        gGba.inputs[GBA_IN_AUD1] = (uint16_t)(AudioIn1() + 2048);
        gGba.inputs[GBA_IN_AUD2] = (uint16_t)(AudioIn2() + 2048);

        gGba.gate = PulseIn2() ? 1 : 0;

        // A monotonic counter, not a sticky flag: core 1 is the only reader and it advances
        // its own tally by one per report, so a trigger far shorter than the 1 ms poll can
        // neither be lost to a clear-vs-set race nor collapsed into its neighbour.
        if (PulseIn2RisingEdge()) gGba.gateEdges = gGba.gateEdges + 1;

        gGba.knobs[0] = (uint16_t)KnobVal(Knob::Main);
        gGba.knobs[1] = (uint16_t)KnobVal(Knob::X);
        gGba.knobs[2] = (uint16_t)KnobVal(Knob::Y);
        gGba.switchPos = (uint8_t)SwitchVal();

        // ---- GBA -> Workshop : the note it is sounding ----
        // CVOut2MIDINote is CALIBRATED from the module's EEPROM, so the quantised pitch out is
        // in tune with the rest of the rack without any trimming here. (The CV INPUTS have no
        // such calibration, which is why the GBA carries a CAL page for the pitch input.)
        CVOut2MIDINote(gGba.note);
        CVOut1Millivolts(gGba.noteVel ? 5000 : 0);

        // Audio outs carry the GBA's A and B buttons as gates, so the performance gestures are
        // patchable by the rest of the rack — mult A into another module's trigger and it follows
        // your fingers. These are the PHYSICAL buttons, so they track whatever the BTN page has
        // assigned them to (A is TRIGGER and B is HOLD by default).
        //
        // They previously mirrored the gate, which said nothing the rack did not already know:
        // Audio Out 1 duplicated CV Out 1, and Audio Out 2 echoed whatever was patched into
        // Pulse In 2 in the first place.
        AudioOut1((gGba.buttons & GBA_A) ? 2000 : 0);
        AudioOut2((gGba.buttons & GBA_B) ? 2000 : 0);

        // ---- status on the LEDs ----
        // While the link is up these are instrument state. While it is NOT, they become a
        // diagnostic readout instead — because "LED 0 is blinking" was true for uploading,
        // waiting for a console, and every kind of failure alike, which is no use at all when
        // the thing you need to know is which of those is happening.
        bool booted = (gGba.state == LinkState::Booted);
        if (booted) {
            LedOn(0, true);
            LedOn(1, gGba.gate);
            LedOn(2, gGba.noteVel != 0);
            LedOn(3, gGba.mode == GBA_MODE_EDIT);
            LedOn(4, (gGba.page & 1) != 0);            // edit page as a binary pair
            LedOn(5, (gGba.page & 2) != 0);
        } else {
            LedOn(0, (tick_ >> 12) & 1);               // blinking: not booted
            uint32_t p = gba_mb_progress;
            if (p > 0) {
                // Uploading: LEDs 1-5 are a five-segment progress bar. A 37 kB payload is five
                // and a half seconds of transfer, and a bar moving is the difference between
                // "wait" and "something is wrong".
                for (int i = 0; i < 5; i++) LedOn(1 + i, (int)p >= (i + 1) * 20);
            } else {
                // Idle or failed: LEDs 1-3 are the last MultibootResult as a 3-bit code.
                //   1 NoGBA (nothing answered)   2 BadHandshake   3 TransferError
                //   4 CrcMismatch                5 BadPayload
                uint8_t e = gGba.lastError;
                LedOn(1, e & 1);
                LedOn(2, e & 2);
                LedOn(3, e & 4);
                LedOn(4, false);
                LedOn(5, false);
            }
        }

        tick_++;
    }

private:
    uint32_t tick_ = 0;
};

int main()
{
    // 144 MHz — a clean multiple of 48 MHz (as 96_cathode uses), giving tidy PIO clkdivs.
    set_sys_clock_khz(144000, true);

    GbaCard card;
    card.EnableNormalisationProbe();   // so an unpatched input reads 0 V instead of noise

    // Launch core 1 FIRST, then arm the flash lockout. THE ORDER MATTERS AND GETTING IT WRONG
    // IS SILENT.
    //
    // multicore_lockout_victim_init() installs an exclusive handler on core 0's inter-core FIFO
    // interrupt. multicore_launch_core1() also uses that FIFO, for its startup handshake — so
    // arming the lockout first means the handler eats the handshake replies and core 0 blocks
    // for ever inside the launch, never reaching Run(). Core 1 still comes up and the GBA still
    // boots and plays, which is what makes it so misleading: the console looks alive while the
    // module's 48 kHz loop has never run once, so every knob, the switch and every CV input sit
    // frozen at their power-on values.
    //
    // Armed after the launch, the lockout does its real job: a patch save erases and reprograms
    // a flash sector, which stops XIP, and any code running from flash on either core during
    // that window crashes the chip. Core 0 lives in ComputerCard's Run() loop, which is in
    // flash. This parks it in a RAM-resident handler for the few milliseconds the write takes —
    // and because the audio interrupt does not run during the lockout, a save clicks. That is
    // the price of a deliberate action, and the reason this is not done from the audio path.
    card.StartLink();
    multicore_lockout_victim_init();

    // GPIO 2/8/9 are owned by the PIO link engine, not by ComputerCard's pulse I/O.
    card.Run();   // blocking; core 0 runs the 48 kHz loop, core 1 runs the GBA link
}
