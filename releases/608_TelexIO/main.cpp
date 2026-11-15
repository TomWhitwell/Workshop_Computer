#include "ComputerCard.h"
#include "telexio.h"
#include "lut.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

// FIXMEs
// use of random in the oscillator
// move profiling output to i2c-replies instead of USB-serial
// move any TO operands that take too long to oracle
// consider LUT for v/oct

TelexIO telex; // don't build it on the stack

int main()
{
	set_sys_clock_khz(192000, true);

    // TelexI
    telex.InitTelexI(); 

    // TelexO
    reciprocal_init();
    init_clz_lut();
    telex.InitTelexO();

    // Shared
    multicore_launch_core1(TelexIO::core1);

    telex.Run();
}


  
