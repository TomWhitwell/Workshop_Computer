#!/usr/bin/env python3
"""Build fixed point tables offline; firmware uses no floating-point math."""
from pathlib import Path
import math
root=Path(__file__).resolve().parents[1]/'generated'
root.mkdir(exist_ok=True)
(root/'math_tables.hpp').write_text('#pragma once\n#include <cstdint>\nnamespace spin {\ninline constexpr int32_t log_table[1025]={' + ','.join(str(round(math.log2(1+i/1024)*524288)) for i in range(1025))+'};\ninline constexpr int32_t exp_table[1025]={' + ','.join(str(round(2**(i/1024)*8388608)) for i in range(1025))+'};\n}\n')
rows=[]
for rate in range(8000,40001,1000):
    cutoff=0.42*rate/48000
    h=[]
    for n in range(33):
        x=n-16
        sinc=2*cutoff if not x else math.sin(2*math.pi*cutoff*x)/(math.pi*x)
        h.append(sinc*(0.42-0.5*math.cos(2*math.pi*n/32)+0.08*math.cos(4*math.pi*n/32)))
    total=sum(h); q=[round(x/total*32768) for x in h]; q[16]+=32768-sum(q)
    assert sum(abs(x) for x in q)<65536
    rows.append('{'+','.join(map(str,q))+'}')
(root/'fir_tables.hpp').write_text('#pragma once\n#include <cstdint>\nnamespace spin {\ninline constexpr int16_t fir[33][33]={\n'+',\n'.join(rows)+'\n};\n}\n')
