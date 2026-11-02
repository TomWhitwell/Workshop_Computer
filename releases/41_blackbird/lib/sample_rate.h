#pragma once

// Central definition for the audio ISR rate used across the firmware.
// Keep everything derived from these macros so future sample-rate changes
// only require editing this file (or passing -DBB_SAMPLE_RATE_HZ=... to CMake).
#ifndef PROCESS_SAMPLE_RATE_HZ_INT
#define PROCESS_SAMPLE_RATE_HZ_INT    24000
#endif

// The ADC runs at 8 conversions per sample from 48MHz with an integer
// divider, so the rate must divide 48MHz / 8 = 6MHz exactly, or real time
// drifts from the sample count (e.g. 19200 would actually run at 19230.8Hz).
#if (6000000 % PROCESS_SAMPLE_RATE_HZ_INT) != 0
#error "PROCESS_SAMPLE_RATE_HZ_INT must divide 6000000 (e.g. 9600, 12000, 15000, 16000, 20000, 24000)"
#endif

#define PROCESS_SAMPLE_RATE_HZ        ((float)PROCESS_SAMPLE_RATE_HZ_INT)
#define PROCESS_SAMPLE_RATE_HZ_DOUBLE ((double)PROCESS_SAMPLE_RATE_HZ_INT)
#define PROCESS_SAMPLE_PERIOD_US      (1000000.0f / PROCESS_SAMPLE_RATE_HZ)
