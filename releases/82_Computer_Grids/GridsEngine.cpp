#include "GridsEngine.h"
#include "GridsResources.h"

namespace {
static inline uint32_t XorShift32(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

static inline uint8_t MixU8(uint8_t a, uint8_t b, uint8_t amount) {
  const int16_t delta = static_cast<int16_t>(b) - static_cast<int16_t>(a);
  return static_cast<uint8_t>(a + ((delta * amount) >> 8));
}

constexpr uint8_t kDrumMap[5][5] = {
    {10, 8, 0, 9, 11},
    {15, 7, 13, 12, 6},
    {18, 14, 4, 5, 3},
    {23, 16, 21, 1, 2},
    {24, 19, 17, 20, 22},
};
}  // namespace

void GridsEngine::Seed(uint32_t seed) {
  rng_ = seed ? seed : 1;
}

void GridsEngine::Reset() {
  step_ = 0;
}

uint8_t GridsEngine::ReadDrumMap(uint8_t step, uint8_t instrument, uint8_t x, uint8_t y) const {
  const uint8_t i = x >> 6;
  const uint8_t j = y >> 6;
  const uint8_t xMix = static_cast<uint8_t>(x << 2);
  const uint8_t yMix = static_cast<uint8_t>(y << 2);

  const uint8_t offset = static_cast<uint8_t>(instrument * GridsResources::kStepsPerPattern + step);
  const auto& aNode = GridsResources::kNodeTable[kDrumMap[i][j]];
  const auto& bNode = GridsResources::kNodeTable[kDrumMap[i + 1][j]];
  const auto& cNode = GridsResources::kNodeTable[kDrumMap[i][j + 1]];
  const auto& dNode = GridsResources::kNodeTable[kDrumMap[i + 1][j + 1]];
  const uint8_t a = aNode[offset];
  const uint8_t b = bNode[offset];
  const uint8_t c = cNode[offset];
  const uint8_t d = dNode[offset];
  return MixU8(MixU8(a, b, xMix), MixU8(c, d, xMix), yMix);
}

GridsEngine::Outputs GridsEngine::Tick(uint16_t map_x, uint16_t map_y, uint16_t fill_lane1, uint16_t fill_lane2,
                                       uint16_t fill_lane3, uint8_t chaos) {
  const uint8_t x8 = static_cast<uint8_t>(map_x >> 4);
  const uint8_t y8 = static_cast<uint8_t>(map_y >> 4);
  const uint16_t fills[3] = {fill_lane1, fill_lane2, fill_lane3};

  if (step_ == 0) {
    const uint8_t randomness = chaos << 1;
    for (uint8_t i = 0; i < 3; ++i) {
      part_perturbation_[i] = static_cast<uint8_t>((XorShift32(rng_) & 0xFF) * randomness >> 8);
    }
  }

  Outputs out;
  bool accent = false;
  for (uint8_t instrument = 0; instrument < 3; ++instrument) {
    uint8_t level = ReadDrumMap(step_, instrument, x8, y8);
    const uint8_t perturb = part_perturbation_[instrument];
    if (level < static_cast<uint8_t>(255 - perturb)) {
      level = static_cast<uint8_t>(level + perturb);
    } else {
      level = 255;
    }
    const uint8_t density = static_cast<uint8_t>(fills[instrument] >> 4);
    const uint8_t threshold = static_cast<uint8_t>(~density);
    const bool hit = level > threshold;
    if (hit && level > 192) {
      accent = true;
    }
    if (instrument == 0) out.lane1 = hit;
    if (instrument == 1) out.lane2 = hit;
    if (instrument == 2) out.lane3 = hit;
  }
  out.accent = accent;

  step_ = static_cast<uint8_t>((step_ + 1) & 0x1F);
  return out;
}

