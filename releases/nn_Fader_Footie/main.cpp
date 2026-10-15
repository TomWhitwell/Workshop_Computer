// Fader Footie
//
// Table football for two players and a Music Thing 8mu, played in a web page
// (web/index.html), with the game turned into voltages by the Workshop
// Computer.  The 8mu sits between the players, plugged into the computer; the
// four faders nearest each player move their four rods on screen, and the two
// top buttons nearest them are a hard and a soft spin shot.  The physics runs
// in the page.  This card is the page's USB MIDI device: it turns what happens
// on the table into control voltages, gates and triggers.
//
// Outputs
//   CV Out 1    The number (1-11) of the last team 1 player to touch the
//               ball, as a voltage; the page chooses how (scale, semitones,
//               or spread over 0-5V).  Calibrated, so it can play pitches
//   CV Out 2    The same for team 2
//   Audio Out 1 How far the ball is from the goal team 1 attacks: about 0V
//               in the goal mouth to about +5V at the far corners.  Switch
//               up turns it over (nearness: +5V at the goal)
//   Audio Out 2 The same for team 2
//   Pulse Out 1 A gate each time team 1 touches the ball, its length set by
//               X; a burst of fast triggers when team 1 scores
//   Pulse Out 2 The same for team 2
//
// Panel
//   Main        Smoothing of the audio outs, from about 1ms to about 1s
//   X           Touch gate length, 2ms to 1s
//   Y           Goal burst rate, 4Hz to 60Hz (the number of triggers is
//               set on the page, 12 by default)
//   Switch up   Audio outs show nearness instead of distance
//   Switch mid  Audio outs show distance
//   Switch down (momentary) Tap to serve the ball again; hold for a second
//               to start a new match
//
// Inputs
//   Pulse In 1  Serve the ball again
//   Pulse In 2  Start a new match
//
// LEDs
//   0 / 1       Audio Out 1 / 2
//   2 / 3       CV Out 1 / 2
//   4 / 5       Pulse Out 1 / 2
//   With no page linked, LEDs 4 and 5 blink slowly in turn.
//
// USB: the card is always a USB MIDI device called "Fader Footie" (protocol in
// sysex.h).  Plug the computer into the Computer's USB socket; the 8mu goes
// into the computer, not the card.

// First, so TinyUSB is configured before anything includes it
#include "tusb_config.h"
#include "ComputerCard.h"
#include "pico/multicore.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "sysex.h"
#include <math.h>

class FaderFootie;
static FaderFootie *gCard = nullptr;

class FaderFootie : public ComputerCard
{
public:
	static constexpr int kSampleRate = 48000;
	static constexpr int kControlEvery = 64;       // samples between knob reads
	static constexpr int kGapSamples = 48;         // 1ms low before a retrigger
	static constexpr int kHoldSamples = kSampleRate; // switch hold for a new match

	FaderFootie()
	{
		for (int i = 0; i < 2; i++)
		{
			cvMv[i] = 0;
			dist[i] = 4095;
			smoothed[i] = int64_t(4095) << 12;
			touchReq[i] = touchSeen[i] = 0;
			goalReq[i] = goalSeen[i] = 0;
		}
		burstCount = 12;
		UpdateKnobs();

		gCard = this;
		multicore_launch_core1(Core1);
	}

	virtual void ProcessSample()
	{
		if (++controlCount >= kControlEvery)
		{
			controlCount = 0;
			HandleSwitch();
		}

		if (PulseIn1RisingEdge()) eventReq[sysex::EvPulseServe]++;
		if (PulseIn2RisingEdge()) eventReq[sysex::EvPulseNewMatch]++;

		bool up = SwitchVal() == Switch::Up;
		for (int i = 0; i < 2; i++)
		{
			// Events from the page: counters, so none is lost between cores
			uint32_t t = touchReq[i];
			if (t != touchSeen[i])
			{
				touchSeen[i] = t;
				StartGate(i);
			}
			uint32_t g = goalReq[i];
			if (g != goalSeen[i])
			{
				goalSeen[i] = g;
				StartBurst(i);
			}
			bool p = RunPulse(i);
			PulseOut(i, p);
			pulseOn[i] = p;

			// Distance, smoothed: the page updates it about 60 times a second
			int32_t target = dist[i];
			smoothed[i] += ((int64_t(target) << 12) - smoothed[i]) * smoothCoef >> 16;
			int32_t d = int32_t(smoothed[i] >> 12);
			if (up) d = 4095 - d;
			int32_t a = (d * 1704) / 4095;      // 0 to about +5V (as CVSeq scales its audio outs)
			AudioOut(i, int16_t(a));
			audioLevel[i] = d;

			CVOutMillivolts(i, cvMv[i]);
		}

		if (controlCount == 0) UpdateLeds();
	}

	//------------------------------------------------------------------------
	// Pulses
	//------------------------------------------------------------------------

	struct Pulse
	{
		int32_t gateLeft = 0;    // samples the touch gate stays high
		int32_t gapLeft = 0;     // samples forced low before what comes next
		int32_t burstLeft = 0;   // triggers still to come in a goal burst
		int32_t phase = 0;       // samples into the burst's current period
		int32_t period = 1;
	};
	Pulse pulses[2];
	bool pulseOn[2] = {false, false};

	void StartGate(int i)
	{
		Pulse &p = pulses[i];
		if (p.burstLeft > 0) return;      // a goal burst wins
		if (pulseOn[i]) p.gapLeft = kGapSamples;
		p.gateLeft = gateSamples;
	}

	void StartBurst(int i)
	{
		Pulse &p = pulses[i];
		if (pulseOn[i]) p.gapLeft = kGapSamples;
		p.gateLeft = 0;
		p.burstLeft = burstCount;
		p.phase = 0;
		p.period = burstPeriod;
	}

	bool RunPulse(int i)
	{
		Pulse &p = pulses[i];
		if (p.gapLeft > 0)
		{
			p.gapLeft--;
			return false;
		}
		if (p.burstLeft > 0)
		{
			bool on = p.phase < (p.period >> 1);
			if (++p.phase >= p.period)
			{
				p.phase = 0;
				p.period = burstPeriod;
				p.burstLeft--;
			}
			return on;
		}
		if (p.gateLeft > 0)
		{
			p.gateLeft--;
			return true;
		}
		return false;
	}

	//------------------------------------------------------------------------
	// Panel
	//------------------------------------------------------------------------

	// Knob settings, worked out on core1 (soft float is too slow for the
	// audio interrupt)
	void UpdateKnobs()
	{
		float x = float(KnobVal(Knob::X)) / 4095.0f;
		float y = float(KnobVal(Knob::Y)) / 4095.0f;
		float m = float(KnobVal(Knob::Main)) / 4095.0f;

		float gateMs = 2.0f * powf(500.0f, x);            // 2ms to 1s
		float rateHz = 4.0f * powf(15.0f, y);             // 4Hz to 60Hz
		float tau = 0.001f * powf(1000.0f, m);            // 1ms to 1s
		gateSamples = int32_t(gateMs * float(kSampleRate) / 1000.0f);
		burstPeriod = int32_t(float(kSampleRate) / rateHz);
		if (burstPeriod < 2 * kGapSamples) burstPeriod = 2 * kGapSamples;
		smoothCoef = int32_t(65536.0f * (1.0f - expf(-1.0f / (tau * float(kSampleRate)))));
		if (smoothCoef < 1) smoothCoef = 1;

		stGateMs = int32_t(gateMs + 0.5f);
		stRateHz = int32_t(rateHz + 0.5f);
		stSmooth = KnobVal(Knob::Main) >> 5;
		stSwitch = int32_t(SwitchVal());
	}

	void HandleSwitch()
	{
		bool down = SwitchVal() == Switch::Down;
		if (down)
		{
			if (!switchHeld)
			{
				switchHeld = true;
				holdCount = 0;
				holdFired = false;
			}
			holdCount += kControlEvery;
			if (!holdFired && holdCount >= kHoldSamples)
			{
				holdFired = true;
				eventReq[sysex::EvNewMatch]++;
			}
		}
		else if (switchHeld)
		{
			switchHeld = false;
			if (!holdFired) eventReq[sysex::EvServe]++;
		}
	}

	void UpdateLeds()
	{
		if (!Linked())
		{
			uint32_t t = time_us_32() >> 19;   // about half a second
			for (int i = 0; i < 4; i++) LedOff(i);
			LedBrightness(4, (t & 1) ? 1200 : 0);
			LedBrightness(5, (t & 1) ? 0 : 1200);
			return;
		}
		for (int i = 0; i < 2; i++)
		{
			LedBrightness(i, uint16_t(audioLevel[i]));
			int32_t cv = cvMv[i];
			if (cv < 0) cv = 0;
			if (cv > 5000) cv = 5000;
			LedBrightness(2 + i, uint16_t(cv * 4095 / 5000));
			LedOn(4 + i, pulseOn[i]);
		}
	}

	//------------------------------------------------------------------------
	// USB, on core1
	//------------------------------------------------------------------------

	bool Linked() const
	{
		return pinged && (time_us_32() - lastPingUs) < 3000000;
	}

	static void Core1()
	{
		board_init();
		tud_init(0);
		sysex::Parser parser;
		uint32_t lastStatusUs = 0, lastKnobsUs = 0;
		uint32_t eventSent[5] = {0, 0, 0, 0, 0};
		while (true)
		{
			tud_task();
			uint8_t buf[64];
			while (tud_midi_available())
			{
				uint32_t n = tud_midi_stream_read(buf, sizeof(buf));
				if (n == 0) break;
				for (uint32_t i = 0; i < n; i++)
				{
					if (parser.Feed(buf[i]))
					{
						gCard->OnSysEx(parser.cmd, parser.payload, parser.length);
					}
				}
			}

			uint32_t now = time_us_32();
			if (now - lastKnobsUs >= 5000)
			{
				lastKnobsUs = now;
				gCard->UpdateKnobs();
			}

			if (!gCard->Linked()) continue;

			for (int e = 1; e < 5; e++)
			{
				uint32_t r = gCard->eventReq[e];
				if (r != eventSent[e])
				{
					eventSent[e] = r;
					uint8_t msg[sysex::kEventLen];
					int n = sysex::Header(msg, sysex::Event);
					msg[n++] = uint8_t(e);
					msg[n++] = 0xF7;
					Write(msg, n);
				}
			}

			if (now - lastStatusUs >= 100000)
			{
				lastStatusUs = now;
				uint8_t msg[sysex::kStatusLen];
				int n = sysex::Header(msg, sysex::Status);
				msg[n++] = uint8_t(gCard->stSwitch & 3);
				n += sysex::Put14(msg + n, gCard->stGateMs);
				msg[n++] = uint8_t(gCard->stRateHz & 0x7F);
				msg[n++] = uint8_t(gCard->stSmooth & 0x7F);
				msg[n++] = 0xF7;
				Write(msg, n);
			}
		}
	}

	// Send a whole message, waiting briefly for room if need be.  If the
	// computer isn't reading, the rest is dropped; the page's parser
	// resynchronises on the next F0.
	static void Write(const uint8_t *msg, int len)
	{
		uint32_t start = time_us_32();
		int sent = 0;
		while (sent < len && tud_mounted())
		{
			sent += int(tud_midi_stream_write(0, msg + sent, uint32_t(len - sent)));
			if (sent < len)
			{
				if (time_us_32() - start > 50000) return;
				tud_task();
			}
		}
	}

	void OnSysEx(uint8_t cmd, const uint8_t *p, int len)
	{
		switch (cmd)
		{
		case sysex::Ping:
			lastPingUs = time_us_32();
			pinged = true;
			break;
		case sysex::Outputs:
			if (len >= 8)
			{
				cvMv[0] = sysex::Get14(p) - 8192;
				cvMv[1] = sysex::Get14(p + 2) - 8192;
				int32_t d1 = sysex::Get14(p + 4), d2 = sysex::Get14(p + 6);
				dist[0] = d1 > 4095 ? 4095 : d1;
				dist[1] = d2 > 4095 ? 4095 : d2;
			}
			break;
		case sysex::Touch:
			if (len >= 1 && p[0] < 2) touchReq[p[0]]++;
			break;
		case sysex::Goal:
			if (len >= 1 && p[0] < 2) goalReq[p[0]]++;
			break;
		case sysex::Config:
			if (len >= 1 && p[0] >= 1 && p[0] <= 64) burstCount = p[0];
			break;
		default:
			break;
		}
	}

private:
	// Written on core1, read on core0
	volatile int32_t cvMv[2];
	volatile int32_t dist[2];
	volatile uint32_t touchReq[2];
	volatile uint32_t goalReq[2];
	volatile int32_t burstCount;
	volatile uint32_t lastPingUs = 0;
	volatile bool pinged = false;

	// Written on core1 (UpdateKnobs), read on core0 and in STATUS
	volatile int32_t smoothCoef = 65536;
	volatile int32_t gateSamples = 480;
	volatile int32_t burstPeriod = 2400;
	volatile int32_t stSwitch = 1, stGateMs = 0, stRateHz = 0, stSmooth = 0;

	// Written on core0, read on core1
	volatile uint32_t eventReq[5] = {0, 0, 0, 0, 0};

	// core0 only
	uint32_t touchSeen[2];
	uint32_t goalSeen[2];
	int64_t smoothed[2];
	int32_t audioLevel[2] = {0, 0};
	int controlCount = 0;
	bool switchHeld = false;
	bool holdFired = false;
	int32_t holdCount = 0;
};


int main()
{
	set_sys_clock_khz(200000, true);

	static FaderFootie card;
	card.Run();
}
