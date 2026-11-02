#define main card_main
#include "card.cpp"
#undef main
#include <cstdio>
// Drive the fake SysTick like the real one (counting down at 200 MHz), with a long
// start-up delay before the first interrupt, then one genuinely late interrupt at 5 s.
int main(){
  static DawnTest c; g_card=&c; c.sw=ComputerCard::Middle;
  uint32_t t = 0xFFFFFF - 200000;          // 1 ms of start-up before the first audio interrupt
  unsigned lateAtStart = 0;
  for (int i = 0; i < 48000 * 8; i++) {
    if (i == 48000 * 5) t = (t - 8000) & 0xFFFFFF;   // a real 40 us hold-up at 5 s
    systick_hw->cvr = t;
    if ((i & 255) == 0) PlanService();
    c.ProcessSample();
    t = (t - CYCLES_PER_SAMPLE) & 0xFFFFFF;
    if (i == 48000 * 2) lateAtStart = g_late;
  }
  printf("late interrupts counted in the first 2 s (start-up only): %u   (should be 0)\n", lateAtStart);
  printf("late interrupts counted in total, with one real hold-up at 5 s: %u   (should be 1)\n", (unsigned)g_late);
}
