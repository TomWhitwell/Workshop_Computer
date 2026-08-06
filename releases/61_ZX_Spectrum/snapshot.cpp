// snapshot.cpp — .z80 snapshot loader (v1/v2/v3, 48K & 128K).
//
// Parses a .z80 image, restores the Z80 registers into the superzazu core, sets
// the border, and (for 128K) restores the 0x7FFD paging latch and unpacks each
// 16KB RAM page into the right bank. Memory pages are RLE-compressed with the
// .z80 scheme: a run of >=5 equal bytes is stored as ED ED count value; a literal
// ED ED is escaped. (v1 compresses the whole 48K as one block ending ED ED 00 00.)
//
// Reference: World of Spectrum .z80 format documentation.

#include "spectrum.h"
#include "machine.h"
#include "z80.h"
#include <cstring>

namespace zx {

// Unpack .z80 RLE from src (srcLen bytes) into dst (up to dstMax bytes).
// Returns bytes written. Stops at dstMax.
static uint32_t Decompress(const uint8_t *src, uint32_t srcLen,
                           uint8_t *dst, uint32_t dstMax)
{
	uint32_t si = 0, di = 0;
	while (si < srcLen && di < dstMax)
	{
		// ED ED count value  -> run
		if (si + 3 < srcLen && src[si] == 0xED && src[si + 1] == 0xED)
		{
			uint8_t count = src[si + 2];
			uint8_t value = src[si + 3];
			si += 4;
			for (uint8_t k = 0; k < count && di < dstMax; k++)
				dst[di++] = value;
		}
		else
		{
			dst[di++] = src[si++];
		}
	}
	return di;
}

// Restore the flags byte (SZ5H3PNC) into the superzazu bitfields.
static void SetFlags(z80 *z, uint8_t f)
{
	z->sf = (f >> 7) & 1;
	z->zf = (f >> 6) & 1;
	z->yf = (f >> 5) & 1;
	z->hf = (f >> 4) & 1;
	z->xf = (f >> 3) & 1;
	z->pf = (f >> 2) & 1;
	z->nf = (f >> 1) & 1;
	z->cf = (f >> 0) & 1;
}

// Auto-detect .sna vs .z80 by the exact .sna file sizes, else treat as .z80.
bool LoadSnapshot(Machine &m, const uint8_t *d, uint32_t len)
{
	// .sna sizes: 48K = 49179; 128K = 131103 or 147499.
	if (len == 49179 || len == 131103 || len == 147499)
		return LoadSNA(m, d, len);
	return LoadZ80(m, d, len);
}

// Load a 48K .sna image (27-byte header + 48KB RAM). 128K .sna adds an extended
// tail (PC, 0x7FFD, and extra banks); we handle the common 48K case, and the
// 128K case if the file is long enough.
bool LoadSNA(Machine &m, const uint8_t *d, uint32_t len)
{
	if (len < 27 + 0xC000) return false; // need header + 48KB
	Spectrum *spec = m.Spec();
	z80 *z = m.Cpu();

	// 27-byte header
	z->i = d[0];
	z->l_ = d[1];  z->h_ = d[2];
	z->e_ = d[3];  z->d_ = d[4];
	z->c_ = d[5];  z->b_ = d[6];
	z->f_ = d[7];  z->a_ = d[8];
	z->l = d[9];   z->h = d[10];
	z->e = d[11];  z->d = d[12];
	z->c = d[13];  z->b = d[14];
	z->iy = d[15] | (d[16] << 8);
	z->ix = d[17] | (d[18] << 8);
	z->iff2 = (d[19] & 0x04) ? 1 : 0;
	z->iff1 = z->iff2;               // .sna stores IFF2 in bit 2; mirror to IFF1
	z->r = d[20];
	SetFlags(z, d[21]);              // F
	z->a = d[22];
	z->sp = d[23] | (d[24] << 8);
	z->interrupt_mode = d[25] & 3;
	spec->xc.border = d[26] & 7;

	// 48KB RAM at 0x4000..0xFFFF. Use the 48K page layout (ROM1 = 48 BASIC).
	spec->mem.SetPaging(0x10);
	const uint8_t *ram = d + 27;
	for (uint32_t i = 0; i < 0xC000; i++)
		spec->mem.Write(0x4000 + i, ram[i]);

	if (len >= 27 + 0xC000 + 4)
	{
		// 128K .sna tail: PC (2), 0x7FFD (1), TR-DOS flag (1), then extra 16KB banks.
		spec->SetMode(MODE_128K);
		const uint8_t *tail = d + 27 + 0xC000;
		z->pc = tail[0] | (tail[1] << 8);
		uint8_t port = tail[2];
		spec->mem.SetPaging(port);
		// The remaining banks (those not already the currently-paged ones) follow.
		// For simplicity we load them in ascending page order into unused banks.
		const uint8_t *banks = tail + 4;
		uint32_t remaining = (d + len) - banks;
		uint8_t pagedIn = port & 0x07;
		int bi = 0;
		for (int page = 0; page < 8 && remaining >= 0x4000; page++)
		{
			// Skip pages already present in the 48K-style dump (5,2, and pagedIn).
			if (page == 5 || page == 2 || page == pagedIn) continue;
			memcpy(spec->mem.ram[page], banks + bi * 0x4000, 0x4000);
			bi++;
			remaining -= 0x4000;
		}
	}
	else
	{
		// 48K .sna: PC is on the stack — pop it.
		spec->SetMode(MODE_48K);
		uint16_t sp = z->sp;
		uint8_t lo = spec->mem.Read(sp);
		uint8_t hi = spec->mem.Read(sp + 1);
		z->pc = lo | (hi << 8);
		z->sp = sp + 2;
	}
	return true;
}

// Load a .z80 image into the machine. Returns true on success.
bool LoadZ80(Machine &m, const uint8_t *d, uint32_t len)
{
	if (len < 30) return false;
	Spectrum *spec = m.Spec();
	z80 *z = m.Cpu();

	// --- v1 base header (bytes 0..29) ---
	z->a = d[0];
	SetFlags(z, d[1]);
	z->c = d[2];  z->b = d[3];
	z->l = d[4];  z->h = d[5];
	uint16_t pc = d[6] | (d[7] << 8);
	z->sp = d[8] | (d[9] << 8);
	z->i = d[10];
	z->r = (d[11] & 0x7F) | ((d[12] & 1) << 7);
	uint8_t byte12 = (d[12] == 0xFF) ? 1 : d[12];
	uint8_t border = (byte12 >> 1) & 7;
	bool v1compressed = (byte12 >> 5) & 1;
	z->e = d[13]; z->d = d[14];
	z->c_ = d[15]; z->b_ = d[16];
	z->e_ = d[17]; z->d_ = d[18];
	z->l_ = d[19]; z->h_ = d[20];
	z->a_ = d[21]; z->f_ = d[22];
	z->iy = d[23] | (d[24] << 8);
	z->ix = d[25] | (d[26] << 8);
	z->iff1 = d[27] ? 1 : 0;
	z->iff2 = d[28] ? 1 : 0;
	z->interrupt_mode = d[29] & 3;

	spec->xc.border = border;

	if (pc != 0)
	{
		// --- v1 snapshot: 48K, single (maybe compressed) 48KB block ---
		spec->SetMode(MODE_48K);
		z->pc = pc;
		// v1 is a 48K machine; set 48K-style paging (ROM1=48 BASIC in low slot).
		// For our 128K model, page in ROM so 0x0000-0x3FFF is 48 BASIC and RAM5/2/0.
		spec->mem.SetPaging(0x10); // ROM1 (48 BASIC), RAM0 top
		uint8_t ram48[0xC000];
		uint32_t n = v1compressed
			? Decompress(d + 30, len - 30, ram48, sizeof(ram48))
			: 0;
		if (!v1compressed)
		{
			uint32_t copy = (len - 30 < sizeof(ram48)) ? len - 30 : sizeof(ram48);
			for (uint32_t i = 0; i < copy; i++) ram48[i] = d[30 + i];
			n = copy;
		}
		// 48K memory map: 0x4000-0x7FFF=page5, 0x8000-0xBFFF=page2, 0xC000=page0
		for (uint32_t i = 0; i < n; i++)
		{
			uint16_t addr = 0x4000 + i;
			spec->mem.Write(addr, ram48[i]);
		}
		return true;
	}

	// --- v2/v3 extended header ---
	uint16_t extLen = d[30] | (d[31] << 8);
	uint32_t hdrEnd = 32 + extLen;
	if (hdrEnd > len) return false;
	z->pc = d[32] | (d[33] << 8);
	uint8_t hwMode = d[34];
	uint8_t port7ffd = d[35];

	// hwMode: v2 3/4/5 => 128K; v3 4/5/6 => 128K. Below that = 48K.
	bool is128 = (extLen == 23) ? (hwMode >= 3) : (hwMode >= 4);
	spec->SetMode(is128 ? MODE_128K : MODE_48K);

	if (is128)
		spec->mem.SetPaging(port7ffd);
	else
		spec->mem.SetPaging(0x10); // 48K-ish

	// --- Memory pages: [lenLo][lenHi][pageNum] then data ---
	uint32_t off = hdrEnd;
	while (off + 3 <= len)
	{
		uint16_t blkLen = d[off] | (d[off + 1] << 8);
		uint8_t  page   = d[off + 2];
		off += 3;
		bool uncompressed = (blkLen == 0xFFFF);
		uint32_t dataLen = uncompressed ? 0x4000 : blkLen;
		if (off + dataLen > len) break;

		// Map .z80 page number -> our RAM bank.
		//   128K: page 3..10 = RAM banks 0..7 ; page 8 = bank 5, etc.
		//   (pages 0,1,2 = ROM/other, ignored)
		int bank = -1;
		if (is128)
		{
			if (page >= 3 && page <= 10) bank = page - 3;   // 3->0 ... 10->7
		}
		else
		{
			// 48K: page 8=0x4000(bank5), 4=0x8000(bank2), 5=0xC000(bank0)
			if (page == 8) bank = 5;
			else if (page == 4) bank = 2;
			else if (page == 5) bank = 0;
		}

		if (bank >= 0 && bank < (int)kNumRamPages)
		{
			uint8_t *dst = spec->mem.ram[bank];
			if (uncompressed)
			{
				for (uint32_t i = 0; i < 0x4000; i++) dst[i] = d[off + i];
			}
			else
			{
				Decompress(d + off, dataLen, dst, 0x4000);
			}
		}
		off += dataLen;
	}
	return true;
}

} // namespace zx
