# bench_cycles.py : cycle-count the QiGong Timer's per-sample code on an emulated Cortex-M0+.
#
# ADAPTED from tools/bench/cycles.py of the Equanimity card by Matt Allison (MIT licence,
# github.com/TomWhitwell/Workshop_Computer/tree/main/releases/112_Equanimity). The emulation and cycle model
# below are his; the card-specific set-up and the scenarios at the end are ours.
#
# Usage (from the card folder):
#   cmake -S . -B build-bench -DPICO_BOARD=pico -DCARD_BENCH=ON && cmake --build build-bench
#   python3 -m venv venv && venv/bin/pip install unicorn capstone
#   (cd host && make tables)          # the start-up tables (the emulator has no boot-ROM float routines)
#   venv/bin/python scripts/bench_cycles.py build-bench/qi_timer.elf 1.0 3000     (3000 = 144 MHz, 4000 = 192 MHz)
#
# ---- original notes (Equanimity) ----
#
# Why: ProcessSample() runs in ComputerCard's audio interrupt and must
# finish, together with ComputerCard's own work, inside one 48kHz sample.
# Overrunning scrambles the ADC/mux/DAC sequence -- garbled audio. This
# runs the compiled firmware (not a host build) on the Unicorn emulator,
# counts Cortex-M0+ cycles per sample through a scripted performance, and
# reports the worst cases against the budget.
#
# Setup (once):   python3 -m venv venv && venv/bin/pip install unicorn capstone
# Build:          cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DEQUANIMITY_BENCH=ON
#                 cmake --build build-bench
# Run:            venv/bin/python tools/bench/cycles.py build-bench/Equanimity.elf 1.0 4000
#                 (args: ELF, scenario length scale, CPU cycles per sample:
#                  3000 at 144MHz, 4000 at 192MHz)
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
BUDGET = int(sys.argv[3]) if len(sys.argv) > 3 else 4000  # CPU cycles per 48kHz sample (192MHz)
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
mu.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: hook_div(uc, a, s, True), begin=idiv, end=idiv)
if "__wrap___aeabi_uidiv" in syms:   # (only present if the code divides unsigned numbers)
    uidiv = sym("__wrap___aeabi_uidiv")
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

# ---- static constructors (skip the ones that talk to the flash chip: there is none in the emulator)
ia0, ia1 = syms["__init_array_start"], syms["__init_array_end"]
skip = set()
for s in ("_retrieve_unique_id_on_boot", "ProbeFlash()"):
    if s in dem or s in syms: skip.add(sym(s) & ~1)
for p in range(ia0, ia1, 4):
    f, = struct.unpack("<I", mu.mem_read(p, 4))
    if f and (f & ~1) not in skip: call(f & ~1)

# ---- a fake card object with a real, initialised engine inside
off = struct.unpack("<11I", mu.mem_read(sym("bench_offsets"), 44))
O_KNOBS, O_PULSE, O_LPULSE, O_CV, O_ADCL, O_ADCR, O_CONN, O_SW, O_LSW, O_LEDPH, SIZE = off
THIS = 0x20038000
VT = 0x2003F000
assert THIS + SIZE < VT, "card object too big for the fake address"
mu.mem_write(VT, struct.pack("<16I", *([STUB | 1] * 16)))
mu.mem_write(THIS, b"\0" * (SIZE + 64))
mu.mem_write(THIS, struct.pack("<I", VT + 8))
mu.mem_write(THIS + O_CONN, bytes([1, 1, 0, 0, 0, 0]))
# Load the start-up tables computed on the PC (host/out/tables.json from `make tables`): same values the card computes
import json
TABLES = sys.argv[4] if len(sys.argv) > 4 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "host", "out", "tables.json")
fmt = {"cardkit::gSine": "<h", "cardkit::gNoteInc": "<I"}
for name, vals in json.load(open(TABLES)).items():
    if name not in dem and name not in syms: continue          # (a table the firmware never uses is not linked)
    f = fmt.get(name, "<i")
    mu.mem_write(sym(name), b"".join(struct.pack(f, v) for v in vals))
init_cycles = call(sym("bench_init"), THIS)
print(f"engine Init (tables, start-up only): {init_cycles} cycles")

bf = [call(sym("ComputerCard::BufferFull()"), THIS) for _ in range(64)]
pwm = [call(sym("ComputerCard::OnCVPWMWrap()")) for _ in range(16)]
BF, PWM, IRQ = max(bf), max(pwm), 40
PWM_PER_SAMPLE = (BUDGET * 48000.0 / 2000.0) / 48000.0
OVERHEAD = BF + IRQ + int(PWM_PER_SAMPLE * (PWM + IRQ))
print(f"ComputerCard BufferFull (no ProcessSample): max {BF}; CV PWM IRQ {PWM}; fixed overhead per sample ~{OVERHEAD} cycles")
mu.mem_write(THIS, struct.pack("<I", VT + 8))       # (BufferFull may have touched state; keep the stub vtable)

PS = sym("QiTimerCard::ProcessSample()")
B1 = sym("bench_core1")
core1 = []
state = dict(knobs=[2048, 2048, 2048, 0], sw=1, audio=[0, 0], sig="none")
def write_inputs(n):
    sig = state["sig"]
    if sig == "bells":
        t = n / 48000.0
        e1 = math.exp(-(t % 3.0) / 1.5); e2 = math.exp(-(t % 5.0) / 3.0)
        state["audio"] = [int(1200 * e1 * math.sin(2 * math.pi * 880 * t)), int(1300 * e2 * math.sin(2 * math.pi * 147 * t))]
    elif sig == "square":
        v = 2047 if (n // 54) % 2 else -2047
        state["audio"] = [v, -v]
    elif sig == "none":
        state["audio"] = [0, 0]
    mu.mem_write(THIS + O_KNOBS, struct.pack("<4i", *state["knobs"]))
    mu.mem_write(THIS + O_SW, struct.pack("<i", state["sw"]))
    mu.mem_write(THIS + O_LSW, struct.pack("<i", state["sw"]))
    mu.mem_write(THIS + O_ADCL, struct.pack("<h", state["audio"][0]))
    mu.mem_write(THIS + O_ADCR, struct.pack("<h", state["audio"][1]))

results = collections.defaultdict(list)
worst = []
n = 0
def run(phase, count, per_sample=None):
    global n, PROFILE, PROFILING
    for i in range(int(count * SCALE)):
        if per_sample: per_sample(i)
        write_inputs(n)
        PROFILE = {}; PROFILING = True
        c = call(PS, THIS)
        PROFILING = False
        c1 = call(B1, THIS)                    # core 1's step (on the card: in parallel, on the other core)
        if c1 > 200: core1.append(c1)
        worst.append((c, phase, PROFILE))
        if len(worst) > 64:
            worst.sort(key=lambda w: -w[0]); del worst[8:]
        results[phase].append(c)
        n += 1

def tri(i, period):  # 0..4095 triangle
    x = (i % period) / period
    return int(4095 * (2 * x if x < 0.5 else 2 - 2 * x))
def fx_sweep(i):     # Z middle: Main visits the three pages, X and Y sweep (passing every stored value: pickups engage)
    seg = (i // 16000) % 3
    state["knobs"][0] = [150, 2048, 3950][seg]
    state["knobs"][1] = tri(i, 16000); state["knobs"][2] = 4095 - tri(i, 16000)
def fx_max(i):       # then park every effect at maximum
    seg = (i // 16000) % 3
    state["knobs"][0] = [150, 2048, 3950][seg]
    x = tri(i % 16000, 16000) if (i % 16000) < 8000 else 4095
    state["knobs"][1] = x; state["knobs"][2] = x                       # (all at maximum; reverb page Y = brightness)
def setup_turns(i):  # Z up: X flips between zones, Main sweeps (notch arithmetic, speech timing)
    state["sw"] = 2
    state["knobs"][1] = 200 if (i // 12000) % 2 == 0 else 3900
    state["knobs"][0] = tri(i, 9000); state["knobs"][2] = tri(i, 7000)
def setup_clicks(i):  # Z up, all three knobs moving fast: clicks + Rule A LED display every few ms
    state["sw"] = 2
    state["knobs"][0] = tri(i, 4000); state["knobs"][1] = tri(i, 5000); state["knobs"][2] = tri(i, 3000)
def preview(i):      # triple tap, then preview mode running (pulses + keeper alerts every second)
    p = i % 48000
    state["sw"] = 0 if (i < 30000 and (i % 10000) < 4800) else 1
def taps(i):         # start, then pause/resume taps, with a hold (LED hold display)
    state["sw"] = 0 if (i % 24000) < 4800 else 1

run("boot guard", 6000)
run("idle, silent", 9600)
state["sig"] = "bells"
run("chime + bell into both inputs, default effects", 48000)
run("sweeping effect pages and knobs", 48000, fx_sweep)
run("every effect at maximum, sweeping", 48000, fx_max)
state["sig"] = "square"
state["knobs"][0] = 1200
run("full-scale square, everything at maximum", 48000)
state["sig"] = "bells"
run("set-up mode: turning knobs", 48000, setup_turns)
run("set-up mode: all three knobs racing (clicks)", 48000, setup_clicks)
state["sw"] = 1; state["knobs"] = [1200, 2048, 2048, 0]
run("triple tap -> preview mode", 96000, preview)
state["sw"] = 1; state["knobs"] = [1200, 2048, 2048, 0]
run("session: start, pause/resume taps", 96000, taps)
state["sw"] = 0
run("holding Z down (stop, hold display)", 150000)
state["sw"] = 1

print(f"\nper-sample ProcessSample cycles (budget {BUDGET} total, {BUDGET - OVERHEAD} after overhead):")
for phase, v in results.items():
    s = sorted(v)
    over = sum(1 for c in v if c + OVERHEAD > BUDGET)
    print(f"  {phase:48s} mean {sum(v)/len(v):6.0f}  p99 {s[int(len(s)*0.99)-1]:6d}  max {s[-1]:6d}  over budget {over}/{len(v)}")
allv = [c for v in results.values() for c in v]
worst_total = max(allv) + OVERHEAD
mean_total = sum(allv) / len(allv) + OVERHEAD
print(f"  worst total incl. overhead: {worst_total} cycles = {100*worst_total/BUDGET:.0f}% of the {BUDGET}-cycle budget; mean {100*mean_total/BUDGET:.0f}%")
if core1:
    print(f"  core 1 (one Control step per 32 samples; its budget is 32 x {BUDGET} = {32*BUDGET}): mean {sum(core1)/len(core1):.0f}, max {max(core1)} cycles = {100*max(core1)/(32*BUDGET):.1f}%")

A2L = NM.replace("arm-none-eabi-nm", "arm-none-eabi-addr2line")
fcache = {}
def fn(a):
    if a not in fcache:
        o = subprocess.run([A2L, "-e", ELF, "-f", "-C", "-i", "%x" % a], capture_output=True, text=True).stdout.splitlines()
        fcache[a] = o[0].split("(")[0] if o else "?"
    return fcache[a]
worst.sort(key=lambda w: -w[0])
print("\nworst samples:")
for c, ph, prof in worst[:4]:
    agg = collections.Counter()
    for a, v in prof.items(): agg[fn(a)] += v
    print(f"  {c} cycles in '{ph}':", ", ".join(f"{k.split('::')[-1]} {v}" for k, v in agg.most_common(6)))
