#!/usr/bin/env python3
# Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
"""Regenerate the integer table; no trigonometry runs in the firmware callback."""
from pathlib import Path
import math
values = [round(32767 * math.sin(2 * math.pi * i / 256)) for i in range(256)]
output = Path(__file__).resolve().parents[1] / 'src/dsp/sine_table.h'
output.write_text('// Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT\n'
                  '// Generated from the mathematical sine function by tools/generate_sine.py.\n'
                  '#pragma once\n#include <cstdint>\nnamespace spin {\n'
                  'inline constexpr int16_t kSine[256] = {\n'
                  + ''.join('    ' + ', '.join(map(str, values[i:i+16])) + ',\n'
                            for i in range(0, 256, 16)) + '};\n}\n')
