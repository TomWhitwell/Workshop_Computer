// gba_link.cpp — Core-1 GBA link engine: multiboot, then the post-boot streaming loop.

#include "gba_link.h"
#include "gba_spi.h"
#include "gba_multiboot.h"
#include "gba_proto.h"
#include "patch_store.h"

#include "pico/stdlib.h"
#include <cstring>

GbaShared gGba;

// ---------------------------------------------------------------------------
// Post-boot loop
//
// One 32-bit exchange per poll, mode 3 SPI, RP2040 still the clock master. Downstream is
// almost always a STREAM word carrying the gate plus one of the two input pairs; upstream is
// whatever the GBA had queued, identified by its 16-bit tag. See gba_proto.h.
//
// The tag check is what lets the host reject noise and half-synced words from the slow Pulse
// In 1 line. The tolerance for failures has to be generous: the GBA slave can only have one
// transfer pending and is briefly deaf between finishing one word and re-arming, so a short
// run of tag misses is NORMAL, not a dropped link. An early threshold of 32 at 1 kHz meant
// 32 ms of silence tore the link down and restarted multiboot, which is why it used to connect
// and immediately drop.
// ---------------------------------------------------------------------------

// ~5 s of unbroken silence at the 1 kHz poll rate before declaring the link dead.
static constexpr int kMaxConsecutiveBad = 5000;

// Multiboot rate LADDER, fastest first.
//
// 100 kHz is the measured, repeatable ceiling (mbrate.uf2, 2026-09-07): it succeeded on every
// power-cycle. 200 kHz was MARGINAL — it sometimes completed and often failed partway, leaving
// the console restarting its boot over and over, which is a far worse experience than simply
// running slower. So the ladder deliberately starts at 100 kHz and never attempts above it.
//
// This supersedes an earlier note claiming multiboot ran at 200 kHz. That figure came from an
// automatic ladder that kept the first rate to succeed, which meant it reported whichever rung
// it happened to be on when the console finished booting — timing luck, not a measurement.
// Manual per-speed testing with repeats is what settled it.
// TWO RUNGS, NOT FIVE, AND THE PAYLOAD SIZE IS WHY.
//
// The ladder used to run down to 5 kHz. That was harmless when the payload was 5 kB; with a
// 37 kB payload, 5 kHz is nearly two minutes of transfer during which the module looks hung,
// and 25 kHz is twenty seconds. Since 100 kHz is the rate manual testing proved repeatable
// (mbrate.uf2), a failure at 100 kHz is far more likely to be a one-off than evidence that a
// slower rate is needed — so it is better to fail fast and try again than to grind through
// rungs that each take longer than the last.
static constexpr uint32_t kMultibootLadder[] = { 100'000, 50'000 };
static constexpr int kLadderLen = (int)(sizeof(kMultibootLadder) / sizeof(kMultibootLadder[0]));

// THE GAP IS THE POINT, and it applies to EVERY word we send — control words included.
//
// The GBA slave holds ONE pending transfer and re-arms with a read-modify-write of SIOCNT, so
// a word clocked before it has re-armed is not merely lost, it is CORRUPTED. Measured clean at
// 2000 words/s over 25 passes (linkrate.uf2); 1 kHz sits at half that with 1 ms of latency.
// At 100 kHz SCK a 32-bit word occupies 320 us, leaving ~680 us of slack.
//
// The rock-steady "dropped 2, corrupt 4" that survived a 20x rate range was one out-of-band
// word sent without this gap, destroying the word behind it. Do not special-case anything.
static inline void link_gap() { sleep_us(1000); }

void gba_link_core1(const uint8_t *payload, uint32_t payload_size)
{
    gba_spi_init(kMultibootLadder[0]);

    int ladderIx = 0;          // index of the rate to try first; sticks once one works

    for (;;) {
        // ---- (re)connect: walk the rate ladder until multiboot succeeds ----
        gGba.state = LinkState::Connecting;

        MultibootResult r = MultibootResult::NoGBA;
        int tried = 0;
        for (; tried < kLadderLen; tried++) {
            int ix = (ladderIx + tried) % kLadderLen;
            uint32_t hz = kMultibootLadder[ix];
            gGba.linkHz = hz;                      // published so the UI can show the rate
            gba_spi_set_clock(hz);

            r = gba_multiboot_send(payload, payload_size);
            if (r == MultibootResult::Ok) { ladderIx = ix; break; }

            gGba.lastError = (uint8_t)r;
            // NoGBA just means nothing answered at this rate — drop to the next one. Any
            // other error means we DID get a conversation and it broke partway, which is
            // much more interesting, so surface it.
            if (r != MultibootResult::NoGBA) gGba.state = LinkState::Error;
        }

        if (r != MultibootResult::Ok) {
            gGba.state = (gGba.lastError == (uint8_t)MultibootResult::NoGBA)
                       ? LinkState::Connecting : LinkState::Error;
            sleep_ms(250);
            continue;
        }

        // ---- booted: give the payload a moment to start its serial slave ----
        gGba.state = LinkState::Booted;
        gba_spi_set_clock(kMultibootLadder[ladderIx]);   // poll at the proven rate, not faster
        sleep_ms(50);

        // Introduce ourselves. Repeated because a single missed word would leave the GBA's CAL
        // page reporting the wrong calibration state for ever, and each one takes its gap like
        // any other word.
        for (int i = 0; i < 4; i++) {
            gba_spi_xfer32(gba_hello_pack(gGba.caps));
            link_gap();
        }

        // ---- streaming loop ----
        int      consecutiveBad = 0;
        int      pair           = 0;    // alternates {CV1,CV2} and {AUD1,AUD2}
        uint32_t reportedEdges  = gGba.gateEdges;
        uint32_t seq            = 0;
        uint32_t knobIx         = 0;
        uint8_t  lastSwitch     = 0xFF;

        // ---- patch transfer state ----
        // A save arrives as a stream of indexed bytes; the seen-bitmap is what lets the host tell
        // "the GBA has sent everything" from "a word went missing", without which a half-received
        // patch would be written to flash and look like corruption later.
        static uint8_t  patchBuf[GBA_PATCH_SLOT_BYTES];
        static uint8_t  patchSeen[GBA_PATCH_SLOT_BYTES / 8];
        int      rxSlot   = -1;
        int      txSlot   = -1;      // a LOAD in progress
        int      txPos    = 0;
        int      txLen    = 0;
        int      ackSlot  = -1;
        int      ackLeft  = 0;

        for (;;) {
            uint32_t word;

            // Housekeeping is interleaved one word at a time so it never displaces more than a
            // single input update.
            uint8_t sw = gGba.switchPos;
            if (txSlot >= 0) {
                // Streaming a stored patch down. This owns the channel for about a quarter of a
                // second; the inputs hold their last value, which is exactly what you want while
                // the instrument is being reconfigured anyway.
                if (txPos < txLen) {
                    // BYTE VALUE AT [15:8], NOT [7:0]. gba_proto.h is explicit about the layout
                    // and the GBA decodes it from [15:8]; packing it into [7:0] here meant every
                    // LOAD delivered 256 zero bytes. The GBA then rejected the magic and left the
                    // patch untouched, and because PATCH_DONE still arrived the page reported a
                    // clean load that had changed nothing.
                    word = gba_control_pack(GBA_OP_PATCH,
                                            ((uint32_t)txPos << 16)
                                          | ((uint32_t)patchBuf[txPos] << 8));
                    txPos++;
                } else {
                    word = gba_control_pack(GBA_OP_PATCH_DONE, (uint32_t)txSlot);
                    txSlot = -1;
                }
            } else if (ackLeft > 0) {
                // Repeated a few times: a lost ack would leave the GBA resending for ever.
                ackLeft--;
                word = gba_control_pack(GBA_OP_PATCH_ACK, (uint32_t)ackSlot);
            } else if (sw != lastSwitch) {
                // THE SWITCH GOES FIRST, the moment it moves. It is a trigger source on the GBA
                // (the SW column of the TRIG page), so it earns the same priority the note gets
                // in the upstream direction: one poll of latency rather than a wait for its turn
                // in a rotation. A momentary switch used to play notes is unusable otherwise.
                lastSwitch = sw;
                word = gba_control_pack(GBA_OP_SWITCH, sw);
            } else if ((seq & 0x7u) == 0x7u) {
                // Knobs round-robin, one word in eight. They are modulation sources now rather
                // than a display curiosity, so about 40 Hz each instead of the previous 4 Hz.
                // That costs an eighth of the stream and still leaves each CV pair above 400 Hz.
                word = gba_knob_pack(knobIx, gGba.knobs[knobIx]);
                knobIx = (knobIx + 1u) % 3u;
            } else if ((seq & 0x1FFu) == 0x100u) {
                // Which slots hold a patch, so the GBA's store page can show free from used
                // without asking. Cheap, and it keeps the display honest after a save.
                word = gba_control_pack(GBA_OP_SLOTS, gGba.patchMask);
            } else if ((seq & 0x7FFu) == 0x400u) {
                // Deliberately NOT congruent to 7 mod 8, so it can never collide with a knob slot
                // and quietly cost a knob update every couple of seconds.
                word = gba_hello_pack(gGba.caps);
            } else {
                // The hot path. Report exactly one gate edge per increment: advancing by one
                // rather than jumping to the current count means a burst of triggers arrives
                // in order instead of being collapsed into one.
                uint32_t edges = gGba.gateEdges;
                int      edge  = (edges != reportedEdges);

                word = gba_stream_pack(pair, gGba.gate, edge,
                                       gGba.inputs[pair ? GBA_IN_AUD1 : GBA_IN_CV1],
                                       gGba.inputs[pair ? GBA_IN_AUD2 : GBA_IN_CV2]);
                if (edge) reportedEdges++;
                pair ^= 1;
            }
            seq++;

            uint32_t reply = gba_spi_xfer32(word);

            switch (GBA_UP_TAG(reply)) {
            case GBA_UP_BUTTONS:
                gGba.buttons = (uint16_t)GBA_UP_DATA(reply);
                break;
            case GBA_UP_NOTE:
                gGba.note    = (uint8_t)(GBA_UP_DATA(reply) >> 8);
                gGba.noteVel = (uint8_t)(GBA_UP_DATA(reply) & 0xFF);
                break;
            case GBA_UP_STATUS:
                gGba.page  = (uint8_t)((GBA_UP_DATA(reply) >> 12) & 0xF);
                gGba.mode  = (uint8_t)((GBA_UP_DATA(reply) >>  8) & 0xF);
                gGba.flags = (uint8_t)(GBA_UP_DATA(reply) & 0xFF);
                break;
            case GBA_UP_PATCH: {
                uint32_t d  = GBA_UP_DATA(reply);
                uint32_t ix = (d >> 8) & 0xFFu;
                if (rxSlot >= 0 && ix < GBA_PATCH_SLOT_BYTES) {
                    patchBuf[ix]         = (uint8_t)(d & 0xFFu);
                    patchSeen[ix >> 3]  |= (uint8_t)(1u << (ix & 7));
                }
                break;
            }

            case GBA_UP_REQ: {
                uint32_t d    = GBA_UP_DATA(reply);
                uint32_t req  = (d >> 12) & 0xFu;
                uint32_t slot = (d >> 8) & 0xFu;

                if (req == GBA_REQ_SAVE) {
                    if (rxSlot != (int)slot) {
                        rxSlot = (int)slot;
                        memset(patchSeen, 0, sizeof(patchSeen));
                    }
                } else if (req == GBA_REQ_SAVE_END && rxSlot == (int)slot) {
                    // Only commit a COMPLETE block. An incomplete one is simply ignored: the GBA
                    // is still repeating the stream, so the next pass fills the holes and the
                    // save succeeds a few hundred milliseconds later instead of storing rubbish.
                    bool full = true;
                    for (unsigned i = 0; i < sizeof(patchSeen); i++)
                        if (patchSeen[i] != 0xFF) { full = false; break; }
                    if (full && patch_store_save((int)slot, patchBuf)) {
                        ackSlot = (int)slot;
                        ackLeft = 4;
                        rxSlot  = -1;
                        gGba.patchMask = patch_store_used();
                    }
                } else if (req == GBA_REQ_LOAD && txSlot < 0) {
                    txLen = GBA_PATCH_SLOT_BYTES;
                    if (patch_store_load((int)slot, patchBuf)) {
                        txSlot = (int)slot;
                        txPos  = 0;
                    } else {
                        // Nothing stored there: say so at once rather than sending 256 bytes of
                        // erased flash for the GBA to reject.
                        gba_spi_xfer32(gba_control_pack(GBA_OP_PATCH_DONE, slot | 0x80u));
                        link_gap();
                    }
                }
                break;
            }

            case GBA_UP_PARAM:
                break;                          // reserved
            default:
                gGba.rxBad++;
                if (++consecutiveBad > kMaxConsecutiveBad) goto dropped;
                link_gap();
                continue;
            }

            gGba.rxSeq++;
            consecutiveBad = 0;
            link_gap();
        }

    dropped:
        ;   // fall out to the reconnect loop
    }
}
