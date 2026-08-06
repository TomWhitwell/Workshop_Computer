#include "ComputerCard.h"
#include "pantograph.h"

namespace pantograph {
class Pantograph : public ComputerCard {
public:
  virtual void ProcessSample() override {
    auto switchChanged = SwitchChanged();
    auto switchVal = SwitchVal();
    auto knobX = KnobVal(Knob::X);
    auto knobY = KnobVal(Knob::Y);
    auto cvIn1 = CVIn1();
    auto cvIn2 = CVIn2();

    switch (switchVal) {
    case Up: {
      return Live(knobX, knobY, cvIn1, cvIn2);
    }
    case Down: {
      if (switchChanged) {
        // Record
        buffer_.Reset();
      }

      tick_++;

      if (tick_ == 192) {
        buffer_.Push({static_cast<int16_t>(KnobToCV(knobX)), static_cast<int16_t>(KnobToCV(knobY))});
        tick_ = 0;
      }

      uint32_t count = (buffer_.Length() * 6) / buffer_.Capacity();
      for (uint32_t k = 0; k < 6; ++k) {
        LedOn(k, k < count);
      }

      return MonitorOutputs(knobX, knobY, cvIn1, cvIn2);
    }
    case Middle: {
      if (switchChanged) {
        phasor_.SetLength(buffer_.Length());
      }

      auto length = buffer_.Length();
      if (length == 0) {
        return Live(knobX, knobY, cvIn1, cvIn2);
      }

      auto pulseIn = Connected(Pulse1);

      if (pulseIn && PulseIn1RisingEdge()) {
        phasor_.Trigger();
      }

      bool wrapped = phasor_.Advance(KnobToSpeed(KnobVal(Knob::Main)) / 192, !pulseIn);
      bool finished = phasor_.Finished();
      bool isEdge = finished && !lastFinished_;
      lastFinished_ = finished;

      PulseOut1(trigger_.tick(wrapped || isEdge));

      auto i = phasor_.Index();
      auto frac = phasor_.Frac();

      uint32_t dot = (i * 6) / length;
      for (uint32_t k = 0; k < 6; ++k) {
        LedOn(k, k == dot);
      }

      int xVal = ApplyDepth(
          LerpI(buffer_.At(i).x, buffer_.At((i + 1) % length).x, frac), knobX);
      CVOut1(SumClamp(xVal, cvIn1));
      PulseOut2(schmitt_.update(xVal));

      CVOut2(SumClamp(
          ApplyDepth(
              LerpI(buffer_.At(i).y, buffer_.At((i + 1) % length).y, frac), knobY),
          cvIn2));

      break;
    }
    default: {
      return;
    }
    }
  }

private:
  void MonitorOutputs(int knobX, int knobY, int cvIn1, int cvIn2) {
    auto out1 = SumClamp(KnobToCV(knobX), cvIn1);
    auto out2 = SumClamp(KnobToCV(knobY), cvIn2);

    CVOut1(out1);
    CVOut2(out2);

    lastOut1_ = out1;
    lastOut2_ = out2;
  }

  void Live(int knobX, int knobY, int cvIn1, int cvIn2) {
    MonitorOutputs(knobX, knobY, cvIn1, cvIn2);

    uint16_t bx = std::min(std::abs(lastOut1_) * 2, 4095);
    uint16_t by = std::min(std::abs(lastOut2_) * 2, 4095);

    LedBrightness(0, bx);
    LedBrightness(2, bx);
    LedBrightness(4, bx);
    LedBrightness(1, by);
    LedBrightness(3, by);
    LedBrightness(5, by);
  }

  Buffer buffer_;
  Phasor phasor_;
  TriggerOut trigger_;
  Schmitt schmitt_;
  uint32_t tick_ = 0;
  int lastOut1_ = 0;
  int lastOut2_ = 0;
  bool lastFinished_ = false;
};
} // namespace pantograph

int main() {
  set_sys_clock_khz(144000, true);
  static pantograph::Pantograph card;
  card.EnableNormalisationProbe();
  card.Run();
}
