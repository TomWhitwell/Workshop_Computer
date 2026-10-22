#include "ComputerCard.h"

extern "C" {
#include "ii_bus.h"
}

/// I2C over UART: the Computer on an ii (I2C) bus, as a follower and, when
/// Pulse In 1 fires, briefly as a leader (as crow does).
///
/// Connect the UART pads (TX = SDA, RX = SCL) and ground to the ii bus,
/// whose pull-ups must go to 3.3V only. ii_bus.c can be reused by any card.
///
/// ii values are 16-bit, big-endian. Volts are 16383 = 10V (Teletype's V 10).
///
/// Follower, address 0x48: command 1 with a value sets CV out 1.
///   Teletype:  IIA 72  then  IIS1 1 V 3
///   crow:      ii.raw(0x48, string.char(1, 0x13, 0x33))
/// A leader reading after command 2 gets the main knob (0-4095).
///   Teletype:  IIA 72  then  X IIQ 2
///   crow:      ii.raw(0x48, string.char(2), 2)  -- the answer goes to ii.event_raw
/// Leader: each rising edge at Pulse In 1 sets crow's output 1 to the main
/// knob (0-5V), with crow's ii "volts" command. Crow needs no script.
///
/// In real cards, you likely don't want to run ii on the same core as audio because:
/// - The follower's I2C interrupt shares core 0 with audio. It is short, but
///   a card with a heavy ProcessSample may prefer ii on core 1 (see second_core).
/// - If a transfer times out (a stuck bus or faulty device), ii_bus_poll stalls
///   for about 1ms to recover the bus, so audio drops out briefly.
/// - A transfer takes about 0.5ms at 100kHz. ii_bus_start refuses a new one
///   while one is running, so pulses faster than about 2kHz are skipped.

static constexpr uint8_t kAddress = 0x48;      // our follower address
static constexpr uint8_t kCrowAddress = 0x01;

static volatile int32_t knob = 0;  // for Request, which isn't a member

// A leader is reading from us; `msg` is the command it wrote first.
// Called from the I2C interrupt, so answer quickly.
static int Request(const uint8_t* msg, uint8_t len, uint8_t* reply)
{
	if (len == 1 && msg[0] == 2)
	{
		reply[0] = knob >> 8;
		reply[1] = knob;
		return 2;  // bytes in reply
	}
	return 0;
}

class I2COverUART : public ComputerCard
{
	uint8_t msg[II_BUS_MAX_BYTES];

public:
	// ProcessSample is called for every sample, at 48kHz
	virtual void ProcessSample()
	{
		knob = KnobVal(Main);

		// Follower: a leader wrote to us
		if (ii_bus_receive(msg) == 3 && msg[0] == 1)
		{
			int16_t volts = (msg[1] << 8) | msg[2];
			CVOut1Millivolts(volts * 10000 / 16383);
		}

		// Leader: ii.crow.volts(1, knob)
		if (PulseIn1RisingEdge())
		{
			int16_t volts = knob * 2;  // 0-4095 -> 0-5V
			uint8_t cmd[4] = {1, 1, uint8_t(volts >> 8), uint8_t(volts)};  // volts, output 1
			ii_bus_start(kCrowAddress, cmd, 4, 0);
		}

		// Runs a transfer we started; when it ends, we're back to following
		ii_bus_poll(nullptr);
	}
};


int main()
{
	I2COverUART card;

	// Move I2C0 to the UART pads, and join the bus as a follower
	ii_bus_config_t bus = { .i2c = card.EnableI2COverUART(), .sda_pin = UART_I2C_SDA, .scl_pin = UART_I2C_SCL };
	ii_bus_init(&bus);
	ii_bus_follow(kAddress, Request);

	card.Run();
}
