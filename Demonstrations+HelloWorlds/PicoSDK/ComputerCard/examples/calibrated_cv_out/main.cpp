#include "ComputerCard.h"

class CalibratedCVOut : public ComputerCard
{
public:

	virtual void ProcessSample()
	{

   		int32_t cv1mv, cv2mv;

		// Set voltages corresponding to other switch positions
		switch(SwitchVal())
		{
		case Up:
			cv1mv = 5000;
			cv2mv = 1000;
			break;
			
		case Middle:
			cv1mv = -5000;
			cv2mv = -1000;
			break;
			
		default: // switch is down
			cv1mv = 0;
			cv2mv = 0;
			break;
		}

		// Output desired voltages on CV out
		bool cv1limited = CVOut1Millivolts(cv1mv);
		bool cv2limited = CVOut2Millivolts(cv2mv);

		// Light top left/right LED, if requested voltage
		// on corresponding channel is not possible
		LedOn(0, cv1limited);
		LedOn(1, cv2limited);

		// Light bottom right LED if CV outputs have not been calibrated
		LedOn(5, !CVOutsCalibrated());
	}
};


int main()
{
	set_sys_clock_khz(144000, true);

	CalibratedCVOut ccvo;

	ccvo.Run();
}

  
