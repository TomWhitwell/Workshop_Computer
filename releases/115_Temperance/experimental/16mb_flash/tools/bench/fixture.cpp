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
extern "C" void bench_flash_prepare(uint32_t);

extern "C" void bench_prepare(uint32_t scenario) {
    auto &t = *bench_instrument();
    uint32_t mode = scenario & 3u;
    constexpr uint32_t length = 10000001;
    bench_flash_prepare(length);
    for (auto &p : t.params_) {
        p.running = true;
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
    }
}
extern "C" void bench_service() { temflash::Service(nullptr); }
