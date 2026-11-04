#pragma once
#include <cstdint>

namespace tem
{

// Audio storage at the ADC's own depth: 12 bits a sample, two samples in
// three bytes.
//
//   byte 0      byte 1           byte 2
//   a7..a0      b3..b0 a11..a8   b11..b4
//
// Samples are signed 12-bit, -2048..2047, exactly what ComputerCard reads
// from the audio inputs, so packing loses nothing. It is what lets two
// loops of 40,256 samples (59KB each) fit in the RP2040's 264KB beside the
// reverb: as int16 they would need a third more.
//
// A read or write touches two bytes and costs a few more instructions than
// an int16 array. The loopers do one write and a few reads per clock tick.
template <int32_t N>
class Packed12
{
	static_assert(N % 2 == 0, "pairs of samples share three bytes");

public:
	static constexpr int32_t kBytes = N / 2 * 3;

	__attribute__((always_inline)) int32_t Read(int32_t i) const
	{
		const uint8_t *p = &b_[(i >> 1) * 3];
		int32_t v = (i & 1) ? ((p[1] >> 4) | (p[2] << 4)) : (p[0] | ((p[1] & 0x0F) << 8));
		// Sign-extend from 12 bits: 0x800..0xFFF are the negatives.
		return v - ((v & 0x800) << 1);
	}

	// `v` must already be within -2048..2047.
	__attribute__((always_inline)) void Write(int32_t i, int32_t v)
	{
		uint32_t u = (uint32_t)v & 0xFFF;
		uint8_t *p = &b_[(i >> 1) * 3];
		if (i & 1)
		{
			p[1] = (uint8_t)((p[1] & 0x0F) | (u << 4));
			p[2] = (uint8_t)(u >> 4);
		}
		else
		{
			p[0] = (uint8_t)u;
			p[1] = (uint8_t)((p[1] & 0xF0) | (u >> 8));
		}
	}

private:
	uint8_t b_[kBytes] = {};
};

} // namespace tem
