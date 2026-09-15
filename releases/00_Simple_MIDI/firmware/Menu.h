#pragma once
#include "ComputerCard.h"

class Menu : public ComputerCard
{
	int sampleCount = 0;
	bool cal = false;
	
	void __not_in_flash_func(ProcessSample)() override final
	{
		sampleCount++;
		if (sampleCount >= 720) // 15 ms at 48 kHz
		{
			cal = (SwitchVal() == Switch::Down);
			Abort();
		}
	}

public:
	// Run() on core 0: fires ProcessSample() via DMA IRQ until Abort() exits it.
	bool DetectMode()
	{
		Run();
		return cal;
	}
};
