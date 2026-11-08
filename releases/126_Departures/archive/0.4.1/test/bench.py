# ARM timing harness adapted from Equanimity tools/bench/cycles.py.
# Copyright (c) 2026 Matt Allison (original); Adrian Vos (adaptation). MIT.
# Uses Unicorn and Capstone. Cycle estimates exclude bus contention and are
# evidence of headroom, not a replacement for physical hardware timing.
# Usage: python3 test/bench.py build-bench/Departures.elf
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

def call(addr, r0=0, r1=0):
    global cycles, pending_fallthrough
    cycles = 0
    pending_fallthrough = None
    mu.reg_write(UC_ARM_REG_SP, 0x20042000)
    mu.reg_write(UC_ARM_REG_R0, r0)
    mu.reg_write(UC_ARM_REG_R1, r1)
    mu.reg_write(UC_ARM_REG_LR, STOP | 1)
    try:
        mu.emu_start(addr | 1, STOP, count=5_000_000)
    except UcError as e:
        print("fault calling %x at pc=%x lr=%x" % (addr, mu.reg_read(UC_ARM_REG_PC), mu.reg_read(UC_ARM_REG_LR)))
        raise
    return cycles


# Lookup tables are generated exactly as firmware startup does, without needing
# to emulate the RP2040 ROM math routines or touch an emulated flash device.
mu.mem_write(sym("departures::Tables::sine"), struct.pack("<1025h", *[
    round(32767 * math.sin(i * 2 * math.pi / 1024)) for i in range(1025)]))
mu.mem_write(sym("departures::Tables::pitch"), struct.pack("<128I", *[
    round(440 * 2**((i-69)/12) * 2**32/48000) for i in range(128)]))
call(sym("departures::Engine::Init(unsigned long)"), sym("engine"), 123)

off = struct.unpack("<11I", mu.mem_read(sym("bench_offsets"), 44))
O_KNOBS, O_PULSE, O_LPULSE, O_CV, O_ADCL, O_ADCR, O_CONN, O_SW, O_LSW, O_DAC, SIZE = off
THIS, VT = 0x20030000, 0x20032000
mu.mem_write(THIS, bytes(SIZE + 1024))
mu.mem_write(VT, struct.pack("<16I", *([STUB | 1] * 16)))
mu.mem_write(THIS, struct.pack("<I", VT + 8))
mu.mem_write(THIS + O_CONN, bytes([1] * 6))
# The real wrapper's default inputs and counters are refreshed before playing.
bf = max(call(sym("ComputerCard::BufferFull()"), THIS) for _ in range(64))
pwm = max(call(sym("ComputerCard::OnCVPWMWrap()")) for _ in range(16))
overhead = bf + 40 + math.ceil((pwm + 40) * 2000 / 48000)
print(f"Estimated ComputerCard + IRQ overhead: {overhead} cycles/sample", flush=True)
ps = sym("DeparturesCard::ProcessSample()")
last = [0, 0]
results = collections.defaultdict(list)
for n in range(100000):
    active = n >= 6000
    # 20Hz input clocks stress all four bell streams and their crossfades;
    # changing scale/destination, full effects and audio input stress DSP paths.
    pulse = [int(active and n % 2500 == 0), int(active and n < 22000 and n % 2600 == 0)]
    cv = [(-2048, 2047, 609, 1097, 365, 1341, 853)[(n // 512) % 7], (-2048, 0, 250, 252, 600, 1200, 2047)[(n // 512) % 7]]
    mu.mem_write(THIS + O_CONN, bytes([1, 1, int(n < 90000), int(n < 22000), 1, 1]))
    mu.mem_write(THIS + O_KNOBS, struct.pack("<4i", 4095, 4095, 4095, 2048))
    mu.mem_write(THIS + O_SW, struct.pack("<ii", 1, 1))
    mu.mem_write(THIS + O_CV, struct.pack("<2i", *cv))
    mu.mem_write(THIS + O_ADCL, struct.pack("<h", 2047 if n & 64 else -2048))
    mu.mem_write(THIS + O_ADCR, struct.pack("<h", (2047 if n & 64 else -2048) if n >= 50000 else (-2048 if n & 64 else 2047)))
    mu.mem_write(THIS + O_PULSE, bytes(pulse))
    mu.mem_write(THIS + O_LPULSE, bytes(last))
    last = pulse
    c = call(ps, THIS)
    results["startup" if not active else "manual changes" if n < 22000 else "automatic changes"].append(c)
for name, counts in results.items():
    total = max(counts) + overhead
    print(f"{name}: max callback {max(counts)}, mean {sum(counts)/len(counts):.0f}; "
          f"with overhead {total}/{int(BUDGET)} ({total/BUDGET:.1%}) cycles")
if max(max(v) for v in results.values()) + overhead >= BUDGET:
    raise SystemExit("FAIL: estimated 48kHz sample budget exceeded")
