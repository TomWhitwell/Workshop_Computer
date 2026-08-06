#!/usr/bin/env python3
"""Backfill a `date:` field into each release card's info.yaml.

Uses Phil's git-blame pattern: run `git blame` on the card's own info.yaml and
take the OLDEST surviving per-line author date. Bulk edits rewrite most lines to
a recent date, but original lines keep their real dates, so the minimum blame
date is a bulk-edit-resistant estimate of when the card's metadata first existed.

This is a single, uniform method that works for every card that has an
info.yaml, including cards with no .uf2 firmware (Python / external-link /
placeholder stubs).

Dry-run by default. Pass --apply to write changes. Existing `date:` values are
preserved and those cards are skipped.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASES = os.path.join(ROOT, "releases")

DATE_RE = re.compile(r"\d{4}-\d{2}-\d{2}")
# Existing top-level date key (schema treats hyphens/spaces as equivalent and
# maps `releasedate` -> date).
HAS_DATE_RE = re.compile(r"(?i)^(date|release[\s-]?date)\s*:")
# A top-level key line (starts at column 0, no leading whitespace).
TOP_KEY_RE = re.compile(r"^([A-Za-z][\w -]*?)\s*:(.*)$")


def oldest_blame_date(rel_info_path):
    """Return the oldest author date (YYYY-MM-DD) across all lines of a file."""
    try:
        out = subprocess.run(
            ["git", "blame", "--date=short", "-l", rel_info_path],
            cwd=ROOT, check=True, capture_output=True, text=True,
        ).stdout
    except subprocess.CalledProcessError as e:
        print(f"  ! git blame failed for {rel_info_path}: {e.stderr.strip()}",
              file=sys.stderr)
        return None
    dates = DATE_RE.findall(out)
    return min(dates) if dates else None


def insertion_index(lines):
    """Choose where to insert the date line.

    Only ever anchors on a TOP-LEVEL (column 0), single-line scalar key so we
    never split a nested block or a folded/literal (`>`/`|`) block scalar.
    Prefers just after a top-level `License:`; otherwise after the last simple
    top-level scalar in the opening core block (stopping at the first blank line
    or the first top-level block opener such as `panel:`/`tags:`)."""
    last_scalar = None
    for i, line in enumerate(lines):
        stripped = line.rstrip("\n")
        if stripped.strip() == "" or stripped.lstrip().startswith("#"):
            if last_scalar is not None:
                break  # blank/comment ends the opening core block
            continue
        m = TOP_KEY_RE.match(stripped)
        if not m:
            # Indented line (block content) — end of the top-level core block.
            if last_scalar is not None:
                break
            continue
        key, value = m.group(1), m.group(2).strip()
        # Block openers: empty value (mapping/sequence) or folded/literal scalar.
        if value == "" or value.startswith(">") or value.startswith("|"):
            break
        last_scalar = i
        if key.strip().lower() == "license":
            return i + 1
    if last_scalar is not None:
        return last_scalar + 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true",
                    help="write changes (default: dry run)")
    args = ap.parse_args()

    folders = sorted(
        d for d in os.listdir(RELEASES)
        if os.path.isfile(os.path.join(RELEASES, d, "info.yaml"))
    )

    planned = 0
    skipped = 0
    for folder in folders:
        info_path = os.path.join(RELEASES, folder, "info.yaml")
        rel = os.path.relpath(info_path, ROOT)
        with open(info_path, "r", encoding="utf-8") as f:
            text = f.read()
        lines = text.splitlines(keepends=True)

        if any(HAS_DATE_RE.match(l) for l in lines):
            existing = next(l.strip() for l in lines if HAS_DATE_RE.match(l))
            print(f"skip  {folder:<24} (has {existing})")
            skipped += 1
            continue

        date = oldest_blame_date(rel)
        if not date:
            print(f"skip  {folder:<24} (no blame date)")
            skipped += 1
            continue

        idx = insertion_index(lines)
        # Ensure the line we insert after ends with a newline.
        if idx > 0 and not lines[idx - 1].endswith("\n"):
            lines[idx - 1] += "\n"
        date_line = f"date: {date}\n"
        print(f"add   {folder:<24} date: {date}")
        planned += 1

        if args.apply:
            lines.insert(idx, date_line)
            with open(info_path, "w", encoding="utf-8") as f:
                f.write("".join(lines))

    print(f"\n{'APPLIED' if args.apply else 'DRY RUN'}: "
          f"{planned} to add, {skipped} skipped, {len(folders)} total")
    if not args.apply and planned:
        print("Re-run with --apply to write these changes.")


if __name__ == "__main__":
    main()
