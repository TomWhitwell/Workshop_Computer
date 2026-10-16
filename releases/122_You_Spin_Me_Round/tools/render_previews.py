#!/usr/bin/env python3
# Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
"""Compile the actual DSP on the host and export matched stereo WAV previews."""
from pathlib import Path
import array
import subprocess
import tempfile
import wave
import sys
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='spin-preview-') as temp:
    binary = Path(temp) / 'render'
    raw = Path(temp) / 'audio.raw'
    subprocess.run(['clang++', '-std=c++17', '-O2', '-I'+str(root/'src'),
                    str(root/'tools/render_preview.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(raw)], check=True)
    data = array.array('h', raw.read_bytes())
    for offset, name in enumerate(['dry-organ', 'outside', 'inside']):
        pair = array.array('h')
        for frame in range(0, len(data), 6):
            pair.extend(data[frame + offset*2:frame + offset*2+2])
        if sys.byteorder != 'little':
            pair.byteswap()
        path = root/'previews'/f'{name}.wav'
        with wave.open(str(path), 'wb') as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            wav.writeframes(pair.tobytes())
        print(path)
