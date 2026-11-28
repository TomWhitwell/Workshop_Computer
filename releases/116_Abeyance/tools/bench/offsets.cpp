// Bench only: export ComputerCard field offsets for the emulator.
#include <cstddef>
#define COMPUTERCARD_NOIMPL
#define private public
#define protected public
#include "ComputerCard.h"
#undef private
#undef protected
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
extern "C" __attribute__((used)) const uint32_t bench_offsets[] = {
	offsetof(ComputerCard, knobs), offsetof(ComputerCard, pulse), offsetof(ComputerCard, last_pulse),
	offsetof(ComputerCard, cv), offsetof(ComputerCard, adcInL), offsetof(ComputerCard, adcInR),
	offsetof(ComputerCard, connected), offsetof(ComputerCard, switchVal), offsetof(ComputerCard, lastSwitchVal),
	offsetof(ComputerCard, dacOut), sizeof(ComputerCard)};
