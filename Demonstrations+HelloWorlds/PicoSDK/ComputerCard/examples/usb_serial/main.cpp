#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h" // for sleep_ms and printf
#include <cstdio>

/*

Output of data over a USB serial port, for e.g. debugging

This requires multicore processing, as USB is too slow to execute in ProcessSample.
Furthermore, because the RPi Pico SDK implementation of sending stdout to a USB
serial port runs code on core 0, we must run the ComputerCard ProcessSample loop on core 1.

To use, connect USB to a computer and run a serial terminal at 115200 baud
 */


// Variables for communication between the two cores
volatile uint32_t v1, v2;


class USBSerial : public ComputerCard
{
	
public:
	// 48kHz audio processing function
	virtual void ProcessSample()
	{
		// Copy the main knob and CV input 1 into v1 and v2, for transmission to printf
		v1 = KnobVal(Main);
		v2 = CVIn1();
	}
};

// Core 1 is used to launch the ComputerCard progrma
void core1()
{
	USBSerial usbs;
	usbs.Run();
}

int main()
{
	set_sys_clock_khz(144000, true);

	// Sleep commands not essential, but give some serial terminal programs
	// time to notice the new virtual COM port / TTY, and connect to it.
    sleep_ms(500);
	
	// Turn on USB serial. As far as I can tell, the RP2040 stdio USB serial port
	// must be initialised, and have code running on, core 0. This means that we
	// have to run ComputerCard on core 1.
	stdio_init_all();
	
	sleep_ms(500);

	// Launch the other core. Does not block
	multicore_launch_core1(core1);
	
	// Continue on core 0 with USB serial activity,
	// displaying CV input values every ~10ms
	while (1)
	{
		printf("%ld\t%ld\n", v1, v2);
		sleep_ms(10);
	}
}

  
