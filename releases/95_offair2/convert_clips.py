"""
convert_clips.py — bake the OffAir radio audio into clips.h

OffAir's audio (Stations, looping interference, Insta-ference one-shots) is compiled
into clips.h as C arrays. This script generates that header from your own source
audio files. The source audio is NOT shipped in the repo — supply your own.

--------------------------------------------------------------------------------
USAGE
--------------------------------------------------------------------------------
1. Install the tools (one-off):     pip install miniaudio numpy

2. Pick your audio. Any format/rate works (WAV/MP3/AIFF/FLAC, mono or stereo, any
   sample rate — it is resampled and downmixed automatically). Three pools:
     - Stations        : the 2 main signals you tune in (played in baked-in mode)
     - Interference    : continuous beds (numbers/morse/data) that loop at dial spots
     - Insta-ference   : short isolated events (clicks/dropouts/bursts) on Pulse In 2

3. Point the script at your files (see the settings just below):
     - FOLDER          : directory with the Station + interference source files
     - CLIPS table     : one row per Station/interference file
                         (filename, start_sec, max_sec, var_name, description, sr, bits)
                         Keep 2 Station + 6 interference rows unless you also change
                         the stream counts in main.cpp.
     - LOOP_MAX_SEC    : caps interference loop length (frees flash)
     - MAX_BCAST_SEC   : caps Station length
     - ONESHOT_FOLDER  : a folder of Insta-ference files — ALL files in it are
                         auto-discovered (any count). ONESHOT_MAX_SEC caps each.

4. Run from this directory:         python convert_clips.py
   It rewrites clips.h and prints per-clip sizes plus the total flash budget
   (warns if you exceed ~2 MB — trim lengths or drop Insta-ference if so).

Then rebuild the firmware (Pico SDK / CMake / Ninja) and flash the new offair.uf2.

--------------------------------------------------------------------------------
AUDIO FORMATS (what gets baked)
--------------------------------------------------------------------------------
Interference / Insta-ference : 8 kHz,    uint8   (offset binary, 128 = silence)
Station clips                : 11025 Hz, 12-bit packed signed (2 samples per 3 bytes)

12-bit packing layout (A=sample[i], B=sample[i+1]):
  byte0 = A[11:4];  byte1 = (A[3:0]<<4) | B[11:8];  byte2 = B[7:0]
Unpack in C:
  int32_t a = (int32_t)((byte0<<4)|(byte1>>4));  if(a>=2048)a-=4096;
  int32_t b = (int32_t)(((byte1&0xF)<<8)|byte2); if(b>=2048)b-=4096;
"""

import miniaudio
import numpy as np
import os
import glob

TARGET_SR_INTF  = 8000
TARGET_SR_BCAST = 11025
MAX_BCAST_SEC   = 35

# One-shot bank: drop curated short event files (clicks, blips, dropouts, crashes,
# single morse bursts...) into this folder. Any count, any length. Each is converted
# to 8-bit 8kHz and played once-through when triggered by Pulse In 2.
ONESHOT_FOLDER  = "C:/Users/andyu/Downloads/NumbersStationsEtc/Oneshots/"
ONESHOT_EXTS    = ("*.wav", "*.mp3", "*.aif", "*.aiff", "*.flac")
ONESHOT_MAX_SEC = 3.0   # safety cap per clip

# Loop interference clips trimmed to <=12s (back halves rarely heard; frees flash
# for the one-shot bank). (filename, start_sec, max_sec, var_name, description, sr, bits)
LOOP_MAX_SEC = 12
CLIPS = [
    # AM band interference — voice / numbers
    ("POL-2015-05-18-1310utc.mp3",                          0, LOOP_MAX_SEC, "clip_pol",   "Polish numbers station",    TARGET_SR_INTF,  8),
    ("UM10-Q7JN-2015-02-09-1604utc-3956khz.mp3",            0, LOOP_MAX_SEC, "clip_um10",  "UM10 voice/tones",          TARGET_SR_INTF,  8),
    # SW band interference — data / digital signals
    ("F03-2017-08-21-0940-0945utc-AFSK400-960_10210khz.mp3",5, LOOP_MAX_SEC, "clip_f03",   "AFSK data signal",          TARGET_SR_INTF,  8),
    ("XT2-2232.5khz.mp3",                                   0, LOOP_MAX_SEC, "clip_xt2",   "XT2 unidentified digital",  TARGET_SR_INTF,  8),
    # LW band interference — tones / polytones
    ("Unid-polytone-2010-02-17.mp3",                        0, LOOP_MAX_SEC, "clip_poly",  "Unidentified polytone",     TARGET_SR_INTF,  8),
    ("MX-L-2013-10-16-1530utc-bygwraspe.mp3",               0, LOOP_MAX_SEC, "clip_mxl",   "MX-L tone sequence",        TARGET_SR_INTF,  8),
    # Altboot broadcast stations — 12-bit packed signed
    ("Demo1.wav",  0, MAX_BCAST_SEC, "clip_demo1", "Broadcast station 1", TARGET_SR_BCAST, 12),
    ("Demo2.wav",  0, MAX_BCAST_SEC, "clip_demo2", "Broadcast station 2", TARGET_SR_BCAST, 12),
]

FOLDER = "C:/Users/andyu/Downloads/NumbersStationsEtc/"

def decode_float(filename, start_sec, max_sec, target_sr):
    path = FOLDER + filename
    decoded = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.FLOAT32,
                                    nchannels=1, sample_rate=target_sr)
    samples = np.frombuffer(decoded.samples, dtype=np.float32).copy()
    start_sample = int(start_sec * target_sr)
    end_sample   = start_sample + int(max_sec * target_sr)
    if end_sample > len(samples):
        end_sample = len(samples)
    samples = samples[start_sample:end_sample]
    peak = np.max(np.abs(samples))
    if peak > 0:
        samples = samples / peak * 0.95
    return samples

def to_u8(samples):
    return np.clip((samples * 127.0 + 128.0), 0, 255).astype(np.uint8)

def to_12bit_packed(samples):
    s12 = np.clip(np.round(samples * 2047.0), -2048, 2047).astype(np.int16)
    if len(s12) % 2 != 0:
        s12 = np.append(s12, [0])
    packed = bytearray()
    for i in range(0, len(s12), 2):
        a = int(s12[i])   & 0xFFF
        b = int(s12[i+1]) & 0xFFF
        packed.append((a >> 4) & 0xFF)
        packed.append(((a & 0xF) << 4) | ((b >> 8) & 0xF))
        packed.append(b & 0xFF)
    return packed, len(s12)

def array_to_c_u8(data, var_name, description, sr):
    n = len(data)
    lines = [
        f"// {description}  ({n/sr:.1f}s at {sr}Hz, {n} samples = {n//1024}KB)",
        f"static const uint8_t {var_name}[{n}] PROGMEM_OR_CONST = {{",
    ]
    for i in range(0, n, 16):
        row = data[i:i+16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in row) + ",")
    lines += ["};", f"static const uint32_t {var_name}_len = {n};",
              f"static const uint32_t {var_name}_sr  = {sr};", ""]
    return "\n".join(lines)

def array_to_c_12bit(packed_bytes, sample_count, var_name, description, sr):
    nb = len(packed_bytes)
    lines = [
        f"// {description}  ({sample_count/sr:.1f}s at {sr}Hz, {sample_count} samples, 12-bit packed = {nb//1024}KB)",
        f"static const uint8_t {var_name}[{nb}] PROGMEM_OR_CONST = {{",
    ]
    for i in range(0, nb, 12):
        row = packed_bytes[i:i+12]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in row) + ",")
    lines += ["};",
              f"static const uint32_t {var_name}_len = {sample_count};  // samples, not bytes",
              f"static const uint32_t {var_name}_sr  = {sr};", ""]
    return "\n".join(lines)

# ---- main ----
out_parts = [
    "// clips.h — baked audio for OffAir",
    "// Auto-generated by convert_clips.py — do not edit by hand",
    "// Interference clips: 8kHz uint8, 128=silence",
    "// Broadcast clips:    11025Hz 12-bit packed signed (2 samples per 3 bytes)",
    "//",
    "#pragma once",
    "#include <stdint.h>",
    "",
    "#define PROGMEM_OR_CONST",
    "",
]

total_kb = 0
for (filename, start_sec, max_sec, var_name, description, sr, bits) in CLIPS:
    print(f"Converting {filename} [{start_sec}s..+{max_sec}s] @ {sr}Hz {bits}bit...")
    samples = decode_float(filename, start_sec, max_sec, sr)
    if bits == 8:
        data = to_u8(samples)
        kb = len(data) // 1024
        total_kb += kb
        print(f"  -> {var_name}: {len(data)} samples ({kb}KB, {len(data)/sr:.1f}s)")
        out_parts.append(array_to_c_u8(data, var_name, description, sr))
    else:
        packed, sample_count = to_12bit_packed(samples)
        kb = len(packed) // 1024
        total_kb += kb
        print(f"  -> {var_name}: {sample_count} samples ({kb}KB packed, {sample_count/sr:.1f}s)")
        out_parts.append(array_to_c_12bit(packed, sample_count, var_name, description, sr))

# ---- one-shot bank (auto-discovered folder) ----
oneshot_files = []
for ext in ONESHOT_EXTS:
    oneshot_files.extend(glob.glob(os.path.join(ONESHOT_FOLDER, ext)))
oneshot_files.sort()

oneshot_vars = []
if oneshot_files:
    print(f"\nOne-shot bank: {len(oneshot_files)} file(s) in {ONESHOT_FOLDER}")
    for idx, path in enumerate(oneshot_files):
        name = os.path.basename(path)
        var  = f"clip_os_{idx}"
        # decode directly by full path (one-shots live in their own folder)
        decoded = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.FLOAT32,
                                        nchannels=1, sample_rate=TARGET_SR_INTF)
        s = np.frombuffer(decoded.samples, dtype=np.float32).copy()
        s = s[:int(ONESHOT_MAX_SEC * TARGET_SR_INTF)]
        peak = np.max(np.abs(s)) if len(s) else 0
        if peak > 0:
            s = s / peak * 0.95
        data = to_u8(s)
        kb = len(data) / 1024
        total_kb += int(kb)
        print(f"  -> {var}: {name}  {len(data)} samples ({kb:.1f}KB, {len(data)/TARGET_SR_INTF:.2f}s)")
        out_parts.append(array_to_c_u8(data, var, f"one-shot: {name}", TARGET_SR_INTF))
        oneshot_vars.append(var)
else:
    print(f"\nOne-shot bank: no files found in {ONESHOT_FOLDER} (bank will be empty)")

out_parts += [
    "// 8-bit interference clip descriptor",
    "struct ClipDesc { const uint8_t* data; uint32_t len; uint32_t sr; };",
    "",
    "// AM band: POL + UM10",
    "static const ClipDesc kAmClips[2] = {",
    "    { clip_pol,  clip_pol_len,  clip_pol_sr  },",
    "    { clip_um10, clip_um10_len, clip_um10_sr },",
    "};",
    "",
    "// SW band: F03 + XT2",
    "static const ClipDesc kSwClips[2] = {",
    "    { clip_f03, clip_f03_len, clip_f03_sr },",
    "    { clip_xt2, clip_xt2_len, clip_xt2_sr },",
    "};",
    "",
    "// LW band: Polytone + MX-L",
    "static const ClipDesc kLwClips[2] = {",
    "    { clip_poly, clip_poly_len, clip_poly_sr },",
    "    { clip_mxl,  clip_mxl_len,  clip_mxl_sr  },",
    "};",
    "",
    "// All six interference clips flat (0=POL,1=UM10,2=F03,3=XT2,4=POLY,5=MXL)",
    "static const ClipDesc kAllClips[6] = {",
    "    { clip_pol,  clip_pol_len,  clip_pol_sr  },",
    "    { clip_um10, clip_um10_len, clip_um10_sr },",
    "    { clip_f03,  clip_f03_len,  clip_f03_sr  },",
    "    { clip_xt2,  clip_xt2_len,  clip_xt2_sr  },",
    "    { clip_poly, clip_poly_len, clip_poly_sr },",
    "    { clip_mxl,  clip_mxl_len,  clip_mxl_sr  },",
    "};",
    "",
    "// 12-bit packed broadcast clip descriptor (data is byte array, len is sample count)",
    "struct BcastDesc { const uint8_t* data; uint32_t len; uint32_t sr; };",
    "",
    "static const BcastDesc kBcastClips[2] = {",
    "    { clip_demo1, clip_demo1_len, clip_demo1_sr },",
    "    { clip_demo2, clip_demo2_len, clip_demo2_sr },",
    "};",
    "",
]

# One-shot bank descriptor (reuses ClipDesc). kNumOneShots auto-computed.
out_parts += [
    "// One-shot bank — short curated events, played once-through on Pulse In 2.",
    f"static constexpr int kNumOneShots = {len(oneshot_vars)};",
]
if oneshot_vars:
    out_parts.append("static const ClipDesc kOneShotClips[kNumOneShots] = {")
    for v in oneshot_vars:
        out_parts.append(f"    {{ {v}, {v}_len, {v}_sr }},")
    out_parts.append("};")
else:
    # Empty bank: provide a 1-element dummy so the array type is valid; firmware
    # guards on kNumOneShots == 0 and never reads it.
    out_parts.append("static const ClipDesc kOneShotClips[1] = { { clip_pol, clip_pol_len, clip_pol_sr } };")
out_parts.append("")

out_path = os.path.join(os.path.dirname(__file__), "clips.h")
with open(out_path, "w") as f:
    f.write("\n".join(out_parts))

FREE_FLASH_KB = 272
print(f"\nWrote clips.h ({total_kb}KB total audio)")
if total_kb > 1747 + FREE_FLASH_KB:
    print(f"  *** WARNING: ~{total_kb}KB exceeds the ~{1747+FREE_FLASH_KB}KB flash budget! ***")
else:
    print(f"  Flash budget OK (~{1747+FREE_FLASH_KB}KB available for audio).")
