#pragma once

#include <cstdint>

class GridsEngine {
 public:
  struct Outputs {
    bool lane1 = false;
    bool lane2 = false;
    bool lane3 = false;
    bool accent = false;
  };

  void Seed(uint32_t seed);
  void Reset();
  /** Current pattern step (0–31), before the next Tick() advances it. */
  uint8_t Step() const { return step_; }
  /** map_x/map_y select the pattern node; each lane uses its own fill (0–4095 → density). */
  Outputs Tick(uint16_t map_x, uint16_t map_y, uint16_t fill_lane1, uint16_t fill_lane2, uint16_t fill_lane3,
               uint8_t chaos);

 private:
  uint8_t ReadDrumMap(uint8_t step, uint8_t instrument, uint8_t x, uint8_t y) const;
  uint8_t step_ = 0;
  uint32_t rng_ = 1;
  uint8_t part_perturbation_[3] = {};
};

