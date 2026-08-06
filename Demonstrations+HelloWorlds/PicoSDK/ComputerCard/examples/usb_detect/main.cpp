#include "ComputerCard.h"


class USBDetect : public ComputerCard
{
public:

	virtual void ProcessSample()
	{

		// Top left LED: downstream facing port (supplying 5V power)
		LedOn(0, USBPowerState() == DFP);

		// Top right LED: upstream facing port (disconnected, or connected to a device that supplies power)
		LedOn(1, USBPowerState() == UFP);

		// Bottom right LED: This version of the Computer does not support detection of USB power state
		LedOn(5, USBPowerState() == Unsupported);

	}
};


int main()
{
	set_sys_clock_khz(144000, true);

	USBDetect usbd;
	usbd.Run();
}

  
