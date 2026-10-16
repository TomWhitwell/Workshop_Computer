/*
usb_msc_host - play and record WAV files on a USB stick


Building: Pico SDK's bundled TinyUSB (0.18) reads USB sticks at 64kB/s max - too
slow for decent quality audio. Use TinyUSB 0.21 or later by passing
  -DPICO_TINYUSB_PATH=/path/to/tinyusb 
to cmake.


This example shows how to use the Workshop System Computer as a USB host for a
USB mass storage device (a USB stick), reading and writing files on it with the
FatFs filesystem library, while ComputerCard carries on running audio at 48kHz.
Requires Computer hardware Rev1_1 (USB host support), with nothing plugged into
the Computer's own USB socket except the stick (via a suitable adaptor).

What it does
  - Plays every .wav file in the root directory of the stick, one after
    another, in directory order (which is not necessarily alphabetical), looping
    forever. Files must be uncompressed 16-bit PCM, mono or stereo, at any
    sample rate up to 192kHz (files are resampled to 48kHz by linear
    interpolation, which is simple but not very high quality).
  - Records audio inputs into new files called REC0001.WAV, REC0002.WAV,
    and so on. These are 48kHz stereo 16-bit.

Controls
  Switch up      Record audio in 1/2 to a new file. Flipping back to the
                 middle position stops recording and plays the new file.
  Switch down    Skip to the next file.

Outputs
  Audio out 1/2  Left/right playback (mono files go to both). Silent while
                 recording.
  LEDs 0/1       Left/right level (the input level while recording)
  LED 2          On: playing, flashing: recording
  LED 4          USB stick mounted
  LED 5          Buffer underrun/overrun (the USB stick can't keep up)

How it works:

  Core 0 runs main(). It handles USB (TinyUSB), the filesystem (FatFs), and
         parsing WAV files -- all things that take too long to process in a
         single audio sample
  Core 1 runs ComputerCard, so ProcessSample() is called 48000 times per second.
         It just consumes or produces audio samples through a buffer in RAM.

The two cores communicate through a single ring buffer, ringBuf. When playing, 
core 0 fills it with data read from the file and core 1 takes samples
out, and vice-versa when recording.
There are always two byte positions:

  ringWritePos   total bytes ever put into the buffer (only ever increases)
  ringReadPos    total bytes ever taken out of the buffer (only ever increases)

The bytes between them are in the buffer, and byte number x lives at
ringBuf[x % ringSize]. Each position is only ever written by one core, which is
what makes this safe without locks. The one thing that must be done carefully
is the order of memory operations: a core must finish writing or reading data
before it publishes its updated position, hence the __dmb() memory barriers.

Finally, some state (the ring positions, the playback format) needs to be reset
when a file starts or stops. If core 0 did this directly, it could change things
under core 1's feet in the middle of a sample. So core 0 never does: it leaves
a request for core 1 (see SetMode()), and core 1 applies it at the start of its
next ProcessSample() call, while core 0 waits.

*/

#include "ComputerCard.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h" // TinyUSB
#include "ff.h"   // FatFs

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <algorithm>

#if TUSB_VERSION_NUMBER < 2100
#error "TinyUSB older than 0.21 reads USB sticks too slowly for this example; configure with -DPICO_TINYUSB_PATH=<tinyusb 0.21 or later>"
#endif

////////////////////////////////////////////////////////////////////////////////
// State shared between the cores
//
// Everything here is volatile, because it is accessed from both cores.

// The ring buffer. Its size must be a power of two, so that x % ringSize can be
// calculated as x & ringMask. ioChunk is how much we read from or write to the
// stick at a time. USB sticks are much more efficient with big transfers, and
// with ones that are a whole number of 512-byte sectors. It must divide
// ringSize so that a chunk never wraps around the end of the buffer.
static constexpr uint32_t ringSize = 131072;
static constexpr uint32_t ringMask = ringSize - 1;
static constexpr uint32_t ioChunk = 8192;

static uint8_t __attribute__((aligned(4))) ringBuf[ringSize];

static volatile uint32_t ringWritePos = 0; // bytes ever put into ringBuf
static volatile uint32_t ringReadPos = 0;  // bytes ever taken out of ringBuf

enum class Mode
{
	Idle,
	Play,
	Record
};

// Requests from core 0 to core 1. Core 0 fills in the pending values, then sets
// modeRequest, and waits for core 1 to clear it (see SetMode()).
static volatile bool modeRequest = false;
static volatile Mode pendingMode = Mode::Idle;
static volatile uint32_t pendingChannels = 1;
static volatile uint32_t pendingRateStep = 65536;
static volatile int32_t pendingNumFrames = 0;

// Status from core 1 to core 0
static volatile bool switchUp = false;      // switch is in the up position
static volatile bool skipRequest = false;   // switch was flicked down
static volatile bool playFinished = false;  // core 1 played the last sample of the file
static volatile bool recStopped = false;    // core 1 has stopped recording

// Status from core 0 to core 1
static volatile bool stickMounted = false;


////////////////////////////////////////////////////////////////////////////////
// Core 1: the audio code

class UsbStick : public ComputerCard
{
	// The current mode, latched from the pending values by HandleRequests()
	Mode mode = Mode::Idle;

	// Playback format
	uint32_t channels = 1;
	uint32_t blockAlign = 2;  // bytes per frame (one sample for each channel)
	int32_t rateStep = 65536; // file sample rate / 48kHz, in 16.16 fixed point
	int32_t numFrames = 0;    // length of the file

	// Playback position: frame + frac / 65536 is the position in the file, in
	// frames. For a 44.1kHz file, each output sample advances this by 0.91875
	// frames.
	int32_t frame = 0;
	int32_t frac = 0;

	bool recording = false;

	int32_t xrunLedTimer = 0;
	uint32_t blinkCounter = 0;

	// Linear interpolation between a and b. frac12 is 0-4095.
	static int32_t Interp(int32_t a, int32_t b, int32_t frac12)
	{
		return a + (((b - a) * frac12) >> 12);
	}

	void Xrun()
	{
		xrunLedTimer = 4800; // light the LED for 100ms
	}

	// Apply any request from core 0. This happens at the very start of
	// ProcessSample(), so we never change state halfway through a sample.
	void HandleRequests()
	{
		if (modeRequest)
		{
			mode = pendingMode;
			channels = pendingChannels;
			blockAlign = channels * 2;
			rateStep = pendingRateStep;
			numFrames = pendingNumFrames;
			frame = 0;
			frac = 0;
			ringWritePos = 0; // both positions are ours to reset,
			ringReadPos = 0;  // because core 0 is waiting
			playFinished = false;
			recording = (mode == Mode::Record);
			recStopped = !recording;

			// Everything above must be visible to core 0 before it sees that
			// the request is complete
			__dmb();
			modeRequest = false;
		}
	}

	// Produce one output sample from the file. left and right are 16-bit.
	void Play(int32_t &left, int32_t &right)
	{
		if (frame >= numFrames)
		{
			playFinished = true;
			return;
		}

		// To interpolate, we need this frame and the next one (unless this is
		// the last). Check that core 0 has already put them in the buffer.
		// The difference is signed, because frame can skip ahead of what has
		// been read if we've underrun.
		uint32_t pos = (uint32_t)frame * blockAlign;
		bool haveNext = (frame + 1 < numFrames);
		int32_t need = (int32_t)(haveNext ? 2 * blockAlign : blockAlign);
		if ((int32_t)(ringWritePos - pos) < need)
		{
			// Underrun: core 0 hasn't read the data from the stick in time.
			// We output silence, and wait. At the very start of a file this is
			// normal, because the buffer is still filling, so the LED isn't lit.
			if (frame > 0)
			{
				Xrun();
			}
			return;
		}

		// The samples are int16, interleaved left, right for stereo files
		const int16_t *a = (const int16_t *)(ringBuf + (pos & ringMask));
		const int16_t *b = haveNext ? (const int16_t *)(ringBuf + ((pos + blockAlign) & ringMask)) : a;
		int32_t frac12 = frac >> 4;

		left = Interp(a[0], b[0], frac12);
		right = (channels == 2) ? Interp(a[1], b[1], frac12) : left;

		// Advance by one output sample's worth of the file
		int32_t f = frac + rateStep;
		frame += f >> 16;
		frac = f & 0xFFFF;

		// Now we've finished with the data before this frame, so tell core 0 that
		// it can be overwritten. The barrier makes sure the reads above have
		// really happened first.
		__dmb();
		ringReadPos = (uint32_t)frame * blockAlign;
	}

	// Take one sample from the inputs (12-bit, so left and right are set to
	// -2048 to 2047) and, if recording, put it in the buffer as 16 bits
	void Record(int32_t &left, int32_t &right)
	{
		left = AudioIn1();
		right = AudioIn2();

		if (!recording)
		{
			return;
		}
		if (!switchUp)
		{
			// Switch moved: stop. Core 0 finishes the file once it sees the flag.
			// (recStopped must only be set after the last sample was written)
			recording = false;
			__dmb();
			recStopped = true;
			return;
		}

		uint32_t wp = ringWritePos;
		if (wp - ringReadPos > ringSize - 4)
		{
			// Overrun: core 0 hasn't written the stick fast enough, and the
			// buffer is full. This sample is lost.
			Xrun();
			return;
		}
		int16_t *p = (int16_t *)(ringBuf + (wp & ringMask));
		p[0] = (int16_t)(left << 4); // 12 bit to 16 bit
		p[1] = (int16_t)(right << 4);
		__dmb(); // sample must be in the buffer before we say it is
		ringWritePos = wp + 4;
	}

public:
	// Called at 48kHz
	virtual void ProcessSample()
	{
		HandleRequests();

		switchUp = (SwitchVal() == Up);

		// A flick down (to the momentary position) skips to the next file
		if (SwitchChanged() && SwitchVal() == Down)
		{
			skipRequest = true;
		}

		int32_t left = 0, right = 0;
		if (mode == Mode::Play)
		{
			Play(left, right);
			left >>= 4; // 16-bit to 12-bit
			right >>= 4;
			AudioOut1(left);
			AudioOut2(right);
		}
		else
		{
			if (mode == Mode::Record)
			{
				Record(left, right);
			}
			AudioOut1(0);
			AudioOut2(0);
		}

		// LEDs. Show levels, and status.
		blinkCounter++;
		LedBrightness(0, std::min<int32_t>(std::abs(left) * 2, 4095));
		LedBrightness(1, std::min<int32_t>(std::abs(right) * 2, 4095));
		LedOn(2, (mode == Mode::Play) || (recording && (blinkCounter & 8192)));
		LedOn(4, stickMounted);
		if (xrunLedTimer > 0)
		{
			xrunLedTimer--;
		}
		LedOn(5, xrunLedTimer > 0);
	}
};


////////////////////////////////////////////////////////////////////////////////
// Core 0: startup and handshake with core 1

static FATFS fatFs; // the mounted filesystem
static FIL wavFile; // the file being played or recorded
static UsbStick *card = nullptr;

// Run on core 1. ComputerCard::Run() doesn't return.
static void Core1Entry()
{
	card->Run();
}

// Ask core 1 to switch to a new mode, and wait until it has. It resets the
// ring buffer, so this is also how we start a new file. The arguments only
// matter for Mode::Play.
static void SetMode(Mode mode, uint32_t channels = 1, uint32_t sampleRate = 48000, int32_t numFrames = 0)
{
	pendingMode = mode;
	pendingChannels = channels;
	pendingRateStep = (uint32_t)(((uint64_t)sampleRate << 16) / 48000);
	pendingNumFrames = numFrames;
	__dmb();
	modeRequest = true;
	while (modeRequest)
	{
		tight_loop_contents();
	}
}

// WAV files are little-endian
static uint16_t Le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t Le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void PutLe16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void PutLe32(uint8_t *p, uint32_t v)
{
	PutLe16(p, v);
	PutLe16(p + 2, v >> 16);
}

////////////////////////////////////////////////////////////////////////////////
// Playback (core 0)

struct WavFormat
{
	uint32_t channels;
	uint32_t sampleRate;
	uint32_t blockAlign;
	FSIZE_t dataOffset; // where in the file the sample data starts
	uint32_t dataBytes; // length of the sample data
};

enum class PlayResult
{
	Next,   // play the next file
	Error,  // couldn't play this file
	Record  // the switch was moved up, so record instead
};

// Parse the header of a WAV file, which is a sequence of chunks, each with a
// four-character name and a length. We need the "fmt " chunk, which describes
// the audio format, and the "data" chunk, which holds the samples. Others (for
// example "LIST" metadata) are skipped. On return the file position is at the
// start of the sample data.
static bool ParseWav(FIL *fil, WavFormat &fmt)
{
	uint8_t buf[40];
	UINT br;

	if (f_read(fil, buf, 12, &br) != FR_OK || br != 12)
	{
		return false;
	}
	if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
	{
		return false;
	}

	bool haveFmt = false;
	uint32_t format = 0, bits = 0;
	while (true)
	{
		if (f_read(fil, buf, 8, &br) != FR_OK || br != 8)
		{
			return false;
		}
		uint32_t chunkSize = Le32(buf + 4);
		FSIZE_t chunkData = f_tell(fil);

		if (memcmp(buf, "fmt ", 4) == 0)
		{
			UINT n = (chunkSize < sizeof(buf)) ? chunkSize : sizeof(buf);
			if (n < 16 || f_read(fil, buf, n, &br) != FR_OK || br != n)
			{
				return false;
			}
			format = Le16(buf);
			fmt.channels = Le16(buf + 2);
			fmt.sampleRate = Le32(buf + 4);
			fmt.blockAlign = Le16(buf + 12);
			bits = Le16(buf + 14);
			if (format == 0xFFFE && n >= 26) // WAVE_FORMAT_EXTENSIBLE: real format is in the subformat GUID
			{
				format = Le16(buf + 24);
			}
			haveFmt = true;
		}
		else if (memcmp(buf, "data", 4) == 0)
		{
			if (!haveFmt)
			{
				return false;
			}
			// The size can be wrong (0xFFFFFFFF in a recording that was never
			// finished, for example), so never trust it beyond the end of the file
			FSIZE_t left = f_size(fil) - chunkData;
			fmt.dataOffset = chunkData;
			fmt.dataBytes = (chunkSize < left) ? chunkSize : (uint32_t)left;
			break;
		}

		// Skip to the next chunk (chunks are padded to an even length)
		if (f_lseek(fil, chunkData + chunkSize + (chunkSize & 1)) != FR_OK)
		{
			return false;
		}
	}

	if (format != 1 || bits != 16 || (fmt.channels != 1 && fmt.channels != 2)
	    || fmt.blockAlign != fmt.channels * 2 || fmt.sampleRate < 1000 || fmt.sampleRate > 192000)
	{
		return false;
	}
	fmt.dataBytes -= fmt.dataBytes % fmt.blockAlign; // whole frames only
	return fmt.dataBytes > 0;
}

// Play one file. Core 0 has one job while this runs: keep the ring buffer full.
static PlayResult PlayFile(const char *name)
{
	if (f_open(&wavFile, name, FA_READ) != FR_OK)
	{
		return PlayResult::Error;
	}

	WavFormat fmt = {};
	if (!ParseWav(&wavFile, fmt))
	{
		f_close(&wavFile);
		return PlayResult::Error;
	}

	// Start core 1 playing. It sets the ring buffer positions to zero, so byte x
	// of the sample data goes at ringBuf[x % ringSize]
	f_lseek(&wavFile, fmt.dataOffset);
	SetMode(Mode::Play, fmt.channels, fmt.sampleRate, (int32_t)(fmt.dataBytes / fmt.blockAlign));
	skipRequest = false;

	PlayResult result = PlayResult::Error;

	// tuh_msc_mounted() goes false if the stick is pulled out
	while (tuh_msc_mounted(1))
	{
		if (switchUp)
		{
			result = PlayResult::Record;
			break;
		}
		if (skipRequest || playFinished)
		{
			result = PlayResult::Next;
			break;
		}

		// Is there room for another chunk, and more file to read? The difference
		// is signed, as core 1 can move ringReadPos past ringWritePos briefly
		// if it underruns.
		uint32_t writePos = ringWritePos;
		bool room = (int32_t)(writePos - ringReadPos) <= (int32_t)(ringSize - ioChunk);
		if (writePos >= fmt.dataBytes || !room)
		{
			// Nothing to do. TinyUSB needs tuh_task() called regularly, or
			// nothing happens.
			tuh_task();
			continue;
		}

		// Read the next chunk from the file. writePos is always a multiple of
		// ioChunk here, so the chunk can't wrap around the end of the buffer.
		UINT n = std::min<uint32_t>(ioChunk, fmt.dataBytes - writePos);
		UINT br = 0;
		if (f_read(&wavFile, ringBuf + (writePos & ringMask), n, &br) != FR_OK || br != n)
		{
			break;
		}

		// Data must be in the buffer before core 1 is told it is there
		__dmb();
		ringWritePos = writePos + n;

	}

	SetMode(Mode::Idle);
	f_close(&wavFile);
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// Recording (core 0)

// We write a standard 44-byte WAV header for 48kHz stereo 16-bit audio, followed
// by a "JUNK" chunk (which players ignore) to pad the header to 512 bytes. That
// way the sample data starts on a sector boundary, and every write of ioChunk
// bytes after it is sector-aligned, which is much faster on a USB stick.
//
// The RIFF and data chunk sizes aren't known until we finish, so they are set
// to 0xFFFFFFFF at first, and filled in at the end. If the stick is pulled out
// or the power fails, the file is still playable, because ParseWav() limits the
// data size to the size of the file.
static constexpr uint32_t recHeaderSize = 512;
static constexpr uint32_t recMaxBytes = 0xFFFFFFFFu - recHeaderSize - ioChunk; // WAV and FAT32 size limit

static void MakeRecHeader(uint8_t *h)
{
	memset(h, 0, recHeaderSize);
	memcpy(h, "RIFF", 4);
	PutLe32(h + 4, 0xFFFFFFFF);  // file size, filled in later
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "fmt ", 4);
	PutLe32(h + 16, 16);         // fmt chunk size
	PutLe16(h + 20, 1);          // PCM
	PutLe16(h + 22, 2);          // channels
	PutLe32(h + 24, 48000);      // sample rate
	PutLe32(h + 28, 48000 * 4);  // byte rate
	PutLe16(h + 32, 4);          // block align
	PutLe16(h + 34, 16);         // bits per sample
	memcpy(h + 36, "JUNK", 4);
	PutLe32(h + 40, recHeaderSize - 44 - 8);
	memcpy(h + recHeaderSize - 8, "data", 4);
	PutLe32(h + recHeaderSize - 4, 0xFFFFFFFF); // data size, filled in later
}

// Write a 32-bit little-endian value at a file offset
static bool PatchLe32(FSIZE_t offset, uint32_t v)
{
	uint8_t b[4];
	UINT bw;
	PutLe32(b, v);
	return f_lseek(&wavFile, offset) == FR_OK && f_write(&wavFile, b, 4, &bw) == FR_OK && bw == 4;
}

// Record until the switch leaves the up position. On success, name is set to the
// name of the new file.
static bool RecordFile(char *name, size_t nameLen)
{
	// Find the first unused name
	int num;
	FILINFO info;
	for (num = 1; num <= 9999; num++)
	{
		snprintf(name, nameLen, "REC%04d.WAV", num);
		if (f_stat(name, &info) == FR_NO_FILE)
		{
			break;
		}
	}
	FRESULT res = (num <= 9999) ? f_open(&wavFile, name, FA_WRITE | FA_CREATE_NEW) : FR_DENIED;
	if (res != FR_OK)
	{
		return false;
	}

	// The ring buffer isn't in use yet, so borrow it to build the header
	UINT bw;
	MakeRecHeader(ringBuf);
	if (f_write(&wavFile, ringBuf, recHeaderSize, &bw) != FR_OK || bw != recHeaderSize)
	{
		f_close(&wavFile);
		f_unlink(name);
		return false;
	}

	SetMode(Mode::Record);

	uint32_t dataBytes = 0;
	bool ok = true;
	absolute_time_t lastSync = get_absolute_time();

	while (true)
	{
		if (!tuh_msc_mounted(1))
		{
			ok = false;
			break;
		}

		// Read the stop flag before the write position, so that once it is set,
		// the write position we read is the final one
		bool stopped = recStopped;
		__dmb();
		uint32_t avail = ringWritePos - ringReadPos;

		// Write full chunks as they become available. After recording stops,
		// write whatever is left, which is less than a chunk.
		bool last = stopped && avail <= ioChunk;
		uint32_t n = (avail >= ioChunk) ? ioChunk : (last ? avail : 0);

		if (n > 0)
		{
			// ringReadPos is a multiple of ioChunk, so this never wraps
			if (f_write(&wavFile, ringBuf + (ringReadPos & ringMask), n, &bw) != FR_OK || bw != n)
			{
				ok = false;
				break;
			}
			__dmb(); // the data must be written out before core 1 may reuse the space
			ringReadPos = ringReadPos + n;
			dataBytes += n;
		}
		if (last || dataBytes >= recMaxBytes)
		{
			break;
		}
		if (n == 0)
		{
			tuh_task();
		}

		// Sync every 5 seconds, so the recording survives removal of the stick.
		// (f_sync writes out the file size and directory entry, which are
		// otherwise only written when the file is closed.)
		if (absolute_time_diff_us(lastSync, get_absolute_time()) > 5000000)
		{
			f_sync(&wavFile);
			lastSync = get_absolute_time();
		}
	}

	SetMode(Mode::Idle);

	// Fill in the sizes in the header, and close the file
	if (ok && dataBytes > 0)
	{
		ok = PatchLe32(4, recHeaderSize - 8 + dataBytes) && PatchLe32(recHeaderSize - 4, dataBytes);
	}
	ok = (f_close(&wavFile) == FR_OK) && ok;

	if (ok && dataBytes == 0)
	{
		f_unlink(name); // don't leave empty files
		return false;
	}
	return ok;
}

////////////////////////////////////////////////////////////////////////////////
// File selection

static bool IsWavFile(const FILINFO &info)
{
	const char *name = info.fname;
	size_t len = strlen(name);
	if ((info.fattrib & (AM_DIR | AM_HID | AM_SYS)) || name[0] == '.' || len < 4) // skip macOS "._" files
	{
		return false;
	}
	return strcasecmp(name + len - 4, ".wav") == 0;
}

// Scan the root directory for WAV files, stopping at the index-th one, or the
// one called name (if non-null). Returns the index of the file found, or -1.
// count receives the number of WAV files before the stopping point (so the total
// number, if it wasn't found). The details of the file found are put in info.
static int ScanWavs(int index, const char *name, FILINFO &info, int &count)
{
	DIR dir;
	int found = -1;
	count = 0;
	if (f_opendir(&dir, "/") != FR_OK)
	{
		return -1;
	}
	while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
	{
		if (IsWavFile(info))
		{
			if (count == index || (name && strcasecmp(name, info.fname) == 0))
			{
				found = count;
				break;
			}
			count++;
		}
	}
	f_closedir(&dir);
	return found;
}

static int CountWavs(FILINFO &info)
{
	int count;
	ScanWavs(-1, nullptr, info, count);
	return count;
}

static bool FindWavByIndex(int index, FILINFO &info)
{
	int count;
	return ScanWavs(index, nullptr, info, count) == index;
}

static int FindWavByName(const char *name, FILINFO &info)
{
	int count;
	return ScanWavs(-1, name, info, count);
}

// Play and record WAV files in the root directory, until the stick is removed
static void RunStick()
{
	static FILINFO fileInfo;
	int numFiles = CountWavs(fileInfo);

	int index = 0;
	int failures = 0; // consecutive files that couldn't be played
	while (tuh_msc_mounted(1))
	{
		if (switchUp)
		{
			char name[16];
			if (RecordFile(name, sizeof(name)))
			{
				// Rescan, and play the new recording
				numFiles = CountWavs(fileInfo);
				index = std::max(FindWavByName(name, fileInfo), 0);
				failures = 0;
			}
			else
			{
				// Don't retry until the switch is flipped again
				while (switchUp && tuh_msc_mounted(1))
				{
					tuh_task();
				}
			}
			continue;
		}

		// Nothing playable: just wait for recording or removal
		if (numFiles == 0 || failures >= numFiles || !FindWavByIndex(index, fileInfo))
		{
			tuh_task();
			continue;
		}

		PlayResult result = PlayFile(fileInfo.fname);
		if (result == PlayResult::Record)
		{
			continue;
		}
		failures = (result == PlayResult::Error) ? failures + 1 : 0;
		index = (index + 1) % numFiles;
	}
}

int main()
{
	// The card is constructed here on core 0, so that it exists before core 1
	// starts running it. (It is a plain local, not a function-level static:
	// main() never returns, and a static would add a thread-safe initialisation
	// guard, which drags a lot of C++ exception code into the binary.)
	UsbStick player;
	card = &player;
	multicore_launch_core1(Core1Entry);

	// Start TinyUSB as a host
	tusb_rhport_init_t hostInit = {
		.role = TUSB_ROLE_HOST,
		.speed = TUSB_SPEED_AUTO
	};
	tusb_init(BOARD_TUH_RHPORT, &hostInit);

	while (true)
	{
		// tuh_task() runs the USB stack: enumeration of newly plugged devices,
		// and completion of transfers. It must be called often.
		tuh_task();
		if (tuh_msc_mounted(1))
		{
			stickMounted = true;

			// Mount the filesystem. FatFs reads the stick through the functions
			// in diskio.c.
			if (f_mount(&fatFs, "", 1) == FR_OK)
			{
				RunStick();
			}
			f_unmount("");

			// Wait for removal before trying again
			while (tuh_msc_mounted(1))
			{
				tuh_task();
			}
			stickMounted = false;
		}
	}
}
