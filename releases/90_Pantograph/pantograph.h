#pragma once

#include <algorithm>
#include <cstdlib>

namespace pantograph {
static constexpr int kMaxFrames = 18750;

int KnobToCV(int knobVal) {
  return knobVal - 2048;
}

int32_t KnobToSpeed(int32_t bigKnob) {
  const int32_t kDeadZone = 40; // TODO tune and adjust
  const int32_t kScale = 128; // 4x speed

  int32_t centered = bigKnob - 2048;

  if (std::abs(centered) < kDeadZone) {
    return 0;
  }

  return centered * kScale;
}

int SumClamp(int cv1, int cv2) {
  return std::clamp(cv1 + cv2, -2048, 2047);
}

int16_t LerpI(int16_t y0, int16_t y1, uint32_t frac_q16) {
  int32_t delta = y1 - y0;
  int32_t scaled = delta * static_cast<int32_t>(frac_q16) >> 16;
  return y0 + scaled;
}

int16_t ApplyDepth(int16_t cv, int32_t knob) {
  return (static_cast<int32_t>(cv) * knob) >> 12;
}

struct Frame {
  int16_t x;
  int16_t y;
};

class Buffer {
public:
  Buffer() {}

  int Length() {
    return idx_;
  }

  void Push(Frame f) {
    if (idx_ < buffer_.size()) {
      buffer_[idx_++] = f;
    }
  }

  void Reset() {
    idx_ = 0;
  }

  int Capacity() const {
    return kMaxFrames;
  }

  Frame At(int idx) {
    //return buffer_.at(idx);
    return buffer_[idx];
  }

private:
  std::array<Frame, kMaxFrames> buffer_;
  std::size_t idx_{};
};

struct TriggerOut {
  static constexpr int kWidthSamples = 240; // ~5 ms at 48kHz
  bool tick(bool trigger) {
    if (trigger) {
      frames_ = kWidthSamples;
      return true;
    }

    frames_--;

    if (frames_ < 0) {
      frames_ = 0;
    }

    return frames_ > 0 ? true : false;
  }

private:
  int frames_ = 0;
};

struct Schmitt {
  static constexpr int kBand = 200;
  bool update(int value) {
    if (value > kBand) {
      state_ = true;
    }

    if (value < -kBand) {
      state_ = false;
    }

    return state_;
  }

private:
  bool state_ = false;
};

class Phasor {
public:
  // set the loop size (in frames)
  void SetLength(uint32_t length) {
    length_ = length;
  }

  bool Advance(int32_t increment, bool loop) {
    if (length_ == 0) {
      return false;
    }

    bool report = false;

    if (loop) {
      int32_t span = length_ << 16;
      int32_t pos = static_cast<int32_t>(position_) + increment;
      report = (pos >= span || pos < 0);

      pos %= span;
      if (pos < 0) {
        pos += span;
      }

      position_ = pos;
    } else {
      int32_t last = (length_ - 1) << 16;
      int32_t pos = static_cast<int32_t>(position_) + increment;

      if (pos >= last) {
        pos = last;
        finished_ = true;
        report = true;
      } else if (pos < 0) {
        report = true;
        pos = 0;
      }

      position_ = pos;
    }

    return report;
  }

  bool Finished() const {
    return finished_;
  }

  uint32_t Index() const {
    return position_ >> 16;
  }

  // Q16
  uint32_t Frac() const {
    return position_ & 0xFFFF;
  }

  void Trigger() {
    position_ = 0;
    finished_ = false;
  }

private:
  uint32_t position_{};
  uint32_t length_{};
  bool finished_{};
};
} // namespace pantograph
