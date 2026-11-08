# GPL-3.0 experiment timing driver, adapted from Matt Allison's Temperance
# tools/bench/cycles.py (MIT; see ../../LICENSES/Temperance-MIT.txt).
# Estimates core-0 cycles; does not simulate real RAM/flash bus contention.
# Cycle-count the real firmware's per-sample code on an emulated Cortex-M0+.
# (From Recurrence, which took it from Hubris, Constancy and Equanimity.)
#
# Why: ProcessSample() runs in ComputerCard's audio interrupt and must
# finish, together with ComputerCard's own work, inside one 48kHz sample.
# Overrunning scrambles the ADC/mux/DAC sequence -- garbled audio. This
# runs the compiled firmware (not a host build) on the Unicorn emulator,
# counts Cortex-M0+ cycles per sample through a scripted performance, and
# reports the worst cases against the budget.
#
# Setup (once):   python3 -m venv venv && venv/bin/pip install unicorn capstone
# Build:          cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DTEMPERANCE_BENCH=ON
#                 cmake --build build-bench
# Run:            venv/bin/python tools/bench/cycles.py build-bench/temperance0.1.0.16mb.elf 1.0 4000
#                 (args: ELF, legacy scale argument, CPU cycles per sample;
#                  scenario lengths are fixed; use 4000 at 192MHz)
# The fixture is emulator-only: do not flash its UF2.
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
for base, size in [(0x0, 0x1000), (0x10000000, 0x1000000), (0x20000000, 0x42000),
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

# __builtin_ctz goes to the bootrom's fast bit functions (pico_bit_ops),
# which aren't in the emulator: do it here, at a ROM-like cost.
CTZ_COST = 12
def hook_ctz(uc, address, size, _):
    global cycles, pending_fallthrough
    a = uc.reg_read(UC_ARM_REG_R0)
    r = 32
    if a:
        r = 0
        while not (a >> r) & 1:
            r += 1
    uc.reg_write(UC_ARM_REG_R0, r)
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    cycles += CTZ_COST
    pending_fallthrough = None
if "__wrap___ctzsi2" in syms:
    ctz = sym("__wrap___ctzsi2")
    mu.hook_add(UC_HOOK_CODE, hook_ctz, begin=ctz, end=ctz)

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
    if mu.reg_read(UC_ARM_REG_PC) != STOP:
        raise RuntimeError("emulator call did not return within instruction limit")
    return cycles

# ---- static constructors
ia0, ia1 = syms["__init_array_start"], syms["__init_array_end"]
for p in range(ia0, ia1, 4):
    fn, = struct.unpack("<I", mu.mem_read(p, 4))
    # Skip the ones that talk to the flash chip (unique ID, and ComputerCard
    # 0.4's flash-size probe): there's no flash chip in the emulator.
    if fn and (fn & ~1) not in (sym("_retrieve_unique_id_on_boot"), sym("ProbeFlash()")): call(fn & ~1)

# ---- fake card object
off = struct.unpack("<11I", mu.mem_read(sym("bench_offsets"), 44))
O_KNOBS, O_PULSE, O_LPULSE, O_CV, O_ADCL, O_ADCR, O_CONN, O_SW, O_LSW, O_DAC, SIZE = off
THIS = 0x20038000
VT = 0x20039000
mu.mem_write(VT, struct.pack("<16I", *([STUB | 1] * 16)))
mu.mem_write(THIS, b"\0" * (SIZE + 64))
mu.mem_write(THIS, struct.pack("<I", VT + 8))
mu.mem_write(THIS + O_CONN, bytes([0, 0, 0, 0, 0, 0]))

norm_offset, = struct.unpack("<I", mu.mem_read(sym("bench_norm_offset"), 4))
mu.mem_write(THIS + norm_offset, b"\x01")

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

PS = sym("TemperanceCard::ProcessSample()")

# The panel: knobs, switch, and every jack Temperance reads.
state = dict(knobs=[4095, 2389, 2389, 2048], sw=1, pulse=[0, 0], last=[0, 0], cv=[0, 0], audio=[0, 0],
             conn=[0, 0, 0, 0, 0, 0])
def write_inputs(n):
    mu.mem_write(THIS + O_KNOBS, struct.pack("<4i", *state["knobs"]))
    mu.mem_write(THIS + O_SW, struct.pack("<ii", state["sw"], state["sw"]))
    mu.mem_write(THIS + O_PULSE, bytes(state["pulse"]))
    mu.mem_write(THIS + O_LPULSE, bytes(state["last"]))
    mu.mem_write(THIS + O_CV, struct.pack("<2i", *state["cv"]))
    mu.mem_write(THIS + O_ADCL, struct.pack("<h", state["audio"][0]))
    mu.mem_write(THIS + O_ADCR, struct.pack("<h", state["audio"][1]))
    mu.mem_write(THIS + O_CONN, bytes(state["conn"]))


# Fill simulated flash with independently decodable nonzero ADPCM pages.
mu.mem_write(0x10080000, (b"\0" * 4 + b"\x77" * 252) * ((0x1000000 - 0x80000) // 256))
SERVICE = sym("bench_service")
PREPARE = sym("bench_prepare")
labels = ["both forward x2", "A half, B x2", "A reverse x2, B x2", "direction crossfades"]
failed = False
for scenario in range(8):
    label = labels[scenario & 3] + (" at seam" if scenario & 4 else " mid-loop")
    call(PREPARE, scenario)
    values = []
    worst_profile = {}
    worst_cost = -1
    for i in range(1200):
        write_inputs(i)
        PROFILE = {}
        PROFILING = True
        sample_cost = call(PS, THIS)
        PROFILING = False
        values.append(sample_cost)
        if sample_cost > worst_cost:
            worst_cost, worst_profile = sample_cost, PROFILE
        call(SERVICE)
    # Include initial cache loading above and the warmed speed-change path below by
    # re-preparing after caches have filled, then measuring its first 256 ticks.
    call(PREPARE, scenario)
    for i in range(512):
        write_inputs(i)
        PROFILE = {}
        PROFILING = True
        sample_cost = call(PS, THIS)
        PROFILING = False
        values.append(sample_cost)
        if sample_cost > worst_cost:
            worst_cost, worst_profile = sample_cost, PROFILE
        call(SERVICE)
    total = max(values) + OVERHEAD
    print(f"{label}: max ProcessSample {max(values)}; including overhead {total} cycles ({100*total/BUDGET:.1f}%), {total / 192:.2f}us", flush=True)
    import bisect
    names = sorted((address, name) for name, address in dem.items() if 0x20000000 <= address < 0x20010000)
    starts = [x[0] for x in names]
    aggregate = collections.Counter()
    for address, cost in worst_profile.items():
        index = bisect.bisect_right(starts, address) - 1
        aggregate[names[index][1]] += cost
    print("  worst profile:", aggregate.most_common(6), flush=True)
    if total >= BUDGET * 0.85:
        print("FAIL: less than 15% modeled timing margin", flush=True)
        failed = True
sys.exit(1 if failed else 0)
