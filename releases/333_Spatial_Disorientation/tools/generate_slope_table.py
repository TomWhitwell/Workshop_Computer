# Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
# Print the original mathematical lookup used in src/dsp/slope_table.h.
import math
print(",".join(str(round(math.degrees(math.asin(i/256))*2048/40)) for i in range(257)))
