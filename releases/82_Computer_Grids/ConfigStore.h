#pragma once

#include <cstddef>
#include <cstdint>

class ConfigStore {
 public:
  static constexpr uint32_t kMagic = 0x47524453;  // GRDS
  static constexpr size_t kFlashSize = 2 * 1024 * 1024;
  static constexpr size_t kBlockSize = 4096;
  static constexpr size_t kOffset = kFlashSize - kBlockSize;

  enum CV1Mode : uint8_t { CV1ToX = 0, CV1ToY = 1, CV1ToBlend = 2 };
  enum CV2Mode : uint8_t { CV2ToFill = 0 };
  enum AuxMode : uint8_t { AuxAccent = 0, AuxClock = 1, AuxLane3Mirror = 2 };

  struct Data {
    uint32_t magic = kMagic;
    uint16_t bpm10 = 1200;  // 120.0 BPM

    /** Swing amount 50 (straight) … 75 (strong shuffle), sequencer-style %. */
    uint8_t swing = 50;
    uint8_t chaos = 10;
    uint8_t cv1_mode = CV1ToBlend;
    uint8_t cv2_mode = CV2ToFill;
    int8_t cv1_amount = 64;
    int8_t cv2_amount = 64;

    // Main fill macro scaling.
    uint8_t lane1_fill_scale = 100;
    uint8_t lane2_fill_scale = 85;
    uint8_t lane3_fill_scale = 115;
    int8_t lane1_fill_offset = 0;
    int8_t lane2_fill_offset = 8;
    int8_t lane3_fill_offset = -10;

    uint8_t aux_mode = AuxAccent;
    uint8_t pulse_ms = 10;
    /** Wire size must match `sizeof(Data)` (28); pads SysEx / flash blob without trailing compiler padding. */
    uint8_t reserved[8] = {};
  };

  void Load(bool force_reset = false);
  void Save();
  void SaveData(const Data& data);
  Data& Get();

 private:
  Data config_;
};

