// QiGong Timer: a Workshop Computer program card (AI-assisted: Claude / Anthropic).
#include "pico/stdlib.h"
#ifdef CARD_BENCH
// BENCH BUILD ONLY (cmake -DCARD_BENCH=ON): lets scripts/bench_cycles.py reach ComputerCard's inputs and the engine
// from an emulator.  Never used for the card firmware.
#define private public
#define protected public
#endif
#include "qi_card.h"
#ifdef CARD_BENCH
#include <cstddef>
#include <new>
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
extern "C" {
// offsets of: knobs, pulse, last_pulse, cv, adcInL, adcInR, connected, switchVal, lastSwitchVal, lastControl_, sizeof(card)
extern const uint32_t bench_offsets[11];
__attribute__((used)) const uint32_t bench_offsets[11] = {
	offsetof(QiTimerCard, knobs), offsetof(QiTimerCard, pulse), offsetof(QiTimerCard, last_pulse), offsetof(QiTimerCard, cv),
	offsetof(QiTimerCard, adcInL), offsetof(QiTimerCard, adcInR), offsetof(QiTimerCard, connected),
	offsetof(QiTimerCard, switchVal), offsetof(QiTimerCard, lastSwitchVal), offsetof(QiTimerCard, lastControl_), sizeof(QiTimerCard)};
// Build a working engine inside a fake card object (the real constructor would touch hardware).
__attribute__((used, noinline)) void bench_init(void* card)
{
	QiTimerCard* c = (QiTimerCard*)card;
	new (&c->engine_) qi::Engine();
	c->engine_.Init(1, false);                // the emulator has no boot ROM float routines: the script loads the tables
	c->lastControl_ = 0;
}
// Core 1's work (measured separately: it must keep up with one Control() per 32 samples).
__attribute__((used, noinline)) int bench_core1(void* card) { return ((QiTimerCard*)card)->Core1Step() ? 1 : 0; }
}
#endif
#ifdef CARD_192MHZ
#include "hardware/vreg.h"
#endif

int main()
{
#ifdef CARD_BENCH
	volatile bool never = false;              // keep the bench symbols; the emulator calls them directly
	if (never) { bench_init((void*)bench_offsets); bench_core1(nullptr); QiTimerCard card; card.Setup(); card.Run(); }   // (also keeps bench_offsets)
	while (true) tight_loop_contents();
#endif
#ifdef CARD_192MHZ
	// OPTION only (-DCARD_192MHZ=ON): 192 MHz at 1.15 V.  Not the default: on the owner's unit a 192 MHz build (Robopera)
	// gave a continuous whine on CV Out 1.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	sleep_ms(2);
	set_sys_clock_khz(192000, true);
#else
	// DEFAULT: 144 MHz (the AI directive's default; fewer ADC tonal artefacts; proven on the owner's unit).  With the UI on
	// core 1 the worst sample uses 81% of the budget here (scripts/bench_cycles.py).
	set_sys_clock_khz(144000, true);
#endif
	QiTimerCard card;                      // a plain local (a function-level `static` pulls ~30 KB of C++ runtime into RAM);
	                                       // large buffers live in global arrays instead
	card.Setup();
	card.Run();                            // never returns: the 48 kHz audio interrupt takes over
}
