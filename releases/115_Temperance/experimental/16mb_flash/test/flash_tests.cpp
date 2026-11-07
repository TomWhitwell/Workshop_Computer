// Tests created for this experiment with OpenAI Codex, 2026-11-04.
// Overall experimental licence: GNU GPL version 3; see ../LICENSE.
// Exercises Matt Allison's Temperance and Dune Desormeaux's ADPCM codec.
// See ../ATTRIBUTION.md for source origins and license notices.
#include "FlashStore.h"
#include "Looper.h"
#include "Temperance.h"
#include "adpcm.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace temflash;

static int Tone(unsigned i, unsigned channel = 0) {
    return int(1500 * std::sin(double(i) * (channel ? 0.043 : 0.031)));
}

static std::vector<int> Reference(unsigned count, unsigned channel = 0) {
    std::vector<int> result;
    adpcm_state_t encoder = {0, 0}, decoder = {0, 0};
    for (unsigned i = 0; i < count; ++i) {
        uint8_t n = adpcm_encode(int16_t(Tone(i, channel) * 16), &encoder);
        result.push_back(adpcm_decode(n, &decoder) / 16);
    }
    return result;
}

static void Flush(unsigned channel) {
    for (unsigned i = 0; i < 200 && !Ready(channel); ++i) Service(nullptr);
    assert(Ready(channel));
}

static void CheckRead(unsigned channel, unsigned pos, unsigned length, int expected, int direction) {
    Request(channel, 0, pos, direction, length, true);
    for (int i = 0; i < 5; ++i) Service(nullptr);
    assert(Available(channel, 0, pos));
    assert(Read(channel, 0, pos) == expected);
}

static void IndependentRecordings() {
    constexpr unsigned aLen = 120123, bLen = 80617;
    auto a = Reference(aLen), b = Reference(bLen, 1);
    Start(0);
    for (unsigned i = 0; i < aLen; ++i) {
        assert(Record(0, Tone(i)));
        if (i == 1000) Start(1);
        if (i >= 1000 && i < 1000 + bLen) assert(Record(1, Tone(i - 1000, 1)));
        if (i == 1000 + bLen) Stop(1);
        if (i % 192 == 0) Service(nullptr);
    }
    Stop(0);
    Flush(0);
    Flush(1);
    for (unsigned i = 0; i < aLen; i += 397)
        CheckRead(0, i, aLen, a[i], 1);
    for (int i = bLen - 1; i >= 0; i -= 283)
        CheckRead(1, unsigned(i), bLen, b[unsigned(i)], -1);
    for (unsigned p : {0u, 1u, 503u, 504u, 65535u, 65536u, aLen-1})
        CheckRead(0, p, aLen, a[p], -1);
    // Erase/program addresses must never touch the firmware reserve.
    const uint8_t *flash = TestFlash();
    assert(std::all_of(flash, flash + kReserve, [](uint8_t b) { return b == 0xff; }));

    // New take before old final pages finish flushing. Queues must survive
    // session changes, and erasing A must leave B's recording intact.
    Start(0);
    for (unsigned i = 0; i < 1201; ++i) assert(Record(0, -700));
    Stop(0);
    Start(0);
    for (unsigned i = 0; i < 1803; ++i) assert(Record(0, Tone(i)));
    Stop(0);
    Flush(0);
    auto newest = Reference(1803);
    for (unsigned i = 0; i < 1803; i += 31) CheckRead(0, i, 1803, newest[i], 1);
    CheckRead(1, 65536, bLen, b[65536], -1);
    assert(diagnostics.queueOverruns == 0);
    std::puts("PASS independent recording, exact odd lengths, reverse/seeks, queued restart, partition isolation");
}

static void LooperControls() {
    tem::Looper a, b;
    a.Channel(0);
    b.Channel(1);
    uint32_t now = 0xffff0000u; // exercise unsigned double-tap timestamp wrap
    a.Tap(now, 0);
    b.Tap(now, 0);
    for (unsigned i = 0; i < 100003; ++i) {
        a.Tick(Tone(i), 65536, 0);
        b.Tick(Tone(i, 1), -32768, 0);
        ++now;
        if (i == 75000) b.Tap(now, 0);
        if (i % 48 == 0) Service(nullptr);
    }
    a.Tap(now, 0);
    assert(a.Len() == 100003 && b.Len() == 75001);
    assert(a.State() == tem::Looper::kPlaying && b.State() == tem::Looper::kPlaying);
    Flush(0);
    Flush(1);
    for (int speed : tem::kZoneSpeed) {
        for (unsigned i = 0; i < 220000; ++i) {
            int av = a.Tick(0, speed, 0);
            int bv = b.Tick(0, -speed, 0);
            assert(av >= -4096 && av <= 4095 && bv >= -4096 && bv <= 4095);
            assert(a.Pos() >= 0 && a.Pos() < a.Len());
            assert(b.Pos() >= 0 && b.Pos() < b.Len());
            if (i % 48 == 0) Service(nullptr);
        }
    }
    assert(a.Passes() > 5 && b.Passes() > 5);
    a.Tap(now, 0);
    for (unsigned i = 0; i < 100; ++i) a.Tick(0, 65536, 0);
    a.Tap(now + 100, 0);
    assert(a.State() == tem::Looper::kEmpty);
    assert(b.State() == tem::Looper::kPlaying);
    std::puts("PASS >65536-sample loops, six speeds, independent lengths, wraps, double-tap clear");
}

static void Instrument() {
    // Both loopers, the real 48kHz clock, dry path, plate and panel control
    // handover run together. Pulse A stops while B continues recording.
    tem::Temperance instrument;
    instrument.Seed(12345);
    tem::Inputs in{};
    tem::Outputs out{};
    int32_t leds[6];
    in.knob[0] = 4095;
    in.knob[1] = 3072;
    in.knob[2] = 3072;
    in.sw = tem::kSwitchMiddle;
    in.pulsePatched[0] = in.pulsePatched[1] = true;
    in.audio2Patched = true;
    in.cv[1] = 1707; // full wash, exercises the plate with nonzero input
    int peak = 0;
    int stereoDifference = 0;
    for (unsigned i = 0; i < 320000; ++i) {
        in.rise[0] = i == 50000 || i == 180000;
        in.rise[1] = i == 51000 || i == 210000;
        in.audio[0] = i < 220000 ? Tone(i) : 0;
        in.audio[1] = i < 220000 ? Tone(i, 1) : 0;
        if (i == 240000) in.knob[0] = 3200; // lower the shared sample clock
        if (i == 260000) in.knob[0] = 0; // 2kHz
        if (i == 280000) in.knob[0] = 4095;
        instrument.Audio(in, out);
        if (i % 48 == 0) {
            instrument.ControlTick(leds);
            Service(nullptr);
        }
        assert(out.audio[0] >= -2048 && out.audio[0] <= 2047);
        assert(out.audio[1] >= -2048 && out.audio[1] <= 2047);
        if (i > 240000) {
            peak = std::max(peak, std::abs(out.audio[0]));
            stereoDifference = std::max(stereoDifference, std::abs(out.audio[0] - out.audio[1]));
        }
    }
    assert(instrument.Loop(0).Len() == 130000);
    assert(instrument.Loop(1).Len() == 159000);
    assert(peak > 100);
    assert(stereoDifference > 100);
    assert(instrument.Params().sendQ15 > 30000);
    assert(Healthy());
    std::puts("PASS complete instrument, delayed independent pulse recording, audible loop output");
}

static void FullCapacity() {
    Start(0);
    for (unsigned i = 0; i < kCapacity; ++i) {
        assert(Record(0, 600));
        if (i % 504 == 0) Service(nullptr);
    }
    assert(!Record(0, 600));
    Stop(0);
    Flush(0);
    CheckRead(0, kCapacity - 1, kCapacity, 600, -1);
    assert(kReserve + kRegionBytes * 2 == kFlashBytes);
    assert(Healthy());
    std::printf("PASS complete %u-sample capacity, final flash page, full-buffer stop\n", kCapacity);
}

static void ParameterChangeRegression() {
    // Hardware report: long recorded loops, Main fully up (48kHz), switch
    // middle, Y at max, then moving X or Y freezes LEDs and stops audio.
    tem::Temperance instrument;
    tem::Inputs in{};
    tem::Outputs out{};
    int32_t led[6];
    in.knob[0] = in.knob[1] = in.knob[2] = 4095;
    in.sw = tem::kSwitchMiddle;
    in.pulsePatched[0] = in.pulsePatched[1] = true;
    in.audio2Patched = true;
    in.cv[1] = 1707; // full wash
    uint32_t previousPos[2] = {};
    unsigned moves[2] = {}, zoneMask[2] = {};
    for (unsigned i = 0; i < 720000; ++i) {
        in.rise[0] = in.rise[1] = i == 50000 || i == 300001;
        in.audio[0] = Tone(i);
        in.audio[1] = Tone(i, 1);
        if (i >= 320000) {
            // Across all six zones every 2ms: restart crossfades before the
            // previous 256-tick fade ends, first X, then Y, then both.
            unsigned zone = ((i - 320000) / 96) % 6;
            int value = int(zone * 683 + 341);
            in.knob[1] = i < 440000 || i >= 560000 ? value : 4095;
            in.knob[2] = i >= 440000 ? value : 4095;
        }
        instrument.Audio(in, out);
        if (i % 48 == 0) {
            instrument.ControlTick(led);
            Service(nullptr);
        }
        if (i > 320000) {
            assert(instrument.Params().clockStep == 0);
            for (unsigned k = 0; k < 2; ++k) {
                assert(instrument.Loop(k).Len() == 250001);
                auto pos = uint32_t(instrument.Loop(k).Pos());
                assert(pos < 250001);
                moves[k] += pos != previousPos[k];
                previousPos[k] = pos;
                zoneMask[k] |= 1u << instrument.Params().zone[k];
            }
            assert(out.audio[0] >= -2048 && out.audio[0] <= 2047);
            assert(out.audio[1] >= -2048 && out.audio[1] <= 2047);
        }
    }
    assert(Healthy());
    assert(moves[0] > 100000 && moves[1] > 100000);
    assert(zoneMask[0] == 63 && zoneMask[1] == 63);
    std::puts("PASS 48kHz long-loop parameter regression: Y max, rapid X/Y changes, full wash");
}

static void CachePublicationRegression() {
    CachedBlock block;
    block.generation = 7;
    block.block = 1;
    block.start = 504;
    block.pcm[12] = 1600;
    PlaybackReader r{&block, 504, 7, 0};
    int32_t value;
    assert(ReadCached(r, 516, value) && value == 100);
    block.sequence = 1;
    assert(!ReadCached(r, 516, value)); // worker writing PCM
    block.sequence = 2;
    block.generation = 8;
    assert(!ReadCached(r, 516, value)); // another recording
    block.generation = 7;
    block.start = 1008;
    assert(!ReadCached(r, 516, value)); // slot evicted/reused
    assert(!ReadCached(r, 503, value)); // unsigned underflow at range start
    assert(!ReadCached(r, 1008, value)); // exclusive end
    std::puts("PASS cached-reader sequence/session/eviction/range guards");
}

static unsigned delayCalls = 0;
static void DuringProgram() {
    ++delayCalls;
    // Simulate the audio interrupt producing samples while core 1 programs.
    for (unsigned i = 0; i < 48; ++i) assert(Record(0, 400));
}

static void ConcurrentProducer() {
    Start(0);
    for (unsigned i = 0; i < kBlockSamples; ++i) assert(Record(0, 400));
    TestProgramDelay(DuringProgram);
    for (unsigned i = 0; i < 100; ++i) Service(nullptr);
    TestProgramDelay(nullptr);
    Stop(0);
    Flush(0);
    assert(delayCalls > 0 && Healthy());
    std::puts("PASS producer advances during consumer page programming");
}

static void Overflow() {
    Start(0);
    for (unsigned i = 0; i < kQueuePages * kBlockSamples; ++i) assert(Record(0, 0));
    assert(!Record(0, 0));
    assert(diagnostics.queueOverruns == 1 && !Healthy());
    Stop(0);
    Service(nullptr);
    tem::Temperance instrument;
    tem::Inputs in{};
    tem::Outputs out{};
    int32_t led[6];
    in.sw = tem::kSwitchMiddle;
    in.knob[0] = 4095;
    in.audio[0] = 600;
    for (unsigned i = 0; i < 10000; ++i) {
        instrument.Audio(in, out);
        if (i % 48 == 0) instrument.ControlTick(led);
    }
    assert(out.audio[0] == 600 && out.audio[1] == 600);
    assert(!out.pulse[1]);
    std::puts("PASS stalled-worker queue overflow latches fault without overwriting pending pages");
    std::puts("PASS storage fault leaves clean dry audio and control updates running");
}

int main() {
    TestFlashSize(2u * 1024 * 1024);
    assert(!Init());
    assert(!Record(0, 100));
    TestFlashSize(kFlashBytes);
    assert(Init());
    IndependentRecordings();
    LooperControls();
    Instrument();
    ParameterChangeRegression();
    CachePublicationRegression();
    FullCapacity();
    ConcurrentProducer();
    assert(diagnostics.maxQueued <= kQueuePages);
    Overflow();
    std::puts("All flash experiment tests passed.");
}
