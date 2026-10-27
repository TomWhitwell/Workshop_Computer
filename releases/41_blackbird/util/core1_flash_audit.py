#!/usr/bin/env python3
"""Check that everything core 1 (the audio core) can run lives in RAM.

Core 0 erases/programs the user script flash while core 1 keeps running, so
core 1 must never execute from, or read data from, flash (XIP) in steady state.

This walks the static call graph in the ELF from core 1's entry points and
reports:
  FLASH CODE  a reachable function that lives in flash
  FLASH DATA  a reachable function whose literal pool points at flash data
              (const tables, strings, switch jump tables)
  INDIRECT    an indirect call the walk can't follow; check the targets by
              hand and add them to ROOTS
Function addresses in literal pools (e.g. pointers compared but not called)
are listed as notes only.

Usage: python3 util/core1_flash_audit.py [build/blackbird.elf]
Exits non-zero if any FLASH CODE / FLASH DATA is found.
"""
import re
import subprocess
import sys

ROOTS = [
    # IRQ handlers installed on core 1 by ComputerCard::AudioWorker()
    "ComputerCard::AudioCallback()",
    "ComputerCard::OnCVPWMWrap()",
    # core 1 main loop, and the targets of its indirect calls
    "ComputerCard::AudioWorker()",
    "ComputerCard::BufferFull()",
    "BlackbirdCrow::ProcessSample()",          # virtual, called by BufferFull
    "blackbird_core1_background_service()",   # core1_background_hook
    "Metro_bang",                              # timer callback, via Timer_Process_Block
]

# SDK float/mem/bit helpers end in an indirect jump through the boot ROM's
# function table. ROM isn't flash, so these are fine.
ROM_TABLE_TAILCALLS = re.compile(r"^(__wrap___|__aeabi_i2f_main$|fix2float$|float2int$)")

# Calls that only happen while core 1 is setting up (before any flash write can
# happen) or tearing down in Abort(), which blackbird never uses.
SETUP_ONLY = {
    "dma_claim_unused_channel", "dma_channel_cleanup",
    "irq_set_enabled", "irq_set_exclusive_handler", "irq_set_priority",
    "irq_remove_handler",
}


def in_flash(addr):
    return 0x10000000 <= addr < 0x12000000


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else "build/blackbird.elf"

    nm = subprocess.run(["arm-none-eabi-nm", "-C", "-S", "--defined-only", elf],
                        capture_output=True, text=True, check=True).stdout
    code_syms = {}
    all_syms = []
    for line in nm.splitlines():
        p = line.split(None, 3)
        if len(p) != 4:
            continue
        addr, size, kind, name = int(p[0], 16), int(p[1], 16), p[2], p[3]
        all_syms.append((addr, size, name))
        if kind in "tTwW":
            code_syms.setdefault(addr & ~1, name)
    all_syms.sort()

    def name_of(addr):
        for a, s, n in all_syms:
            if a <= addr < a + max(s, 1):
                return n
        return "?"

    dis = subprocess.run(["arm-none-eabi-objdump", "-d", "-C", "--no-show-raw-insn", elf],
                         capture_output=True, text=True, check=True).stdout
    hdr = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
    branch = re.compile(r"\b(?:bl|blx|b|beq|bne|bcs|bcc|bhi|bls|bge|blt|bgt|ble|bmi|bpl|cbz|cbnz)"
                        r"(?:\.[nw])?\s+[0-9a-f]+ <(.+?)(?:\+0x[0-9a-f]+)?>")
    indirect = re.compile(r"\bblx\s+r\d|\bbx\s+r(?:[0-9]|1[0-2])\b|\bmov\s+pc,")
    word = re.compile(r"\.word\s+0x([0-9a-f]+)")

    funcs = {}  # name -> dict(addr, calls, words, indirect)
    cur = None
    for line in dis.splitlines():
        m = hdr.match(line)
        if m:
            cur = m.group(2)
            funcs[cur] = {"addr": int(m.group(1), 16), "calls": set(), "words": set(), "ind": []}
            continue
        if cur is None:
            continue
        f = funcs[cur]
        m = branch.search(line)
        if m and m.group(1) != cur:
            f["calls"].add(m.group(1))
        if indirect.search(line):
            f["ind"].append(line.strip())
        m = word.search(line)
        if m:
            v = int(m.group(1), 16)
            if "_veneer" in cur and (v & ~1) in code_syms:
                f["calls"].add(code_syms[v & ~1])  # follow long-branch veneers
            elif in_flash(v):
                f["words"].add(v)

    for r in ROOTS:
        if r not in funcs:
            print(f"warning: root not found: {r}")

    parent, seen, stack = {}, set(), list(ROOTS)
    code, data, notes, indirects = [], [], [], []
    while stack:
        name = stack.pop()
        if name in seen or name not in funcs:
            continue
        seen.add(name)
        f = funcs[name]
        if in_flash(f["addr"]) and "_veneer" not in name:
            code.append(name)
        for v in sorted(f["words"]):
            (notes if (v & ~1) in code_syms else data).append((name, v))
        if not ROM_TABLE_TAILCALLS.match(name):
            for ins in f["ind"]:
                indirects.append((name, ins))
        for c in f["calls"]:
            base = c.split("(")[0]
            if base in SETUP_ONLY or base.lstrip("_").replace("_veneer", "") in SETUP_ONLY:
                continue
            if c not in seen:
                parent.setdefault(c, name)
                stack.append(c)

    def chain(name):
        out = [name]
        while out[-1] in parent and len(out) < 10:
            out.append(parent[out[-1]])
        return " <- ".join(out)

    for name in sorted(code):
        print(f"FLASH CODE  {chain(name)}")
    for name, v in data:
        print(f"FLASH DATA  {name}: 0x{v:08x} {name_of(v)}")
    for name, ins in indirects:
        print(f"INDIRECT    {name}: {ins}")
    for name, v in notes:
        print(f"note        {name} takes the address of {code_syms[v & ~1]}")
    print(f"\n{len(seen)} functions reachable from core 1: "
          f"{len(code)} in flash, {len(data)} flash data refs")
    return 1 if code or data else 0


if __name__ == "__main__":
    sys.exit(main())
