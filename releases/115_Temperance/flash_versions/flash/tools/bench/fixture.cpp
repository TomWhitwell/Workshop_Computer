// GPL-3.0 experiment fixture, 2026-11-07. Exercises Matt Allison's
// Temperance engine and the Goldfish-derived storage layer; see ATTRIBUTION.md.
#include <cstddef>
#define private public
#define protected public
#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"
#include "Temperance.h"
#undef private
#undef protected
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
extern "C" __attribute__((used)) const uint32_t bench_norm_offset =
    offsetof(ComputerCard, useNormProbe);
extern "C" tem::Temperance *bench_instrument();
extern "C" void bench_flash_prepare(uint32_t, uint32_t);
static int32_t initialPosition;
static uint32_t recordingMask;

extern "C" void bench_prepare(uint32_t scenario) {
    auto &t = *bench_instrument();
    recordingMask = scenario & 32u ? (scenario & 1u ? 1u : 3u) : 0u;
    bool gate = (scenario & 16u) || recordingMask || (scenario & 64u);
    t.gateMode_ = gate && !(scenario & 64u);
    t.modeWait_ = 0;
    t.open_[0] = t.open_[1] = false;
    t.r_ = 0;
    t.pulse_[0].Reset(); t.pulse_[1].Reset();
    uint32_t held = recordingMask ? recordingMask : ((scenario & 16u) ? 3u : 0u);
    t.gates_.last_ = recordingMask ? 0u : held;
    for (auto &w : t.gates_.bits_) w = held * 0x55555555u;
    if (recordingMask) {
        for (int i = 0; i < tem::kEventDelay; ++i) {
            t.late_[0][i] = (i & 1) ? 1800 : -1800;
            t.late_[1][i] = (i & 1) ? -1800 : 1800;
        }
    }
    uint32_t mode = scenario & 3u;
    const uint32_t bytes = scenario & 8u ? temflash::kMinFlashBytes : temflash::kMaxFlashBytes;
    const uint32_t length = scenario & 8u ? 1000001u : 10000001u;
    initialPosition = (scenario & 4u) ? 0 : length;
    bench_flash_prepare(length, bytes);
    for (auto &p : t.params_) {
        p.running = true;
        p.gate = gate;
        p.clockStep = 0;
        p.zone[0] = mode == 0 ? 5 : mode == 1 ? 3 : 0;
        p.zone[1] = 5;
        p.sendQ15 = 32760;
        p.decayQ15 = 30474;
        p.dampQ15 = 13107;
    }
    tem::Looper *loops[] = {&t.loopA_, &t.loopB_};
    for (auto *l : loops) {
        l->state_ = tem::Looper::kPlaying;
        l->len_ = l->w_ = length;
        l->head_.pos = (scenario & 4u) ? 0 : length; // seam or middle
        l->head_.speed = mode == 3 ? 65536 :
            (l == &t.loopB_ ? 131072 : tem::kZoneSpeed[t.params_[0].zone[0]]);
        l->ghostGain_ = 0;
        l->inGain_ = 65536;
        l->request_[0] = l->request_[1] = {};
        l->playingReady_ = false;
    }
}
extern "C" void bench_service() { temflash::Service(nullptr); }
extern "C" __attribute__((used)) uint32_t bench_debug() {
    const auto &t = *bench_instrument();
    return t.loopA_.State() | (t.loopB_.State() << 2) |
        (temflash::diagnostics.hardwareFault << 4) | ((t.loopB_.head_.pos == initialPosition) << 8);
}
extern "C" uint32_t bench_status() {
    const auto &t = *bench_instrument();
    return (temflash::Healthy() ? 1u : 0u) |
        ((recordingMask & 1u ? t.loopA_.State() == tem::Looper::kRecording && t.loopA_.w_ > 0 : t.loopA_.head_.pos != initialPosition) ? 2u : 0u) |
        ((recordingMask & 2u ? t.loopB_.State() == tem::Looper::kRecording && t.loopB_.w_ > 0 : t.loopB_.head_.pos != initialPosition) ? 4u : 0u);
}
