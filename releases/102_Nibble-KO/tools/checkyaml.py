#!/usr/bin/env python3
"""checkyaml.py - validate info.yaml before it reaches the card registry.

info.yaml is prose-heavy, and prose contains colons. An unquoted `KEYS: the
thing` inside a description is a YAML mapping, not text, and the file stops
parsing - which has now happened twice while editing descriptions. The registry
would reject it and the only symptom here is a stack trace at publish time.

Checks structure as well as syntax, because a file that parses but is missing a
socket id is just as broken from the registry's point of view.

Run:  python tools/checkyaml.py
"""

import sys

try:
    import yaml
except ImportError:
    print("checkyaml: pyyaml not installed - SKIPPED")
    sys.exit(0)

REQUIRED_TOP = ["Name", "Version", "License", "Creator", "repository",
                "short-description", "summary", "panel", "controls"]

VALID_INPUTS = {"CVIn1", "CVIn2", "AudioIn1", "AudioIn2", "PulseIn1", "PulseIn2"}
VALID_OUTPUTS = {"CVOut1", "CVOut2", "AudioOut1", "AudioOut2",
                 "PulseOut1", "PulseOut2"}
VALID_TYPES = {"cv", "audio", "pulse"}

errors = []


def check(cond, msg):
    if not cond:
        errors.append(msg)


def main():
    path = "info.yaml"
    try:
        with open(path, encoding="utf-8") as f:
            doc = yaml.safe_load(f)
    except yaml.YAMLError as e:
        # The overwhelmingly likely cause, so say so rather than just re-raising.
        print("checkyaml: %s DOES NOT PARSE" % path)
        print()
        print(e)
        print()
        print("Most likely an unquoted colon inside a description. Wrap the")
        print('whole value in "double quotes", or use " - " instead of ": ".')
        return 1

    for k in REQUIRED_TOP:
        check(k in doc, "missing top-level key: %s" % k)

    if "panel" in doc:
        for kind, valid in (("inputs", VALID_INPUTS), ("outputs", VALID_OUTPUTS)):
            for sock in doc["panel"].get(kind, []):
                sid = sock.get("id")
                check(sid in valid, "%s: unknown socket id %r" % (kind, sid))
                check(sock.get("type") in VALID_TYPES,
                      "%s %s: bad type %r" % (kind, sid, sock.get("type")))
                check(bool(sock.get("description", "").strip()),
                      "%s %s: empty description" % (kind, sid))

    if "controls" in doc:
        sw = doc["controls"].get("switch", {})
        for pos in ("up", "middle", "down", "tap"):
            check(pos in sw, "controls.switch: missing %r" % pos)
        knobs = doc["controls"].get("knobs", [{}])[0]
        for k in ("main", "x", "y"):
            check(k in knobs, "controls.knobs: missing %r" % k)

    # The version appears in two places and they diverge silently.
    ver = str(doc.get("Version", ""))
    for entry in doc.get("uf2", []):
        name = entry.get("name", "")
        check(ver in name,
              "uf2 name %r does not mention Version %r" % (name, ver))

    if errors:
        print("checkyaml: %d problem(s)" % len(errors))
        for e in errors:
            print("  - %s" % e)
        return 1

    print("checkyaml: info.yaml OK (v%s)" % ver)
    return 0


if __name__ == "__main__":
    sys.exit(main())
