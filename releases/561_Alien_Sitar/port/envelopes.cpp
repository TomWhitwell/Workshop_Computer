#include "envelopes.hpp"
namespace computer {
int32_t Envelopes::follow(int32_t x, int32_t goal) {
    return mtm::approach(x, goal, goal > x ? 7 : 12);
}

void Envelopes::next(int32_t direct, int32_t wet) {
    dry = follow(dry, mtm::abs32(direct));
    halo = follow(halo, mtm::abs32(wet));
}

int32_t Envelopes::haloMv() const {
    return mtm::clip(halo * 5000 / 600, 0, 5000);
}
} // namespace computer
