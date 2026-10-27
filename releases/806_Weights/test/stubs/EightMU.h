// Stand-in for EightMU.h: a pretend 8mu the test can move
#pragma once
#include <stdint.h>

class EightMU
{
public:
	static constexpr int numFaders = 8, numButtons = 4, numLeds = 8;
	bool connected = false;
	int32_t fader[8] = {};    // 0-4064, as the real class gives them
	bool button[4] = {};
	int32_t led[8] = {};

	bool Connected() const {return connected;}
	int32_t Fader(int i) const {return fader[i];}
	bool Button(int i) const {return button[i];}
	void SetLed(int i, int32_t b) {led[i] = b;}
	void Poll() {}
};
