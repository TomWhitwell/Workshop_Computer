# Cycle-count the real firmware's per-sample code on an emulated Cortex-M0+.
#
# Why: ProcessSample() runs in ComputerCard's audio interrupt and must
# finish, together with ComputerCard's own work, inside one 48kHz sample.
# Overrunning scrambles the ADC/mux/DAC sequence -- garbled audio. This
# runs the compiled firmware (not a host build) on the Unicorn emulator,
# counts Cortex-M0+ cycles per sample through a scripted performance, and
# reports the worst cases against the budget.
#
# Setup (once):   python3 -m venv venv && venv/bin/pip install unicorn capstone
# Build:          cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DIMPERMANENCE_BENCH=ON
#                 cmake --build build-bench
# Run:            venv/bin/python tools/bench/cycles.py build-bench/Impermanence.elf 1.0 4000
#                 (args: ELF, scenario length scale, CPU cycles per sample:
#                  4000 at 192MHz, 3000 at 144MHz)
#
# Cycle model (Cortex-M0+ TRM): loads/stores 2, taken branch 2, BL 3,
# push/pop 1+n, MULS 1 (RP2040), hardware divider ~24 per call, +2 per
# peripheral access. Not modelled: RAM contention with core 1 and DMA, so
# keep real margin -- aim for the worst case under ~85%.
import sys, struct, math, subprocess, collections
from unicorn import *
from unicorn.arm_const import *
from capstone import *
from capstone.arm import *

ELF = sys.argv[1]
SCALE = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
BUDGET = int(sys.argv[3]) if len(sys.argv) > 3 else 3000  # CPU cycles per 48kHz sample
import shutil, os
NM = os.environ.get("ARM_NM") or shutil.which("arm-none-eabi-nm") or "arm-none-eabi-nm"

# ---- symbols
syms = {}
for line in subprocess.run([NM, ELF], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        syms[p[2]] = int(p[0], 16)
dem = {}
for line in subprocess.run([NM, "-C", ELF], capture_output=True, text=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3:
        dem[p[2]] = int(p[0], 16)

def sym(name):
    return dem[name] if name in dem else syms[name]

# ---- load ELF segments at their virtual addresses (post copy_to_ram state)
data = open(ELF, "rb").read()
e_phoff, = struct.unpack_from("<I", data, 28)
e_phentsize, e_phnum = struct.unpack_from("<HH", data, 42)
mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for base, size in [(0x0, 0x1000), (0x10000000, 0x200000), (0x20000000, 0x42000),
                   (0x40000000, 0x100000), (0x50000000, 0x400000), (0xd0000000, 0x1000), (0xe0000000, 0x100000)]:
    mu.mem_map(base, size)
for i in range(e_phnum):
    p_type, p_off, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from("<IIIIII", data, e_phoff + i * e_phentsize)
    if p_type == 1 and p_filesz:
        mu.mem_write(p_vaddr, data[p_off:p_off + p_filesz])

STOP = 0x100
mu.mem_write(STOP, b"\x00\xbf\x00\xbf")  # nops
STUB = 0x200
mu.mem_write(STUB, b"\x70\x47")  # bx lr

# ---- cycle model
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = True
block_cache = {}
PROFILE = None
PROFILING = False
cycles = 0
pending_fallthrough = None  # (fallthrough address) if last block ended in a conditional branch

def insn_cost(ins):
    m = ins.mnemonic
    if m.startswith("push") or m.startswith("pop"):
        n = len(ins.operands)
        pc = any(o.type == ARM_OP_REG and o.reg == ARM_REG_PC for o in ins.operands)
        return 1 + n + (2 if pc else 0)
    if m.startswith("ldm") or m.startswith("stm"):
        return 1 + len(ins.operands) - 1
    if m.startswith("ldr") or m.startswith("str"):
        return 2
    if m == "bl":
        return 3
    if m in ("bx", "blx"):
        return 2
    if m == "b":
        return 2
    if m.startswith("b") and len(m) == 3 and m not in ("bic",):  # conditional b<cc>
        return 1  # +1 if taken, added at the next block
    if m.startswith("b") and m.endswith(".n") and len(m) == 5:
        return 1
    if m in ("mov", "add") and ins.operands and ins.operands[0].type == ARM_OP_REG and ins.operands[0].reg == ARM_REG_PC:
        return 2
    if m in ("dmb", "dsb", "isb"):
        return 3
    return 1

def is_cond_branch(ins):
    return ins.id == ARM_INS_B and ins.cc not in (ARM_CC_AL, ARM_CC_INVALID)

def hook_block(uc, address, size, _):
    global cycles, pending_fallthrough
    if pending_fallthrough is not None:
        if address != pending_fallthrough:
            cycles += 1
        pending_fallthrough = None
    key = (address, size)
    info = block_cache.get(key)
    if info is None:
        code = bytes(uc.mem_read(address, size))
        insns = list(md.disasm(code, address))
        cost = sum(insn_cost(i) for i in insns)
        last = insns[-1] if insns else None
        ft = (last.address + last.size) if (last is not None and is_cond_branch(last)) else None
        info = (cost, ft)
        block_cache[key] = info
    cycles += info[0]
    if PROFILE is not None and PROFILING:
        PROFILE[address] = PROFILE.get(address, 0) + info[0]
    pending_fallthrough = info[1]

def hook_periph(uc, access, address, size, value, _):
    global cycles
    cycles += 2  # APB peripheral wait states (approximate)

DIV_COST = 24
def hook_div(uc, address, size, signed):
    global cycles, pending_fallthrough
    a = uc.reg_read(UC_ARM_REG_R0)
    b = uc.reg_read(UC_ARM_REG_R1)
    if signed:
        sa = a - (1 << 32) if a & 0x80000000 else a
        sb = b - (1 << 32) if b & 0x80000000 else b
        if sb == 0:
            q, r = 0, 0
        else:
            q = int(sa / sb)
            r = sa - q * sb
    else:
        q, r = (a // b, a % b) if b else (0, 0)
    uc.reg_write(UC_ARM_REG_R0, q & 0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R1, r & 0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    cycles += DIV_COST
    pending_fallthrough = None

mu.hook_add(UC_HOOK_BLOCK, hook_block)
mu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, hook_periph, begin=0x40000000, end=0x400FFFFF)
idiv = sym("__wrap___aeabi_idiv")
uidiv = sym("__wrap___aeabi_uidiv")
mu.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: hook_div(uc, a, s, True), begin=idiv, end=idiv)
mu.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: hook_div(uc, a, s, False), begin=uidiv, end=uidiv)

def hook_mem(uc, address, size, kind):
    global cycles, pending_fallthrough
    r0, r1, r2 = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
    if kind == "set":
        uc.mem_write(r0, bytes([r1 & 0xFF]) * r2)
    else:
        uc.mem_write(r0, bytes(uc.mem_read(r1, r2)))
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    cycles += 20 + r2 // 2
    pending_fallthrough = None

for name, kind in [("__wrap_memset", "set"), ("__wrap_memcpy", "cpy")]:
    a = sym(name)
    mu.hook_add(UC_HOOK_CODE, (lambda k: lambda uc, ad, s, d: hook_mem(uc, ad, s, k))(kind), begin=a, end=a)

def call(addr, r0=0):
    global cycles, pending_fallthrough
    cycles = 0
    pending_fallthrough = None
    mu.reg_write(UC_ARM_REG_SP, 0x20042000)
    mu.reg_write(UC_ARM_REG_R0, r0)
    mu.reg_write(UC_ARM_REG_LR, STOP | 1)
    try:
        mu.emu_start(addr | 1, STOP, count=5_000_000)
    except UcError as e:
        print("fault calling %x at pc=%x lr=%x" % (addr, mu.reg_read(UC_ARM_REG_PC), mu.reg_read(UC_ARM_REG_LR)))
        raise
    return cycles

# ---- static constructors
ia0, ia1 = syms["__init_array_start"], syms["__init_array_end"]
for p in range(ia0, ia1, 4):
    fn, = struct.unpack("<I", mu.mem_read(p, 4))
    if fn and (fn & ~1) != sym("_retrieve_unique_id_on_boot"): call(fn & ~1)

# ---- fake card object
off = struct.unpack("<11I", mu.mem_read(sym("bench_offsets"), 44))
O_KNOBS, O_PULSE, O_LPULSE, O_CV, O_ADCL, O_ADCR, O_CONN, O_SW, O_LSW, O_DAC, SIZE = off
THIS = 0x20038000
VT = 0x20039000
mu.mem_write(VT, struct.pack("<16I", *([STUB | 1] * 16)))
mu.mem_write(THIS, b"\0" * (SIZE + 64))
mu.mem_write(THIS, struct.pack("<I", VT + 8))
mu.mem_write(THIS + O_CONN, bytes([1, 1, 0, 0, 1, 1]))

# ComputerCard overhead: BufferFull with ProcessSample stubbed out, and the CV PWM IRQ.
bf = [call(sym("ComputerCard::BufferFull()"), THIS) for _ in range(64)]
pwm = [call(sym("ComputerCard::OnCVPWMWrap()")) for _ in range(16)]
BF = max(bf)
PWM = max(pwm)
IRQ = 40  # exception entry/exit + vector (approx)
print(f"ComputerCard BufferFull (no ProcessSample): max {BF}, mean {sum(bf)/len(bf):.0f} cycles")
print(f"CV PWM wrap IRQ: {PWM} cycles, {BUDGET/2000:.1f} per sample")
PWM_PER_SAMPLE = (BUDGET * 48000.0 / 2000.0) / 48000.0
OVERHEAD = BF + IRQ + int(PWM_PER_SAMPLE * (PWM + IRQ))
print(f"fixed overhead per sample ~{OVERHEAD} cycles")

PS = sym("ImpermanenceCard::ProcessSample()")

state = dict(knobs=[2000, 2000, 1500, 2048], sw=1, cv=[0, 0], p1=False, p2=False, audioL=0, audioR=0)
last = [False, False]
def write_inputs(n):
    ph = 2 * math.pi * 220 * n / 48000
    l = int(1200 * math.sin(ph)); r = int(1000 * math.sin(ph * 1.5))
    mu.mem_write(THIS + O_KNOBS, struct.pack("<4i", *state["knobs"]))
    mu.mem_write(THIS + O_SW, struct.pack("<ii", state["sw"], state["sw"]))
    mu.mem_write(THIS + O_CV, struct.pack("<2i", *state["cv"]))
    mu.mem_write(THIS + O_ADCL, struct.pack("<hh", l, r))
    p = [state["p1"], state["p2"]]
    mu.mem_write(THIS + O_PULSE, bytes([int(p[0]), int(p[1])]))
    mu.mem_write(THIS + O_LPULSE, bytes([int(last[0]), int(last[1])]))
    last[0], last[1] = p

results = collections.defaultdict(list)
ticks = []
worst = []
TICK = sym("bench_control_tick")
LEDBUF = 0x2003A000
n = 0
def run(phase, count, per_sample=None):
    global n
    for i in range(int(count * SCALE)):
        if per_sample:
            per_sample(i)
        write_inputs(n)
        global PROFILING, PROFILE
        saved = PROFILE
        PROFILE = {}
        PROFILING = True
        c = call(PS, THIS)
        PROFILING = False
        if c > 2000:
            worst.append((c, phase, PROFILE))
        if saved is not None:
            for k, v in PROFILE.items(): saved[k] = saved.get(k, 0) + v
        PROFILE = saved
        results[phase].append(c)
        ticks.append(call(TICK, LEDBUF))
        state["p1"] = state["p2"] = False
        n += 1

run("settle", 5200 / SCALE)
def rec(i):
    if i == 0 or i == 6000: state["p1"] = True
run("recording (short take)", 6500, rec)
state["knobs"][0] = 3900; state["knobs"][2] = 3000
PROFILE = {}
run("texture, high chaos, held", 8000)
prof = PROFILE; PROFILE = None
import subprocess as sp
A2L = NM.replace("arm-none-eabi-nm", "arm-none-eabi-addr2line")
addrs = sorted(prof)
out = sp.run([A2L, "-e", ELF, "-f", "-C", "-i"] + ["%x" % a for a in addrs], capture_output=True, text=True).stdout
# with -i, each address gives 1+ (func, file:line) pairs; separate by re-querying one at a time is slow, so query individually
agg = collections.Counter(); aggf = collections.Counter()
for a in addrs:
    o = sp.run([A2L, "-e", ELF, "-f", "-C", "-i", "%x" % a], capture_output=True, text=True).stdout.splitlines()
    funcs = o[0::2]; locs = o[1::2]
    innermost = funcs[0].split("(")[0]
    fl = locs[0].split("/")[-1].split(" ")[0]
    agg[innermost] += prof[a]; aggf[fl.split(":")[0]] += prof[a]
tot = sum(prof.values())
print("\nprofile, texture high chaos (cycles/sample):")
nsamp = int(8000*SCALE)
for k, v in agg.most_common(22): print(f"  {v/nsamp:7.0f}  {k}")
print("by file:")
for k, v in aggf.most_common(10): print(f"  {v/nsamp:7.0f}  {k}")
def sweep(i):
    state["knobs"][0] = 3900 - (i * 3000) // 12000
run("texture, sweeping chaos", 12000, sweep)
def tap(i):
    state["sw"] = 0 if i < 50 else 1
run("tap down", 200, tap)
def clocked(i):
    if i % 800 == 0: state["p2"] = True
    state["knobs"][0] = 1000 + (i * 3000) // 12000
run("rhythm, fast clock, sweeping", 12000, clocked)
state["sw"] = 0
run("tap back to texture", 100)
state["sw"] = 1
def worst_case(i):
    if i % 7001 == 0: state["p2"] = True          # odd tempo: non-integer speeds
    if i == 5000 or i == 16000: state["p1"] = True  # record while playing
    state["knobs"][0] = 4000 - (i * 3000) // 24000 if (i // 3000) % 2 == 0 else 1000 + (i * 3000) // 24000
    state["knobs"][2] = 4095
    state["cv"][0] = (i // 500) % 2 * 600
run("texture, clocked odd tempo, recording, CV", 24000, worst_case)
def clear_record(i):
    state["knobs"][0] = 14 if i < 6000 else 3500 + (i % 3000) // 10  # clear, then record + re-cut
    state["cv"][0] = 0
    if i % 5000 == 0: state["p2"] = True
run("Main fully CCW: clear, then record", 16000, clear_record)

print(f"\nper-sample ProcessSample cycles (budget {BUDGET} total, {BUDGET - OVERHEAD} after overhead):")
for phase, v in results.items():
    s = sorted(v)
    over = sum(1 for c in v if c + OVERHEAD > BUDGET)
    print(f"  {phase:32s} mean {sum(v)/len(v):6.0f}  p99 {s[int(len(s)*0.99)-1]:6d}  max {s[-1]:6d}  over budget {over}/{len(v)}")
allv = [c for v in results.values() for c in v]
print(f"core 1 control tick: mean {sum(ticks)/len(ticks):.0f}, max {max(ticks)} cycles")
print(f"  worst total incl. overhead: {max(allv) + OVERHEAD} cycles = {(max(allv) + OVERHEAD)/(BUDGET*0.048):.1f} us (limit 20.8 us)")

import subprocess as sp2
A2L = NM.replace("arm-none-eabi-nm", "arm-none-eabi-addr2line")
worst.sort(key=lambda w: -w[0])
fcache = {}
def fn(a):
    if a not in fcache:
        o = sp2.run([A2L, "-e", ELF, "-f", "-C", "-i", "%x" % a], capture_output=True, text=True).stdout.splitlines()
        fcache[a] = o[0].split("(")[0]
    return fcache[a]
print("\nworst samples:")
for c, ph, prof in worst[:4]:
    agg = collections.Counter()
    for a, v in prof.items(): agg[fn(a)] += v
    print(f"  {c} cycles in '{ph}':", ", ".join(f"{k.split('::')[-1]} {v}" for k, v in agg.most_common(8)))
