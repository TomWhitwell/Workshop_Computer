// link.c — GBA serial slave: harvest a word, decode it, re-arm.
//
// The harvest body (link_pump) is the same logic that has run since bring-up. What is new is
// that it can be driven by the serial interrupt as well as by polled calls, and that it now
// decodes the typed protocol in gba_proto.h instead of four raw parameter bytes.

#include "link.h"

// ---- GBA memory-mapped I/O ----
#define REG_BASE      0x04000000
#define REG_SIODATA32 (*(volatile uint32_t *)(REG_BASE + 0x0120))
#define REG_SIOCNT    (*(volatile uint16_t *)(REG_BASE + 0x0128))
#define REG_RCNT      (*(volatile uint16_t *)(REG_BASE + 0x0134))
#define REG_IE        (*(volatile uint16_t *)(REG_BASE + 0x0200))
#define REG_IF        (*(volatile uint16_t *)(REG_BASE + 0x0202))
#define REG_IME       (*(volatile uint16_t *)(REG_BASE + 0x0208))

// SIOCNT, normal mode. Mode select is SIOCNT bits 13:12 while RCNT[15:14] == 00:
//   00 = Normal 8-bit, 01 = Normal 32-bit, 10 = Multiplay, 11 = UART.
#define SIO_32BIT   (1 << 12)
#define SIO_IRQ     (1 << 14)   // raise IRQ 7 when a transfer completes
#define SIO_START   (1 << 7)    // slave sets this to arm; hardware clears on completion
#define SIO_SLAVE   (0 << 0)    // external clock
#define RCNT_SERIAL 0x0000

#define IRQ_SERIAL  (1 << 7)

// The BIOS reads the user IRQ handler address from here — multiboot images included.
#define BIOS_IRQ_VECTOR (*(volatile uint32_t *)0x03007FFC)

// ---- state the rest of the payload reads ----
volatile uint16_t g_in[4]     = { 2048, 2048, 2048, 2048 };
volatile uint8_t  g_gate      = 0;
volatile uint32_t g_gateEdges = 0;
volatile uint16_t g_knob[3]   = { 0, 0, 0 };
volatile uint8_t  g_switch    = 1;
volatile uint16_t g_hostCaps  = 0;
volatile uint8_t  g_hostSeen  = 0;
volatile uint32_t g_rx        = 0;
volatile uint32_t g_streamRx  = 0;
volatile uint32_t g_streamBad = 0;
volatile uint32_t g_ctlRx     = 0;
volatile uint32_t g_knobRx    = 0;
volatile uint32_t g_swRx      = 0;

// ---- what we send back ----
// Only one word comes back per poll, so the kinds share the channel by PRIORITY, not by
// round-robin: a changed note or gate goes up on the very next reply, buttons fill every
// other slot, status trickles out one word in eight. Note latency then equals one poll, which
// is what the host's CV Out 2 needs to feel connected to the keyboard.
static volatile uint16_t g_buttons   = 0;
static volatile uint16_t g_upNote    = 0;
static volatile uint8_t  g_upNoteDirty = 0;
static volatile uint16_t g_upStatus  = 0;
static volatile uint8_t  g_upStatusDirty = 0;
static volatile uint32_t g_irqCount  = 0;

// ---- diagnostic mode state (see link.h) ----
volatile int      g_bench       = 0;
volatile int      g_benchDirty  = 1;
volatile int      g_linkTest    = 0;
volatile uint32_t g_ltRate      = 0;
volatile uint32_t g_ltVal       = 0;
volatile uint32_t g_ltFailAt    = 0;
volatile int      g_ltFailed    = 0;
volatile uint32_t g_ltPassOk    = 0;
volatile uint32_t g_ltPassBad   = 0;
volatile uint32_t g_ltBadMagic  = 0;
volatile int      g_ltMeasuring = 0;
volatile int      g_ltPassDone  = 0;
volatile int      g_ltArmed     = 0;
volatile uint32_t g_ltSkips     = 0;
volatile uint32_t g_ltWild      = 0;
volatile int      g_ltHave      = 0;
static uint16_t   g_ltPrev      = 0;

// ---- patch transfer state ----
volatile uint8_t  g_xferState  = LINK_XFER_NONE;
volatile uint8_t  g_xferSlot   = 0;
volatile uint8_t  g_xferResult = LINK_RESULT_IDLE;
volatile uint8_t  g_loadReady  = 0;
volatile uint16_t g_slotMask   = 0;
volatile uint8_t  g_patchBuf[GBA_PATCH_SLOT_BYTES];

static volatile int      g_txPos = 0;
static volatile uint8_t  g_rxSeen[GBA_PATCH_SLOT_BYTES / 8];

// A slot is GBA_PATCH_SLOT_BYTES wide and the WHOLE slot is sent, zero-padded past the end of
// the patch. The host has no idea how big a Patch is, so it can only judge a transfer complete
// by seeing every byte of the slot; sending only sizeof(Patch) left the tail bytes permanently
// unseen, the host never committed, never acknowledged, and the GBA resent for ever — which
// also monopolised the upstream channel and froze the button reports.
void link_begin_save(uint8_t slot, const uint8_t *data, int len)
{
    for (int i = 0; i < GBA_PATCH_SLOT_BYTES; i++)
        g_patchBuf[i] = (i < len) ? data[i] : 0;

    g_txPos      = 0;
    g_xferSlot   = slot;
    g_xferResult = LINK_RESULT_IDLE;
    g_xferState  = LINK_XFER_SAVE;
}

void link_begin_load(uint8_t slot)
{
    for (unsigned i = 0; i < sizeof(g_rxSeen); i++) g_rxSeen[i] = 0;
    g_xferSlot   = slot;
    g_xferResult = LINK_RESULT_IDLE;
    g_txPos      = 0;
    g_xferState  = LINK_XFER_LOAD;
}

int link_xfer_progress(void)
{
    if (g_xferState == LINK_XFER_SAVE) {
        int pos = g_txPos;
        return (pos * 100) / (GBA_PATCH_SLOT_BYTES + 2);        // constant divisor
    }
    if (g_xferState == LINK_XFER_LOAD) {
        int n = 0;
        for (unsigned i = 0; i < sizeof(g_rxSeen); i++) {
            uint8_t b = g_rxSeen[i];
            while (b) { n += b & 1; b >>= 1; }
        }
        return (n * 100) / GBA_PATCH_SLOT_BYTES;
    }
    return 100;
}

void link_set_buttons(uint16_t b) { g_buttons = b; }

void link_post_note(uint8_t note, uint8_t gate)
{
    g_upNote = (uint16_t)(((uint16_t)note << 8) | gate);
    g_upNoteDirty = 1;
}

void link_post_status(uint8_t page, uint8_t mode, uint8_t flags)
{
    g_upStatus = (uint16_t)(((page & 0xF) << 12) | ((mode & 0xF) << 8) | flags);
    g_upStatusDirty = 1;
}

// Choose the reply that will be clocked out during the NEXT transfer. The echo necessarily
// lags by one word: the reply for transfer N+1 is preloaded while transfer N is harvested.
static uint32_t next_reply(uint32_t got)
{
    if (g_bench)    return gba_up_pack(GBA_UP_BUTTONS, (uint16_t)got);   // echo downstream
    if (g_linkTest) return gba_up_pack(GBA_UP_BUTTONS, (uint16_t)g_ltVal);

    // A transfer owns the upstream channel while it runs. It is a deliberate, brief action and
    // the note/button traffic can wait a quarter of a second for it.
    if (g_xferState == LINK_XFER_SAVE) {
        const int len = GBA_PATCH_SLOT_BYTES;
        int pos = g_txPos;
        g_txPos = (pos + 1 > len + 1) ? 0 : pos + 1;
        if (pos == 0)   return gba_up_pack(GBA_UP_REQ, (GBA_REQ_SAVE << 12) | (g_xferSlot << 8));
        if (pos <= len) return gba_up_pack(GBA_UP_PATCH, ((pos - 1) << 8) | g_patchBuf[pos - 1]);
        return gba_up_pack(GBA_UP_REQ, (GBA_REQ_SAVE_END << 12) | (g_xferSlot << 8));
    }
    if (g_xferState == LINK_XFER_LOAD) {
        // Ask about once every sixteen words until the host starts answering, so a lost request
        // costs 16 ms rather than hanging the page.
        if ((++g_txPos & 0xF) == 0)
            return gba_up_pack(GBA_UP_REQ, (GBA_REQ_LOAD << 12) | (g_xferSlot << 8));
    }

    if (g_upNoteDirty)   { g_upNoteDirty = 0;   return gba_up_pack(GBA_UP_NOTE,   g_upNote); }
    if (g_upStatusDirty && (g_rx & 7u) == 0u) {
        g_upStatusDirty = 0;
        return gba_up_pack(GBA_UP_STATUS, g_upStatus);
    }
    return gba_up_pack(GBA_UP_BUTTONS, g_buttons);
}

// ---- the applet protocol ------------------------------------------------------------------
static void decode_applet(uint32_t got)
{
    switch (GBA_TAG_OF(got)) {
    case GBA_TAG_STREAM:
        if ((got & 0x3u) != gba_stream_check(got)) { g_streamBad++; return; }
        g_streamRx++;
        g_gate = (got & GBA_ST_GATE) ? 1 : 0;
        if (got & GBA_ST_EDGE) g_gateEdges++;        // counter, not a flag: see link.h
        if (got & GBA_ST_PAIR) {
            g_in[LINK_IN_AUD1] = (uint16_t)GBA_ST_A(got);
            g_in[LINK_IN_AUD2] = (uint16_t)GBA_ST_B(got);
        } else {
            g_in[LINK_IN_CV1]  = (uint16_t)GBA_ST_A(got);
            g_in[LINK_IN_CV2]  = (uint16_t)GBA_ST_B(got);
        }
        return;

    case GBA_TAG_CONTROL: {
        uint32_t arg = GBA_CTL_ARG(got);
        g_ctlRx++;
        switch (GBA_CTL_OP(got)) {
        case GBA_OP_HELLO:
            g_hostCaps = (uint16_t)(arg & 0xFFFFu);
            g_hostSeen = 1;
            break;
        case GBA_OP_KNOB:
            g_knobRx++;
            g_knob[(arg >> 22) & 0x3u] = (uint16_t)((arg >> 10) & 0xFFFu);
            break;
        case GBA_OP_SWITCH:
            g_swRx++;
            g_switch = (uint8_t)(arg & 0x3u);
            break;

        case GBA_OP_PATCH: {
            uint32_t ix = (arg >> 16) & 0xFFu;
            if (ix < GBA_PATCH_SLOT_BYTES) {
                g_patchBuf[ix] = (uint8_t)((arg >> 8) & 0xFFu);
                g_rxSeen[ix >> 3] |= (uint8_t)(1u << (ix & 7));
            }
            break;
        }

        case GBA_OP_PATCH_DONE:
            if (g_xferState == LINK_XFER_LOAD) {
                // Bit 7 means the host had nothing stored in that slot.
                if (arg & 0x80u) {
                    g_xferResult = LINK_RESULT_EMPTY;
                } else {
                    g_xferResult = LINK_RESULT_OK;
                    g_loadReady  = 1;
                }
                g_xferState = LINK_XFER_NONE;
            }
            break;

        case GBA_OP_SLOTS:
            g_slotMask = (uint16_t)(arg & 0xFFFFu);
            break;

        case GBA_OP_PATCH_ACK:
            if (g_xferState == LINK_XFER_SAVE && (arg & 0x1Fu) == g_xferSlot) {
                g_xferResult = LINK_RESULT_OK;
                g_xferState  = LINK_XFER_NONE;
            }
            break;
        default:
            break;   // opcode 0 and anything unknown: ignore, never act on a zero word
        }
        return;
    }

    default:
        g_streamBad++;
        return;
    }
}

// ---- the diagnostic protocols (unchanged behaviour) ---------------------------------------
// Their magics all live under tag 0b101, which gba_proto.h reserves, so they can never
// collide with a live stream word.
static void decode_diagnostic(uint32_t got)
{
    if (got == BENCH_ENTER) { if (!g_bench) { g_bench = 1; g_benchDirty = 1; } }
    if (got == BENCH_LEAVE) { if (g_bench)  { g_bench = 0; g_benchDirty = 1; } }

    uint32_t mag = got >> 16;
    if (mag == LT_SEQ_MAGIC) {
        g_linkTest = 1;
        uint16_t v = (uint16_t)(got & 0x0FFFu);   // 12-bit ramp: 4096 words
        if (!g_ltMeasuring) {
            g_ltPrev = v; g_ltHave = 0;           // drawing: track position, judge nothing
        } else if (!g_ltArmed) {
            // Warm-up: follow the ramp without judging until it wraps, so the pass starts at
            // a known point and boundary artefacts are excluded entirely.
            if (g_ltHave && v < g_ltPrev) {
                g_ltArmed = 1;
                g_ltSkips = 0; g_ltWild = 0; g_ltBadMagic = 0;
                g_ltFailed = 0; g_ltFailAt = 0;
            }
            g_ltPrev = v; g_ltHave = 1; g_ltVal = v;
        } else {
            uint16_t expect = (uint16_t)((g_ltPrev + 1) & 0x0FFFu);
            if (v < g_ltPrev) {
                // Value went backwards = the ramp wrapped. Detecting the wrap this way rather
                // than by waiting for exactly 0 matters: if that one word is dropped, waiting
                // for it would hang the pass for ever.
                g_ltPassDone = 1;
            } else if (v != expect) {
                uint16_t gap = (uint16_t)(v - expect);
                if (gap <= 8) g_ltSkips++; else g_ltWild++;
                if (!g_ltFailed) { g_ltFailed = 1; g_ltFailAt = g_ltPrev; }
            }
            g_ltPrev = v; g_ltVal = v;
        }
    } else if (mag == LT_RATE_MAGIC) {
        g_linkTest = 1;
        // A rate change restarts everything, so what is on screen always describes the rate
        // shown beside it and never a blend of two.
        if ((got & 0xFFFFu) != g_ltRate) {
            g_ltRate = got & 0xFFFFu;
            g_ltPassOk = g_ltPassBad = 0; g_ltBadMagic = 0;
            g_ltFailed = 0; g_ltFailAt = 0; g_ltHave = 0; g_ltVal = 0;
            g_benchDirty = 1;
        }
    } else if (g_linkTest && g_ltMeasuring && g_ltArmed) {
        // Neither magic: corrupt, most likely bit-shifted by the slave re-arming mid transfer.
        g_ltBadMagic++;
        if (!g_ltFailed) { g_ltFailed = 1; g_ltFailAt = g_ltPrev; }
    }
}

// ---- the harvest itself -------------------------------------------------------------------
// Kept deliberately small. Under LINK_USE_IRQ the BIOS calls the handler in IRQ MODE on the
// IRQ stack, which has only about 160 bytes below 0x03007FA0 before it runs into the user
// stack that crt0.s sets at 0x03007F00.
//
// The per-word Hamming-distance analysis that used to live here (counting how many bits a
// word differed from BENCH_ENTER) has gone: the display panel that read those counters was
// removed once the fault it was chasing was closed, so it was a 32-iteration loop on every
// single word feeding nothing. That cost is not acceptable inside an interrupt handler.
// Set once link_init() has configured the port. Before that, SIODATA32 holds whatever the BIOS
// left behind and SIOCNT has never been armed, so every service call during the boot splash was
// harvesting garbage and counting it as a rejected word. That is where the REJ counter's initial
// non-zero reading came from: it was us, before the link existed. Now REJ starts at zero and any
// movement at all is a real event.
static volatile uint8_t g_linkReady = 0;

static void link_pump(void)
{
    if (!g_linkReady) return;
    if (REG_SIOCNT & SIO_START) return;        // transfer still in flight

    uint32_t got = REG_SIODATA32;
    g_rx++;

    // Once a diagnostic mode is running, EVERY word goes to its decoder, not just the ones
    // carrying a magic. Its corruption counter works by counting words that match neither
    // magic, so routing those to the applet decoder instead would silently under-report
    // corruption and change numbers that are already recorded in POSTMORTEM.md.
    if (g_bench || g_linkTest || GBA_TAG_OF(got) == GBA_TAG_DIAGNOSTIC) decode_diagnostic(got);
    else                                                                decode_applet(got);

    REG_SIODATA32 = next_reply(got);
    REG_SIOCNT |= SIO_START;                   // re-arm; also pulls SO low = ready
}

#if LINK_USE_IRQ
// The BIOS handler saves r0-r3/r12/lr and jumps here with IME already 0. A normal C function
// preserves r4-r11 and returns via lr, which is exactly what is required.
static void link_irq_handler(void)
{
    uint16_t pending = REG_IF;
    if (pending & IRQ_SERIAL) {
        g_irqCount++;
        link_pump();
    }
    REG_IF = pending;                          // ack every bit we were handed
}

void link_service(void)
{
    // Mask the interrupt around the body so polled and interrupt-driven harvests are mutually
    // exclusive rather than racing. If the IRQ is working this almost always finds the
    // transfer still pending and falls straight out.
    uint16_t ime = REG_IME;
    REG_IME = 0;
    link_pump();
    REG_IME = ime;
}

int link_irq_alive(void) { return g_irqCount != 0; }
#else
void link_service(void) { link_pump(); }
int  link_irq_alive(void) { return 0; }
#endif

void link_spin(uint32_t iterations)
{
    while (iterations--) link_service();
}

void link_init(void)
{
    REG_RCNT   = RCNT_SERIAL;
    REG_SIOCNT = SIO_32BIT | SIO_SLAVE;
    REG_SIODATA32 = gba_up_pack(GBA_UP_BUTTONS, 0);

#if LINK_USE_IRQ
    REG_IME = 0;
    BIOS_IRQ_VECTOR = (uint32_t)link_irq_handler;
    REG_IF  = 0xFFFF;                          // clear anything stale before enabling
    REG_IE |= IRQ_SERIAL;
    REG_SIOCNT |= SIO_IRQ;
    REG_IME = 1;
#endif

    g_linkReady = 1;
    REG_SIOCNT |= SIO_START;                   // arm immediately
}
