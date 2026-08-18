#!/usr/bin/env python3
"""patsim.py - model of pattern transfer between the card and the browser.

Mirrors webui.cpp's MSG_PAT_GET/MSG_PAT_DATA/MSG_PAT_SET and the matching
downloadPattern()/uploadPattern() in web/index.html, plus looper.cpp's
SetPatternRaw().

This one exists because pattern transfer spans the ONE seam in this repo with
no compiler across it: the card is C++ checked by tools/syntax.sh, the page is
JavaScript checked only by `node --check`, and nothing verifies that the bytes
one puts on the wire are the bytes the other reads off it. The two sides were
written separately from the same prose description, which is exactly how the
loopsim lane-encoding drift happened (the model stored raw knob values while
looper.cpp stored knob >> 4, invisible until the FX lane used all eight bits).

The properties worth checking are not "does a pattern arrive" but:

  - SysEx payloads must be 7-bit. A single byte above 0x7F is read as a status
    byte and TRUNCATES the message, so the encoding is load-bearing rather
    than cosmetic, and a pattern is full of high bits: ticks reach 767 (two
    bytes), velocities reach 255, and kKnobEvent is the 0x80 flag itself.
  - Send() caps a whole message at 64 bytes and silently DROPS anything
    longer, so the 21-raw-bytes-per-chunk figure has to hold with the header
    and framing included.
  - 2048 bytes is not a multiple of 21, so the last chunk is a short one and
    is where an off-by-one would land.
  - An EMPTY slot must produce a complete reply (header then terminator)
    rather than a silence the browser waits out.
  - SetPatternRaw() is a trust boundary: it takes a JSON file a user can edit
    by hand, while RecallPattern() walks the array assuming it is sorted by
    tick. Unsorted or out-of-range input has to be made safe on the way in,
    because the failure is silent and only appears when that slot is recalled.

Run: python tools/patsim.py
"""

import json
import os
import random
import re

# --- constants, mirroring looper.h / webui.h --------------------------------
kLoopTicks = 768          # kTicksPerBeat * kBeatsPerLoop
kMaxEvents = 512
kNumPatterns = 3
kKnobEvent = 0x80
kNumVoices = 12
MFR = 0x7D
PAT_DATA = 0x41
kRawPerChunk = 21         # SendNextPatternChunk()'s figure
kSendCap = 64             # Send()'s buffer


# --- 7-bit codec, mirroring Encode7bit/Decode7bit in webui.cpp --------------

def encode7bit(src):
    out = []
    for i in range(0, len(src), 7):
        group = src[i:i + 7]
        high = 0
        for k, b in enumerate(group):
            if b & 0x80:
                high |= 1 << k
        out.append(high)
        out.extend(b & 0x7F for b in group)
    return out


def decode7bit(src):
    out = []
    for i in range(0, len(src), 8):
        high = src[i]
        group = src[i + 1:i + 8]
        for k, b in enumerate(group):
            out.append(b | (0x80 if high & (1 << k) else 0))
    return out


# --- the card side ----------------------------------------------------------

def frame(payload):
    """webui.cpp Send(): F0 <mfr> payload F7."""
    return [0xF0, MFR] + list(payload) + [0xF7]


def events_to_bytes(events):
    raw = []
    for e in events:
        raw += [e['tick'] & 0xFF, (e['tick'] >> 8) & 0xFF,
                e['what'] & 0xFF, e['value'] & 0xFF]
    return raw


def card_reply(slot, events, knob_count):
    """MSG_PAT_GET's reply: header, then chunks, then terminator."""
    raw = events_to_bytes(events)
    count = len(events)
    msgs = [frame([PAT_DATA, slot, 0x7F,
                   (count >> 7) & 0x7F, count & 0x7F, knob_count & 0x7F])]
    for off in range(0, len(raw), kRawPerChunk):
        chunk = encode7bit(raw[off:off + kRawPerChunk])
        msgs.append(frame([PAT_DATA, slot, 1] + chunk))
    msgs.append(frame([PAT_DATA, slot, 0]))
    return msgs


# --- the browser side, mirroring downloadPattern() in web/index.html --------

def browser_parse(msgs):
    hdr = msgs[0]
    assert hdr[2] == PAT_DATA and hdr[4] == 0x7F, "header rejected"
    count = (hdr[5] << 7) | hdr[6]
    knob_count = hdr[7]

    data = []
    for d in msgs[1:]:
        assert d[2] == PAT_DATA, "lost sync"
        if d[4] == 0:
            break
        data += decode7bit(d[5:len(d) - 1])

    events = [{'tick': data[i] | (data[i + 1] << 8),
               'what': data[i + 2], 'value': data[i + 3]}
              for i in range(0, len(data) - 3, 4)]
    return {'count': count, 'knobCount': knob_count, 'events': events}


# --- looper.cpp SetPatternRaw() ---------------------------------------------

def set_pattern_raw(slot, incoming, count, knob_count):
    """Returns (events, knobCount) or None if refused."""
    if slot < 0 or slot >= kNumPatterns:
        return None
    if count > kMaxEvents:
        return None

    pat = []
    for k in range(count):
        tick, what, value = incoming[k]
        if tick >= kLoopTicks:
            tick %= kLoopTicks
        ev = (tick, what, value)
        j = len(pat)
        while j > 0 and pat[j - 1][0] > ev[0]:
            j -= 1
        pat.insert(j, ev)
    return pat, min(knob_count, len(pat))


# --- checks -----------------------------------------------------------------

def check(label, cond):
    print(f"  {'PASS' if cond else 'FAIL'}  {label}")
    if not cond:
        raise SystemExit(1)


def make_events(n):
    """A pattern that exercises the high bit in every field."""
    out = []
    for i in range(n):
        what = (kKnobEvent | (i % 4)) if i % 5 == 0 else (i % kNumVoices)
        out.append({'tick': (i * 7) % kLoopTicks,
                    'what': what, 'value': (i * 13) % 256})
    return out


print("pattern transfer: card -> browser")
ev = make_events(377)
got = browser_parse(card_reply(2, ev, 41))
check("377 events survive the wire framing intact", got['events'] == ev)
check("count and knobCount arrive in the header",
      got['count'] == 377 and got['knobCount'] == 41)

full = make_events(kMaxEvents)
got = browser_parse(card_reply(1, full, 200))
check("a full 512 events (2048B, not a multiple of 21) round-trips",
      got['events'] == full)

got = browser_parse(card_reply(0, [], 0))
check("an EMPTY slot replies rather than timing out",
      got['count'] == 0 and got['events'] == [])

msgs = card_reply(1, full, 200)
check("no payload byte exceeds 0x7F (would truncate the message)",
      all(b <= 0x7F for m in msgs for b in m[1:-1]))
check(f"largest message fits Send()'s {kSendCap}B cap",
      max(len(m) for m in msgs) <= kSendCap)

# The chunk size is the one constant that lives in THREE places -- this file,
# SendNextPatternChunk() in webui.cpp, and uploadPattern() in web/index.html --
# and a mismatch is silent: the sender and receiver each stay self-consistent,
# so a round trip inside any one of them still passes. Read the other two out
# of the source rather than trusting them to have been kept in step.
_here = os.path.dirname(os.path.abspath(__file__))
_cpp = open(os.path.join(_here, '..', 'webui.cpp'), encoding='utf-8').read()
_html = open(os.path.join(_here, '..', 'web', 'index.html'), encoding='utf-8').read()

_m = re.search(r'constexpr uint32_t kRaw = (\d+);', _cpp)
check("webui.cpp's chunk size is readable",
      _m is not None)
check(f"webui.cpp sends {kRawPerChunk} raw bytes per chunk",
      _m and int(_m.group(1)) == kRawPerChunk)

_m = re.search(r'off \+= (\d+)\)\s*\{\s*\n\s*const chunk = encode7bit', _html)
check("web/index.html's chunk size is readable", _m is not None)
check(f"web/index.html sends {kRawPerChunk} raw bytes per chunk",
      _m and int(_m.group(1)) == kRawPerChunk)

# 21 is NOT the largest chunk that fits -- the cap is not reached until 44 raw
# bytes -- and webui.cpp's comment used to claim otherwise. What matters is the
# headroom, so assert that rather than a maximality the code does not have.
_worst = max(len(m) for m in msgs)
check(f"the chunk size leaves real headroom ({_worst}B of {kSendCap}B used)",
      _worst <= kSendCap // 2)

# The chunk is a whole number of 7-byte groups, which is why the encoding comes
# out even and the last chunk is the only short one.
check(f"{kRawPerChunk} is a whole number of 7-byte encoding groups",
      kRawPerChunk % 7 == 0)

print("\npattern transfer: browser -> card")
raw = events_to_bytes(full)
rebuilt = []
for off in range(0, len(raw), kRawPerChunk):
    rebuilt += decode7bit(encode7bit(raw[off:off + kRawPerChunk]))
check("upload chunking is lossless over the same 21-byte boundary",
      rebuilt == raw)

# The JSON file itself must survive a save/load cycle unchanged.
doc = json.loads(json.dumps({'card': 'nibble-ko', 'kind': 'pattern', 'slot': 1,
                             'knobCount': 200, 'events': full}))
check("the JSON document round-trips without altering an event",
      doc['events'] == full)

print("\nSetPatternRaw: the trust boundary")
random.seed(7)
src = [(random.randrange(0, 3000), random.randrange(0, kNumVoices),
        random.randrange(0, 256)) for _ in range(kMaxEvents)]
pat, kc = set_pattern_raw(1, src, len(src), 999)
check("reverse/random input comes out sorted by tick",
      all(pat[i][0] <= pat[i + 1][0] for i in range(len(pat) - 1)))
check("every out-of-range tick is wrapped into 0..767",
      all(0 <= e[0] < kLoopTicks for e in pat))
check("no event is lost or altered (compared as a multiset)",
      sorted(pat) == sorted((t % kLoopTicks, w, v) for t, w, v in src))
check("knobCount is clamped to the real event count", kc == len(pat))
check("count > kMaxEvents is refused outright",
      set_pattern_raw(1, [(0, 0, 0)] * 600, 600, 0) is None)
check("a bad slot is refused outright",
      set_pattern_raw(kNumPatterns, [(0, 0, 0)], 1, 0) is None)

# A pattern the card sent must survive being loaded straight back.
back = set_pattern_raw(1, [(e['tick'], e['what'], e['value'])
                           for e in got['events']], 0, 0)
check("an empty download loads back as an empty slot", back == ([], 0))

print("\nall passed")
