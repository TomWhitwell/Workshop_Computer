#!/usr/bin/env python3
"""Talk to a blackbird debug build (make debug) over SWD, while it runs.

The firmware keeps a mailbox in RAM (bb_dbg in main.cpp). This tool reads and
writes it through OpenOCD's memory commands, which access RAM over the debug
port without halting the cores. Python standard library only.

Transports:
  gdb  (default) OpenOCD's GDB server, e.g. host.docker.internal:3333. Commands
       go through `monitor`. OpenOCD may halt the target when a GDB client
       attaches; this tool resumes it straight away, but see docs/DEBUG_HARNESS.md.
  tcl  OpenOCD's Tcl RPC server (port 6667). Never halts the target.

Examples:
  util/bbdbg.py stats
  util/bbdbg.py stats --watch 1
  util/bbdbg.py eval "return input[1].volts"
  util/bbdbg.py run my_script.lua
  util/bbdbg.py repl "^^k"
  util/bbdbg.py input cv1=2.5 pulse1=square:100hz knob_main=0.5 switch=up
  util/bbdbg.py input --clear
  util/bbdbg.py log --follow
  util/bbdbg.py test                       # runs tests/test_*.py
  util/bbdbg.py test tests/test_stress.py -k lfo --repeat 3
  NORNS_HOST=192.168.1.20 util/bbdbg.py norns "return norns.crow.connected()"
"""
import argparse
import importlib.util
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
DEFAULT_ELF = os.path.join(PROJECT, "build-debug", "blackbird_debug.elf")
DEFAULT_HOST = os.environ.get("OPENOCD_HOST", "host.docker.internal")

# ---------------------------------------------------------------------------
# Mailbox layout: keep in step with bb_dbg_t in main.cpp (static_asserts there)
# ---------------------------------------------------------------------------
MAGIC = 0x47444242
VERSION = 14
STAT_FIELDS = [
    "magic", "version", "struct_size", "sample_rate_hz",
    "heartbeat", "uptime_ms", "ps_worst_us", "ps_overruns", "ps_budget_us",
    "loop_worst_us", "loop_iterations", "lua_mem_bytes", "lua_mem_peak_bytes",
    "drops_metro", "drops_clock", "drops_input", "drops_asl_done",
    "drops_slope_action", "drops_slope_cmd", "drops_usb_rx",
    "heap_free_bytes", "lua_alloc_failures", "drops_usb_tx",
    "out_changes_1", "out_changes_2", "out_changes_3", "out_changes_4",
    "drops_midi_echo", "pool_pages", "pool_slot_bytes", "pool_capacity_bytes",
    "pool_arena_free_pages", "core1_isr_cycles", "core1_isr_count", "core1_isr_max_cycles",
    "core1_idle_cycles", "slope_underruns", "slope_actions", "cv_pwm_wraps",
]
OFF_CMD_SEQ, OFF_CMD, OFF_ACK_SEQ, OFF_STATUS = 160, 164, 168, 172
OFF_SCRIPT_LEN, OFF_OUT_LEN, OFF_OUT_TRUNCATED, OFF_LOG_HEAD = 176, 180, 184, 188
OFF_INPUTS = 192
INPUT_STRIDE = 24                       # mode, low, high, period, width, phase
OFF_CONNECTED_MASK = OFF_INPUTS + 10 * INPUT_STRIDE
OFF_CONNECTED_VALUE = OFF_CONNECTED_MASK + 4
SCRIPT_MAX, OUT_MAX, LOG_MAX = 2048, 1024, 2048
OFF_SCRIPT = OFF_INPUTS + 10 * INPUT_STRIDE + 8
OFF_OUT = OFF_SCRIPT + SCRIPT_MAX
OFF_LOG = OFF_OUT + OUT_MAX
ALLOC_BUCKETS = 24
OFF_ALLOC = OFF_LOG + LOG_MAX          # alloc_live, alloc_live_bytes, alloc_total
OFF_LOOP_SECTIONS = OFF_ALLOC + 3 * 4 * ALLOC_BUCKETS
# MainControlLoop sections, as marked with LOOP_SECTION(n) in main.cpp
LOOP_SECTIONS = ["usb poll (tud_task, rx, midi)", "midi echo", "usb rx -> repl", "harness mailbox",
                 "leds, welcome, perf warning", "log msgs, slope callbacks, detect, input values",
                 "clock events", "metro events", "input events, change callbacks", "asl done events",
                 "switch, clock source, pulse clocks", "bb.asap", "pulse-in callbacks", "ii leader",
                 "pubview", "(end)"]
STRUCT_SIZE = OFF_LOOP_SECTIONS + 4 * len(LOOP_SECTIONS)
ALLOC_BUCKET_LABELS = [f"{8 * i + 1}-{8 * (i + 1)}" for i in range(16)] + [
    "129-192", "193-256", "257-512", "513-1K", "1K-2K", "2K-4K", ">4K", "-"]

CMD_EVAL, CMD_REPL, CMD_RESET_STATS = 1, 2, 3
STATUS_NAMES = {0: "ok", 1: "load error", 2: "runtime error", 3: "bad command", 4: "no lua"}

# ComputerCard::OverrideInput order, and native-unit conversion for each
INPUTS = ["audio1", "audio2", "cv1", "cv2", "pulse1", "pulse2",
          "knob_main", "knob_x", "knob_y", "switch"]
JACKS = ["audio1", "audio2", "cv1", "cv2", "pulse1", "pulse2"]  # ComputerCard::Input order
MODE_OFF, MODE_CONSTANT, MODE_SQUARE, MODE_TRIANGLE = 0, 1, 2, 3
SWITCH_POSITIONS = {"down": 0, "middle": 1, "up": 2}
COUNTS_PER_VOLT = 2047.0 / 6.0          # matches blackbird's 6/2047 volts per count


def to_native(name, value):
    """Convert volts (audio/cv), 0..1 (knobs), bool (pulse) or name (switch)."""
    if name in ("audio1", "audio2", "cv1", "cv2"):
        return max(-2048, min(2047, round(float(value) * COUNTS_PER_VOLT)))
    if name in ("pulse1", "pulse2"):
        return 1 if value in (True, 1, "1", "high", "on", "true") else 0
    if name.startswith("knob_"):
        return max(0, min(4095, round(float(value) * 4095)))
    if name == "switch":
        return SWITCH_POSITIONS[value] if isinstance(value, str) else int(value)
    raise ValueError(f"unknown input {name}")


class HarnessError(Exception):
    pass


class Skip(Exception):
    """Raise from a test to skip it, with the reason as the message."""


# ---------------------------------------------------------------------------
# Transports: both expose monitor(cmd) -> str (OpenOCD command output)
# ---------------------------------------------------------------------------
def connect(host, port, timeout):
    sock = socket.create_connection((host, port), timeout=timeout)
    # Small request/response packets: without this, Nagle + delayed ACK add ~40ms each
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


class GdbTransport:
    """Minimal GDB remote serial protocol client, enough for `monitor`."""

    def __init__(self, host, port, timeout=5.0, resume=True):
        self.sock = connect(host, port, timeout)
        self.buf = b""
        self.sock.sendall(b"+")
        if resume:
            # OpenOCD halts the target on GDB attach unless configured not to
            # (gdb-attach event). Resume at once; harmless if already running.
            self.monitor("resume")

    REPLY_TIMEOUT = 30.0

    def _read(self):
        # OpenOCD occasionally stalls for several seconds: keep waiting (see
        # TclTransport.monitor) rather than lose the reply and desync
        deadline = time.monotonic() + self.REPLY_TIMEOUT
        while True:
            try:
                chunk = self.sock.recv(4096)
                break
            except socket.timeout:
                if time.monotonic() >= deadline:
                    raise HarnessError(f"OpenOCD didn't answer within {self.REPLY_TIMEOUT:.0f}s")
        if not chunk:
            raise HarnessError("OpenOCD closed the GDB connection")
        self.buf += chunk

    def _send_packet(self, data):
        payload = data.encode("latin-1")
        csum = sum(payload) & 0xFF
        self.sock.sendall(b"$" + payload + b"#" + f"{csum:02x}".encode())
        while True:  # wait for the ack
            while not self.buf:
                self._read()
            c, self.buf = self.buf[:1], self.buf[1:]
            if c == b"+":
                return
            if c == b"-":
                self.sock.sendall(b"$" + payload + b"#" + f"{csum:02x}".encode())

    def _recv_packet(self):
        while True:
            start = self.buf.find(b"$")
            if start >= 0:
                end = self.buf.find(b"#", start)
                if end >= 0 and len(self.buf) >= end + 3:
                    data = self.buf[start + 1:end]
                    self.buf = self.buf[end + 3:]
                    self.sock.sendall(b"+")
                    return data.decode("latin-1")
            self._read()

    def monitor(self, cmd):
        self._send_packet("qRcmd," + cmd.encode().hex())
        out = []
        while True:
            reply = self._recv_packet()
            if reply == "OK":
                return "".join(out)
            if reply.startswith("O") and reply != "OK":
                out.append(bytes.fromhex(reply[1:]).decode("latin-1", "replace"))
            elif reply.startswith("E"):
                raise HarnessError(f"monitor {cmd!r} failed: {reply} {''.join(out)}")
            elif reply == "":
                raise HarnessError("OpenOCD does not support monitor commands")
            else:  # hex-encoded final output (some servers)
                try:
                    out.append(bytes.fromhex(reply).decode("latin-1", "replace"))
                except ValueError:
                    pass
                return "".join(out)

    def close(self):
        try:
            self._send_packet("D")  # detach: OpenOCD resumes the target
            self._recv_packet()
        except Exception:
            pass
        self.sock.close()


class TclTransport:
    """OpenOCD Tcl RPC server: commands terminated by 0x1a."""

    def __init__(self, host, port, timeout=5.0):
        self.sock = connect(host, port, timeout)
        self.buf = b""

    REPLY_TIMEOUT = 30.0

    def monitor(self, cmd):
        self.sock.sendall(cmd.encode() + b"\x1a")
        # OpenOCD occasionally stalls for several seconds (its SWD adapter is
        # shared over USB). Keep waiting rather than give up: a reply arriving
        # after we'd given up would be read as the answer to the next command.
        deadline = time.monotonic() + self.REPLY_TIMEOUT
        while b"\x1a" not in self.buf:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                if time.monotonic() < deadline:
                    continue
                raise HarnessError(f"OpenOCD didn't answer {cmd[:40]!r} within {self.REPLY_TIMEOUT:.0f}s")
            if not chunk:
                raise HarnessError("OpenOCD closed the Tcl connection")
            self.buf += chunk
        reply, self.buf = self.buf.split(b"\x1a", 1)
        return reply.decode("latin-1", "replace")

    def close(self):
        self.sock.close()


# ---------------------------------------------------------------------------
# Memory access on top of a transport
# ---------------------------------------------------------------------------
HEX_LINE = re.compile(r"^\s*(0x[0-9a-fA-F]+\s*)+$")


class Memory:
    READ_CHUNK = 512   # words per command
    WRITE_CHUNK = 128

    def __init__(self, transport):
        self.t = transport
        self.legacy = None  # True if read_memory/write_memory are missing (old OpenOCD)

    def read_words(self, addr, count):
        words = []
        while count > 0:
            n = min(count, self.READ_CHUNK)
            words += self._read_chunk(addr, n)
            addr += 4 * n
            count -= n
        return words

    def _read_chunk(self, addr, n):
        if not self.legacy:
            out = self.t.monitor(f"read_memory 0x{addr:08x} 32 {n}")
            if "invalid command" not in out:
                self.legacy = False
                vals = []
                for line in out.splitlines():
                    if HEX_LINE.match(line):
                        vals += [int(v, 16) for v in line.split()]
                if len(vals) == n:
                    return vals
                raise HarnessError(f"read_memory returned {len(vals)} of {n} words: {out[:200]!r}")
            self.legacy = True
        out = self.t.monitor(f"mdw 0x{addr:08x} {n}")  # "0x20000000: 11111111 22222222 ..."
        vals = []
        for line in out.splitlines():
            m = re.match(r"^\s*0x[0-9a-fA-F]+:\s*(.*)$", line)
            if m:
                vals += [int(v, 16) for v in m.group(1).split()]
        if len(vals) < n:
            raise HarnessError(f"mdw returned {len(vals)} of {n} words: {out[:200]!r}")
        return vals[:n]

    def write_words(self, addr, words):
        for i in range(0, len(words), self.WRITE_CHUNK):
            chunk = words[i:i + self.WRITE_CHUNK]
            a = addr + 4 * i
            if not self.legacy:
                out = self.t.monitor(f"write_memory 0x{a:08x} 32 {{{' '.join(f'0x{w:08x}' for w in chunk)}}}")
                if "invalid command" not in out:
                    self.legacy = False
                    continue
                self.legacy = True
            for j, w in enumerate(chunk):
                self.t.monitor(f"mww 0x{a + 4 * j:08x} 0x{w:08x}")

    def read_u32(self, addr):
        return self.read_words(addr, 1)[0]

    def write_u32(self, addr, value):
        self.write_words(addr, [value & 0xFFFFFFFF])

    def read_bytes(self, addr, length):
        start = addr & ~3
        words = self.read_words(start, (addr + length - start + 3) // 4)
        data = struct.pack(f"<{len(words)}I", *words)
        return data[addr - start:addr - start + length]

    def write_bytes(self, addr, data):
        assert addr % 4 == 0
        data = bytes(data) + b"\0" * (-len(data) % 4)
        self.write_words(addr, list(struct.unpack(f"<{len(data) // 4}I", data)))


def addr2line(elf, addrs):
    """'function file:line' for each address, best effort."""
    try:
        out = subprocess.run(["arm-none-eabi-addr2line", "-f", "-C", "-e", elf] + [hex(a) for a in addrs],
                             capture_output=True, text=True).stdout.splitlines()
        return [f"{out[i]} {os.path.basename(out[i + 1])}" for i in range(0, len(out) - 1, 2)]
    except OSError:
        return []


def find_symbol(elf, name="bb_dbg"):
    try:
        out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True, check=True).stdout
    except (FileNotFoundError, subprocess.CalledProcessError) as e:
        raise HarnessError(f"can't read symbols from {elf}: {e}")
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise HarnessError(f"{name} not found in {elf}: is it a debug build (make debug)?")


# ---------------------------------------------------------------------------
# The card
# ---------------------------------------------------------------------------
class Blackbird:
    def __init__(self, transport="gdb", host=DEFAULT_HOST, port=None, elf=DEFAULT_ELF,
                 addr=None, resume=True):
        if transport == "gdb":
            self.t = GdbTransport(host, port or 3333, resume=resume)
        elif transport == "tcl":
            self.t = TclTransport(host, port or 6667)
        else:
            raise ValueError(transport)
        self.mem = Memory(self.t)
        self.elf = elf
        self.base = addr if addr is not None else find_symbol(elf)
        self._check_mailbox()
        self.log_head = self.mem.read_u32(self.base + OFF_LOG_HEAD)

    def close(self):
        self.t.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _check_mailbox(self):
        magic, version, size = self.mem.read_words(self.base, 3)
        if magic != MAGIC:
            raise HarnessError(f"no mailbox at 0x{self.base:08x} (read 0x{magic:08x}): wrong ELF, "
                               "not a debug build, or the firmware hasn't started yet")
        if version != VERSION or size != STRUCT_SIZE:
            raise HarnessError(f"mailbox v{version}/{size} bytes, tool expects v{VERSION}/{STRUCT_SIZE}: "
                               "rebuild, or update util/bbdbg.py to match main.cpp")

    def symbol(self, name):
        """Address of a firmware symbol, for reading other state directly via self.mem."""
        return find_symbol(self.elf, name)

    # --- stats -------------------------------------------------------------
    def alloc_histogram(self):
        """Lua allocations by size bucket: list of (label, live, live_bytes, total)."""
        w = self.mem.read_words(self.base + OFF_ALLOC, 3 * ALLOC_BUCKETS)
        live, live_bytes, total = w[:ALLOC_BUCKETS], w[ALLOC_BUCKETS:2 * ALLOC_BUCKETS], w[2 * ALLOC_BUCKETS:]
        return [(ALLOC_BUCKET_LABELS[i], live[i], live_bytes[i], total[i]) for i in range(ALLOC_BUCKETS - 1)]

    def loop_sections(self):
        """Worst time per main loop section since reset_stats(): {name: us}."""
        w = self.mem.read_words(self.base + OFF_LOOP_SECTIONS, len(LOOP_SECTIONS))
        return dict(zip(LOOP_SECTIONS, w))

    def stats(self):
        """Current stats. Drop counters (drops_*) count from the last
        reset_stats(), like the worst-case figures do."""
        st = dict(zip(STAT_FIELDS, self.mem.read_words(self.base, len(STAT_FIELDS))))
        for k, base in getattr(self, "_drop_base", {}).items():
            st[k] = (st[k] - base) & 0xFFFFFFFF
        return st

    def reset_stats(self):
        self._command(CMD_RESET_STATS)
        raw = dict(zip(STAT_FIELDS, self.mem.read_words(self.base, len(STAT_FIELDS))))
        self._drop_base = {k: v for k, v in raw.items() if k.startswith("drops_")}

    # --- commands ----------------------------------------------------------
    def _command(self, cmd, script=b"", timeout=5.0):
        if len(script) > SCRIPT_MAX:
            raise HarnessError(f"script is {len(script)} bytes, max {SCRIPT_MAX}")
        seq, _, ack = self.mem.read_words(self.base + OFF_CMD_SEQ, 3)
        if seq != ack:
            self._wait_ack(seq, timeout)  # a previous command (another client?) is still running
        if script:
            self.mem.write_bytes(self.base + OFF_SCRIPT, script)
        self.mem.write_words(self.base + OFF_CMD, [cmd])
        self.mem.write_words(self.base + OFF_SCRIPT_LEN, [len(script)])
        seq = (seq + 1) & 0xFFFFFFFF
        self.mem.write_u32(self.base + OFF_CMD_SEQ, seq)  # submit last
        self._wait_ack(seq, timeout)
        status, _, out_len, truncated = self.mem.read_words(self.base + OFF_STATUS, 4)
        out = self.mem.read_bytes(self.base + OFF_OUT, out_len).decode("utf-8", "replace") if out_len else ""
        if truncated:
            out += "\n[output truncated]"
        return status, out

    FAULT_MAGIC = 0xFA017ED0

    def faults(self):
        """HardFaults captured by the firmware's handler (g_fault_info), per core:
        a list of dicts with pc/lr/sp/xpsr and their source locations."""
        try:
            addr = self.symbol("g_fault_info")
        except HarnessError:
            return []
        words = self.mem.read_words(addr, 22)
        found = []
        for core in (0, 1):
            f = words[core * 11:(core + 1) * 11]
            if f[0] == self.FAULT_MAGIC:
                info = dict(core=core, pc=f[1], lr=f[2], xpsr=f[3], sp=f[4], icsr=f[10])
                info["where"] = addr2line(self.elf, [f[1], f[2] & ~1])
                found.append(info)
        return found

    CPU_HZ = 192_000_000

    def load(self, seconds=3.0):
        """Measure CPU load over `seconds` (debug builds with COMPUTERCARD_PROFILE).
        Returns a dict: core1 busy %, audio ISR % and its average/max time,
        core1 background work % (output rendering, timers, clock), core0 main
        loop rate."""
        a = self.stats()
        time.sleep(seconds)
        b = self.stats()
        d = lambda k: (b[k] - a[k]) & 0xFFFFFFFF
        cycles = d("uptime_ms") / 1000 * self.CPU_HZ
        isr = d("core1_isr_cycles") / cycles
        busy = 1 - d("core1_idle_cycles") / cycles
        n = max(1, d("core1_isr_count"))
        return dict(core1_busy_pct=round(100 * busy, 1), isr_pct=round(100 * isr, 1),
                    slope_underruns_per_s=round(d("slope_underruns") / (d("uptime_ms") / 1000), 1),
                    background_pct=round(100 * (busy - isr), 1),
                    isr_avg_us=round(d("core1_isr_cycles") / n / self.CPU_HZ * 1e6, 2),
                    isr_max_us=round(b["core1_isr_max_cycles"] / self.CPU_HZ * 1e6, 1),
                    isr_rate_hz=round(d("core1_isr_count") / (d("uptime_ms") / 1000)),
                    core0_loop_hz=round(d("heartbeat") / (d("uptime_ms") / 1000)))

    def heap_map(self):
        """Walk newlib's malloc heap over SWD. Returns a dict: used_chunks,
        free_chunks (list of sizes, largest first), free_bytes, top_bytes (free
        space at the end, including heap not yet sbrk'd) and largest_free.
        Fragmentation shows as free_bytes spread over many small chunks."""
        sbrk_base = self.mem.read_u32(self.symbol("__malloc_sbrk_base"))
        top = self.mem.read_u32(self.symbol("__malloc_av_") + 8)  # av_[2]
        limit = self.symbol("__HeapLimit")
        first = (sbrk_base + 7) & ~7
        data = self.mem.read_bytes(first, top + 8 - first)
        chunks, off = [], 0
        while first + off < top:
            size = struct.unpack_from("<I", data, off + 4)[0] & ~3
            if size < 8 or first + off + size > top:
                raise HarnessError(f"heap walk: bad chunk at 0x{first + off:08x} (size {size})")
            chunks.append((off, size))
            off += size
        free = []
        used = 0
        for k, (off, size) in enumerate(chunks):
            next_off = chunks[k + 1][0] if k + 1 < len(chunks) else top - first
            if struct.unpack_from("<I", data, next_off + 4)[0] & 1:
                used += 1
            else:
                free.append(size)
        top_size = self.mem.read_u32(top + 4) & ~3
        top_bytes = top_size + (limit - (top + top_size))
        free.sort(reverse=True)
        return dict(used_chunks=used, free_chunks=free, free_bytes=sum(free), top_bytes=top_bytes,
                    largest_free=max([top_bytes] + free[:1]))

    def halted_cores(self):
        """Names of halted cores. A reset (e.g. the card's button) with OpenOCD
        attached can leave them halted, which looks like a stuck main loop."""
        try:
            names = self.t.monitor("target names").split()
            return [n for n in names if self.t.monitor(f"{n} curstate").strip() == "halted"]
        except HarnessError:
            return []

    def _wait_ack(self, seq, timeout):
        deadline = time.monotonic() + timeout
        while self.mem.read_u32(self.base + OFF_ACK_SEQ) != seq:
            if time.monotonic() > deadline:
                beat = self.mem.read_u32(self.base + 16)
                halted = self.halted_cores()
                if halted:
                    raise HarnessError(f"command {seq} not acknowledged: {', '.join(halted)} halted "
                                       "(was the card reset with OpenOCD attached? resume with "
                                       "`monitor resume`, or reflash)")
                raise HarnessError(f"command {seq} not acknowledged within {timeout}s "
                                   f"(heartbeat {beat}: is the main loop stuck?)")
            time.sleep(0.005)

    def eval(self, code, timeout=5.0, check=True):
        """Run a Lua chunk on the card. Returns its print() output and results."""
        status, out = self._command(CMD_EVAL, code.encode(), timeout)
        if check and status != 0:
            raise HarnessError(f"lua {STATUS_NAMES.get(status, status)}: {out.strip()}")
        return out

    def query(self, expr, timeout=5.0):
        """Evaluate a Lua expression and return it as a Python value (number, bool, nil->None, str)."""
        out = self.eval("return " + expr, timeout).rstrip("\n")
        if out == "nil":
            return None
        if out in ("true", "false"):
            return out == "true"
        try:
            return int(out) if re.fullmatch(r"-?\d+", out) else float(out)
        except ValueError:
            return out

    def repl(self, text, timeout=10.0):
        """Send text through the USB REPL path, exactly as druid would (64-byte packets).
        Returns the USB output it produced. Send '^^s'-style commands in their own call."""
        head = self.mem.read_u32(self.base + OFF_LOG_HEAD)
        status, _ = self._command(CMD_REPL, text.encode(), timeout)
        if status != 0:
            raise HarnessError(f"repl: {STATUS_NAMES.get(status, status)}")
        out, _ = self.read_log(head)
        return out

    def upload(self, script, timeout=20.0):
        """Run a script the way druid's `r` does: ^^s, the script, ^^e (which runs it
        and calls init()). Returns the USB output; '^^ready()' in it means it loaded."""
        if not script.endswith("\n"):
            script += "\n"  # else the last line would merge with ^^e
        out = self.repl("^^s")
        chunk = ""
        for line in script.splitlines(keepends=True):
            if len(chunk) + len(line) > SCRIPT_MAX // 2:
                out += self.repl(chunk, timeout)
                chunk = ""
            chunk += line
        if chunk:
            out += self.repl(chunk, timeout)
        return out + self.repl("^^e", timeout)

    def kill_lua(self):
        """^^k: stop metros/clocks/slopes and clear user globals, as druid does."""
        self.repl("^^k")

    # --- USB output log ----------------------------------------------------
    def read_log(self, since=None):
        """USB CDC output written since `since` (a log_head value). Returns (text, new_head)."""
        head = self.mem.read_u32(self.base + OFF_LOG_HEAD)
        since = self.log_head if since is None else since
        n = (head - since) & 0xFFFFFFFF
        lost = max(0, n - LOG_MAX)
        n = min(n, LOG_MAX)
        start = (head - n) % LOG_MAX
        if start + n <= LOG_MAX:
            data = self.mem.read_bytes(self.base + OFF_LOG + start, n)
        else:
            first = LOG_MAX - start
            data = (self.mem.read_bytes(self.base + OFF_LOG + start, first)
                    + self.mem.read_bytes(self.base + OFF_LOG, n - first))
        text = data.decode("utf-8", "replace")
        if lost:
            text = f"[{lost} bytes of log lost]\n" + text
        self.log_head = head
        return text, head

    # --- inputs ------------------------------------------------------------
    def _write_override(self, name, mode, low=0, high=0, period=0, width=0):
        i = INPUTS.index(name)
        addr = self.base + OFF_INPUTS + i * INPUT_STRIDE
        # Disable first so the ISR never runs a half-written generator
        self.mem.write_u32(addr, MODE_OFF)
        self.mem.write_words(addr + 4, [v & 0xFFFFFFFF for v in (low, high, period, width, 0)])
        self.mem.write_u32(addr, mode)

    def set_input(self, name, value):
        """Hold an input at a value: volts (audio/cv), 0..1 (knobs), bool (pulse), 'up' etc (switch)."""
        self._write_override(name, MODE_CONSTANT, to_native(name, value))

    def square(self, name, hz, low=0, high=1, duty=0.5):
        """Square wave / pulse train on an input, generated sample-accurately on the card."""
        period = max(2, round(self._rate() / hz))
        width = max(1, min(period - 1, round(period * duty)))
        self._write_override(name, MODE_SQUARE, to_native(name, low), to_native(name, high), period, width)

    def triangle(self, name, hz, low, high):
        """Triangle sweep between low and high on an input."""
        period = max(2, round(self._rate() / hz))
        self._write_override(name, MODE_TRIANGLE, to_native(name, low), to_native(name, high), period)

    def release_input(self, name):
        self._write_override(name, MODE_OFF)

    def set_connected(self, **jacks):
        """Force Connected() per jack, e.g. set_connected(pulse1=False). None = stop forcing."""
        mask, value = self.mem.read_words(self.base + OFF_CONNECTED_MASK, 2)
        for name, state in jacks.items():
            bit = 1 << JACKS.index(name)
            if state is None:
                mask &= ~bit
            else:
                mask |= bit
                value = (value | bit) if state else (value & ~bit)
        self.mem.write_words(self.base + OFF_CONNECTED_MASK, [mask, value])

    def clear_inputs(self):
        for name in INPUTS:
            self.mem.write_u32(self.base + OFF_INPUTS + INPUTS.index(name) * INPUT_STRIDE, MODE_OFF)
        self.mem.write_words(self.base + OFF_CONNECTED_MASK, [0, 0])

    def _rate(self):
        if not hasattr(self, "_sample_rate"):
            self._sample_rate = self.mem.read_u32(self.base + 12)
        return self._sample_rate

    # --- helpers for tests --------------------------------------------------
    def wait(self, seconds):
        time.sleep(seconds)

    def reset(self):
        """Fresh state for a test: release inputs, ^^k, zero the counters, skip old log."""
        self.clear_inputs()
        self.kill_lua()
        self.reset_stats()
        self.read_log()


# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------
HEALTH_ERRORS = re.compile(r"lua (runtime|load) error|lua panic|BLACKBIRD CRASH|Out of memory|not enough memory",
                           re.IGNORECASE)


class TestContext:
    """Passed to each test as `bb`; a Blackbird plus expectations the runner checks after."""

    def __init__(self, card):
        self.card = card
        self.allow_overruns = 0
        self.allow_log_errors = False
        self.info = {}  # extra facts a test wants in the report

    def __getattr__(self, name):
        return getattr(self.card, name)


def discover(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += sorted(os.path.join(p, f) for f in os.listdir(p)
                            if f.startswith("test_") and f.endswith(".py"))
        else:
            files.append(p)
    tests = []
    for f in files:
        spec = importlib.util.spec_from_file_location(os.path.splitext(os.path.basename(f))[0], f)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for name in sorted(dir(mod)):
            fn = getattr(mod, name)
            if name.startswith("test_") and callable(fn):
                tests.append((f"{mod.__name__}::{name}", fn))
    return tests


def card_crashed(card):
    """True if the card's main loop has stopped (crash, panic, halted cores)."""
    try:
        if card.halted_cores():
            return True
        a = card.stats()["heartbeat"]
        time.sleep(0.3)
        return card.stats()["heartbeat"] == a
    except (HarnessError, OSError):
        return True


def reset_card(card, boot_seconds=10):
    """Reset the card through OpenOCD and wait until it has really rebooted:
    RAM survives a reset, so the old mailbox would still look valid. Plain
    `reset run` can leave the RP2040 in its boot ROM; `reset init` first
    (as flashing does) boots reliably. Note: this re-enumerates USB, so a
    connected norns needs the cable replugging afterwards."""
    card.t.monitor("reset init")
    card.t.monitor("reset run")
    deadline = time.monotonic() + boot_seconds + 15
    time.sleep(boot_seconds)
    while time.monotonic() < deadline:
        for name in card.halted_cores():  # a reset can leave the cores halted
            card.t.monitor(f"targets {name}; resume")
        try:
            card._check_mailbox()
            a = card.stats()
            time.sleep(0.5)
            b = card.stats()
            if b["heartbeat"] != a["heartbeat"] and b["uptime_ms"] < (boot_seconds + 15) * 1000:
                card.log_head = card.mem.read_u32(card.base + OFF_LOG_HEAD)
                return
        except HarnessError:
            pass
        time.sleep(1)
    raise HarnessError("card didn't come back after reset")


def run_tests(card, paths, keyword=None, repeat=1, allow_flash=False, report=None, reset_on_crash=False):
    tests = discover(paths)
    if keyword:
        tests = [t for t in tests if keyword in t[0]]
    results = []
    for rnd in range(repeat):
        for name, fn in tests:
            if getattr(fn, "writes_flash", False) and not allow_flash:
                print(f"SKIP  {name} (writes flash; pass --allow-flash)")
                results.append({"test": name, "result": "skip"})
                continue
            ctx = TestContext(card)
            t0 = time.monotonic()
            error = None
            try:
                card.reset()
                beat0 = card.stats()["heartbeat"]
                fn(ctx)
                st = card.stats()
                log, _ = card.read_log()
                problems = []
                for f in card.faults():
                    problems.append(f"HardFault on core {f['core']}: pc 0x{f['pc']:08x} lr 0x{f['lr']:08x} "
                                    f"sp 0x{f['sp']:08x}  ({' <- '.join(f['where'])})")
                if st["ps_overruns"] > ctx.allow_overruns:
                    problems.append(f"{st['ps_overruns']} ProcessSample overruns (worst {st['ps_worst_us']}us)")
                if st["heartbeat"] == beat0:
                    time.sleep(0.1)  # a very short test can finish between two iterations
                    if card.stats()["heartbeat"] == beat0:
                        problems.append("main loop heartbeat stalled")
                if not ctx.allow_log_errors and HEALTH_ERRORS.search(log):
                    problems.append("errors in USB output:\n    " + "\n    ".join(
                        l for l in log.splitlines() if HEALTH_ERRORS.search(l)))
                if problems:
                    error = "; ".join(problems)
            except Skip as e:
                print(f"SKIP  {name} ({e})")
                results.append({"test": name, "round": rnd, "result": "skip", "reason": str(e)})
                continue
            except AssertionError as e:
                error = f"assertion failed: {e or traceback.format_exc(limit=-1).strip()}"
                st = card.stats()
            except HarnessError as e:
                error = f"harness: {e}"
                st = {}
            except Exception:
                error = traceback.format_exc()
                st = {}
            dt = time.monotonic() - t0
            crashed = bool(error) and reset_on_crash and card_crashed(card)
            xfail = getattr(fn, "xfail", None)
            if crashed:
                outcome = "crash"
            elif xfail:
                outcome = "xfail" if error else "xpass"
            else:
                outcome = "fail" if error else "pass"
            res = {"test": name, "round": rnd, "result": outcome, "seconds": round(dt, 2),
                   "ps_worst_us": st.get("ps_worst_us"), "ps_overruns": st.get("ps_overruns"),
                   "loop_worst_us": st.get("loop_worst_us"), "lua_mem_peak_bytes": st.get("lua_mem_peak_bytes")}
            if error:
                res["error"] = error
            if xfail:
                res["expected_issue"] = xfail
            res.update(getattr(ctx, "info", {}))
            results.append(res)
            print(f"{outcome.upper():5} {name}  {dt:.1f}s  ps_worst={st.get('ps_worst_us')}us "
                  f"overruns={st.get('ps_overruns')} loop_worst={st.get('loop_worst_us')}us "
                  f"lua_peak={(st.get('lua_mem_peak_bytes') or 0) // 1024}KB")
            if xfail:
                print(f"      expected issue: {xfail}")
            if error:
                print("      " + error.replace("\n", "\n      "))
            for k, v in getattr(ctx, "info", {}).items():
                print(f"      {k}: {v}")
            if crashed:
                print("      card crashed: resetting it")
                reset_card(card)
    card.clear_inputs()
    counts = {k: sum(r["result"] == k for r in results) for k in ("pass", "fail", "crash", "xfail", "xpass", "skip")}
    print("\n" + ", ".join(f"{v} {k}" for k, v in counts.items() if v or k in ("pass", "fail")))
    failed = counts["fail"] + counts["crash"]
    if report:
        with open(report, "w") as f:
            json.dump(results, f, indent=2)
    return failed == 0


# Generic fake inputs for running arbitrary scripts: clocks on CV 1 and the
# pulse inputs, a sweep on CV 2, knobs part-way. (kind, input, argument)
GENERIC_STIMULUS = [
    ("square", "cv1", dict(hz=2, low=0, high=5, duty=0.5)),
    ("triangle", "cv2", dict(hz=0.2, low=-3, high=3)),
    ("square", "pulse1", dict(hz=4, duty=0.2)),
    ("square", "pulse2", dict(hz=1, duty=0.2)),
    ("triangle", "audio1", dict(hz=1, low=-2, high=2)),
    ("set", "knob_main", 0.5),
    ("set", "knob_x", 0.3),
    ("set", "knob_y", 0.7),
    ("set", "switch", "middle"),
]


def apply_stimulus(card, entries):
    for kind, name, arg in entries:
        if kind == "set":
            card.set_input(name, arg)
        elif kind == "square":
            card.square(name, **arg)
        elif kind == "triangle":
            card.triangle(name, **arg)
        elif kind == "off":
            card.release_input(name)


def outputs_moving(card, seconds):
    """Wait `seconds`; the outputs whose value changed meanwhile, with how many
    times. Counted on the card at main loop rate, so short pulses count too."""
    keys = [f"out_changes_{i}" for i in range(1, 5)]
    before = card.stats()
    time.sleep(seconds)
    after = card.stats()
    changes = [(after[k] - before[k]) & 0xFFFFFFFF for k in keys]
    return {i + 1: n for i, n in enumerate(changes) if n}


def writes_flash(fn):
    """Decorator: test writes the card's flash; only runs with --allow-flash."""
    fn.writes_flash = True
    return fn


def expected_failure(reason):
    """Decorator: a known issue. Reported as XFAIL rather than failing the run;
    XPASS if it starts passing, so the entry can be removed."""
    def mark(fn):
        fn.xfail = reason
        return fn
    return mark


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def parse_input_spec(card, spec):
    """name=value | name=square:<hz>hz[:low:high] | name=tri:<hz>hz:low:high | name=off"""
    name, _, value = spec.partition("=")
    if name not in INPUTS:
        raise SystemExit(f"unknown input {name!r}; one of {', '.join(INPUTS)}")
    parts = value.split(":")
    kind = parts[0]
    if kind == "off":
        card.release_input(name)
    elif kind in ("square", "tri"):
        hz = float(parts[1].lower().rstrip("hz"))
        rest = parts[2:]
        if kind == "square":
            low, high = (rest + [None, None])[:2]
            card.square(name, hz, low if low is not None else 0, high if high is not None else 1)
        else:
            card.triangle(name, hz, rest[0], rest[1])
    else:
        card.set_input(name, value)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--transport", choices=["gdb", "tcl"], default=os.environ.get("BBDBG_TRANSPORT", "gdb"))
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, help="default 3333 (gdb) or 6667 (tcl)")
    ap.add_argument("--elf", default=DEFAULT_ELF)
    ap.add_argument("--addr", type=lambda s: int(s, 0), help="bb_dbg address, instead of reading the ELF")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("stats"); p.add_argument("--watch", type=float, metavar="SECONDS")
    sub.add_parser("reset-stats")
    p = sub.add_parser("eval"); p.add_argument("code")
    p = sub.add_parser("run"); p.add_argument("file")
    p = sub.add_parser("repl"); p.add_argument("text")
    p = sub.add_parser("log"); p.add_argument("--follow", action="store_true")
    p = sub.add_parser("input"); p.add_argument("specs", nargs="*"); p.add_argument("--clear", action="store_true")
    p = sub.add_parser("norns", help="run Lua on norns via matron's REPL (NORNS_HOST)")
    p.add_argument("code")
    p = sub.add_parser("test")
    p.add_argument("paths", nargs="*", default=[os.path.join(PROJECT, "tests")])
    p.add_argument("-k", dest="keyword")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--allow-flash", action="store_true", help="also run tests that write flash")
    p.add_argument("--report", help="write JSON results here")
    p.add_argument("--reset-on-crash", action="store_true",
                   help="if a test leaves the card crashed, record CRASH, reset it and carry on")
    args = ap.parse_args()

    if args.cmd == "norns":  # doesn't need the card
        from nornsws import Norns, NornsError
        try:
            with Norns() as n:
                print(n.run(args.code))
        except NornsError as e:
            sys.exit(f"bbdbg: {e}")
        return

    try:
        with Blackbird(args.transport, args.host, args.port, args.elf, args.addr) as card:
            if args.cmd == "stats":
                while True:
                    st = card.stats()
                    print("  ".join(f"{k}={v}" for k, v in st.items() if k not in ("magic", "version", "struct_size")))
                    if not args.watch:
                        break
                    time.sleep(args.watch)
            elif args.cmd == "reset-stats":
                card.reset_stats()
            elif args.cmd == "eval":
                sys.stdout.write(card.eval(args.code))
            elif args.cmd == "run":
                with open(args.file) as f:
                    sys.stdout.write(card.eval(f.read()))
            elif args.cmd == "repl":
                sys.stdout.write(card.repl(args.text.encode().decode("unicode_escape")))
            elif args.cmd == "log":
                card.log_head = (card.log_head - LOG_MAX) & 0xFFFFFFFF  # show what's in the ring
                while True:
                    text, _ = card.read_log()
                    sys.stdout.write(text)
                    sys.stdout.flush()
                    if not args.follow:
                        break
                    time.sleep(0.2)
            elif args.cmd == "input":
                if args.clear:
                    card.clear_inputs()
                for spec in args.specs:
                    parse_input_spec(card, spec)
            elif args.cmd == "test":
                sys.modules.setdefault("bbdbg", sys.modules[__name__])
                ok = run_tests(card, args.paths, args.keyword, args.repeat, args.allow_flash, args.report,
                               args.reset_on_crash)
                sys.exit(0 if ok else 1)
    except (HarnessError, OSError) as e:
        sys.exit(f"bbdbg: {e}")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
