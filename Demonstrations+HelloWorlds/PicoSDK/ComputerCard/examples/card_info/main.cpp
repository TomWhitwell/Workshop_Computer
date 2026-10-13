// Display properties of the program card itself on the LEDs.
//
// The top four LEDs show the low eight bits of the card's unique ID, as four
// 2-bit brightness values, reading left to right, top to bottom:
//
//    bits 7-6   bits 5-4
//    bits 3-2   bits 1-0
//
// Two bits per LED gives four brightness levels - off, dim, medium, full -
// which are easy enough to tell apart by eye. Every card should light a
// different pattern, and the pattern for a given card never changes.
//
// The bottom two LEDs show the size of the flash chip on the card:
//
//    left  - 2MB or smaller
//    right - larger than 2MB
//
// Note that this is the size of the physical chip, not the amount of it that
// this firmware can write to, which is the compile-time PICO_FLASH_SIZE_BYTES.

#include "ComputerCard.h"


class CardInfo : public ComputerCard
{
public:

	CardInfo()
	{
		// Neither of these can change while the card is running, so the LEDs
		// are set once, here, rather than in ProcessSample.
		// LedBrightness squares its argument before driving the PWM, so
		// brightness levels evenly spaced over 0-4095 look evenly spaced.
		uint64_t id = UniqueCardID();
		for (int i = 3; i >= 0; i--)
		{
			LedBrightness(i, (id & 0x3) * (4095 / 3));
			id >>= 2;
		}

		LedOn(4, FlashSizeBytes() <= 2 * 1024 * 1024);
		LedOn(5, FlashSizeBytes() > 2 * 1024 * 1024);
	}

	virtual void ProcessSample()
	{
		// Nothing to do per sample: the LEDs were set in the constructor.
	}
};


int main()
{
	CardInfo ci;
	ci.Run();
}
