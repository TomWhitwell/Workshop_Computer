#include "ComputerCard.h"
#include <cmath>
#include "oscillator.h"
#include "lookup_tables.h"

class WT : public ComputerCard
{
  uint32_t phase;
  uint32_t ph_rot;

  // Anti-aliasing filter state variables
  int32_t filter_L = 0;
  int32_t filter_R = 0;

  // One-pole lowpass coefficient for ~21kHz cutoff at 48kHz sample rate
  static constexpr int32_t FILTER_COEF = 57344; // 0.875 * 65536

  YinYang yinyang;
  PolyCube polycube;
  PolyCone polycone;
  PolyICO polyico;
  YinYangCalligraphy yinyang_c;
  RibbonWC ribbon;
  OutlineWC outline;

  enum BANK
  {
    BANK_FUNC,
    BANK_MESH,
    BANK_WT
  };

  // Pointer to current oscillator
  Oscillator *currentOsc;
  uint8_t currentBank = BANK_FUNC;
  uint8_t currentOscIndex = 0;

  // Array of oscillators for each bank
  Oscillator *bankFunc[1] = {&yinyang};
  Oscillator *bankMesh[3] = {&polycube, &polycone, &polyico};
  Oscillator *bankWT[3] = {&yinyang_c, &ribbon, &outline};

  Oscillator **banks[3] = {bankFunc, bankMesh, bankWT};
  uint8_t bankSizes[3] = {1, 3, 3};

  int32_t mod1_off = 0;
  int32_t mod2_off = 0;
  int32_t mod1_att = 2048;
  int32_t mod2_att = 2048;

  bool mod1guard = false;
  bool mod2guard = false;

  int32_t knobx_last = 0;
  int32_t knoby_last = 0;

public:
  WT()
  {
    phase = 0;
    currentOsc = bankFunc[0];
  }

  void CycleOscillator()
  {
    // Move to next oscillator
    currentOscIndex++;
    PulseOut(2, true);

    // Move to next bank
    if (currentOscIndex >= bankSizes[currentBank])
    {
      currentBank = (currentBank + 1) % 3; // Cycle through banks
      currentOscIndex = 0;
    }

    currentOsc = banks[currentBank][currentOscIndex];
  }

  static inline bool __not_in_flash_func(knobChanged)(int32_t prev, int32_t curr)
  {
    return (curr - prev > 4) || (prev - curr > 4);
  }

  virtual void ProcessSample()
  {
    // Send Pulse on PulseOut1 when changing oscillator
    if (SwitchChanged() && SwitchVal() == Down)
    {
      CycleOscillator();
      PulseOut1(true);
    }
    else
    {
      PulseOut1(false);
    }
    if (PulseIn1RisingEdge())
    {
      currentBank = (currentBank + 1) % 3;
      currentOscIndex = 0;
      currentOsc = banks[currentBank][currentOscIndex];
    }
    if (PulseIn2RisingEdge())
    {
      currentOscIndex = (currentOscIndex + 1) % bankSizes[currentBank];
      currentOsc = banks[currentBank][currentOscIndex];
    }

    // Set guards when switch changes to prevent parameter jumps
    if (SwitchChanged())
    {
      mod1guard = true;
      mod2guard = true;
    }

    // Read current knob positions
    int32_t knobx_curr = KnobVal(X);
    int32_t knoby_curr = KnobVal(Y);

    // Check for knob movement and release guards
    if (knobChanged(knobx_last, knobx_curr))
    {
      mod1guard = false;
      knobx_last = knobx_curr;
    }
    if (knobChanged(knoby_last, knoby_curr))
    {
      mod2guard = false;
      knoby_last = knoby_curr;
    }

    // Update parameters only when guards are released
    if (SwitchVal() == Up)
    {
      if (!mod1guard)
        mod1_att = knobx_curr;
      if (!mod2guard)
        mod2_att = knoby_curr;
    }
    else
    {
      if (!mod1guard)
        mod1_off = knobx_curr;
      if (!mod2guard)
        mod2_off = knoby_curr;
    }

    int32_t mod1 = mod1_off + (AudioIn1() * mod1_att >> 12);
    int32_t mod2 = mod2_off + (AudioIn2() * mod2_att >> 12);

    // oscillator phase increment
    int32_t freq = KnobVal(Main) + CVIn1();
    phase += FREQ_INC_LUT_EXP[freq > 4095 ? 4095 : (freq < 0 ? 0 : freq)];

    // prepare output
    int32_t out[2];

    // Call compute on the current oscillator using pointer
    currentOsc->compute(phase, mod1, mod2, out);

    // Apply anti-aliasing filter to both channels
    filter_L += ((out[0] - filter_L) * FILTER_COEF) >> 16;
    filter_R += ((out[1] - filter_R) * FILTER_COEF) >> 16;

    AudioOut1(filter_L);
    AudioOut2(filter_R);

    LedOn(0, currentBank * 2 == 0);
    LedOn(1, currentOscIndex * 2 + 1 == 1);
    LedOn(2, currentBank * 2 == 2);
    LedOn(3, currentOscIndex * 2 + 1 == 3);
    LedOn(4, currentBank * 2 == 4);
    LedOn(5, currentOscIndex * 2 + 1 == 5);

    // debug LEDs for mod guards
    // LedOn(4, mod1guard);
    // LedOn(5, mod2guard);
  }

protected:
};

int main()
{
  set_sys_clock_khz(192000, true);
  WT wt;
  wt.Run();
}
