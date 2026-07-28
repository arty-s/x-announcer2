#!/usr/bin/env python3
"""Diff the settings v2 ships with against the ones 1.x ships with.

A default that drifts is the quietest way for the two implementations to part
company: the scenarios are flown through both, so if v2 quietly decided that,
say, boarding_repeat is 180 seconds, every scenario would still pass - both
sides would simply be flying a different aeroplane than the user's.

    python tests/diff_config_vs_1x.py [--x1 DIR] [--xa-test PATH]

1.x is read straight out of its `cfg = { ... }` table rather than executed:
booting the Lua plugin needs a simulator stub, and this question is about the
literals in the source. v2 answers by printing the file a first run would write.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess

DEFAULT_X1 = r"D:\claude2\x-announcer"
DEFAULT_XA_TEST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               "build", "xa_test.exe")

# Keys 1.x has that v2 has no code for yet. They are not failures - they are the
# to-do list - but they are printed every run so the list cannot be forgotten.
NOT_PORTED = {"simbrief_id", "widget", "widget_mode", "widget_opacity", "widget_x", "widget_y"}

# Keys 1.x has that v2 deliberately does NOT have.
DROPPED = {
    "auto_find": "hunted for D:\\UA_Sounds and two other drives - one person's "
                 "folder name baked into everybody's install. v2 uses the "
                 "Sound_packs folder beside the plugin when library is empty",
}

# Differences that are meant. Anything not named here has to match exactly.
# Keys v2 has and 1.x never needed.
V2_ONLY = {
    "panel_open": "1.x never reopened its window by itself; v2 has to remember, "
                  "or it either forces the panel open or forgets it entirely",
}

DELIBERATE = {
    "music_bus": "1.x names FlyWithLua's mixer buses (master); the XPLM has no "
                 "master bus, so v2 uses the SDK's exterior - the bus music "
                 "already played on before the file existed",
    "library": "1.x defaults to a Sounds folder beside the script; v2 leaves it "
               "empty, which means the Sound_packs folder beside the plugin - so "
               "the setting keeps working when the plugin folder is moved",
}


def read_1x_defaults(x1_root: str) -> dict[str, str]:
    """Pull the literals out of `local cfg = { ... }` in x_announcer.lua."""
    source = open(os.path.join(x1_root, "x_announcer.lua"), encoding="utf-8").read()
    start = source.index("local cfg = {")
    depth = 0
    for end in range(start, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                break
    body = source[start:end]

    values: dict[str, str] = {}
    for line in body.splitlines()[1:]:
        line = re.sub(r"--.*$", "", line).strip().rstrip(",").strip()
        if not line or "=" not in line:
            continue
        key, _, raw = line.partition("=")
        raw = raw.strip()
        if raw.startswith('"') and raw.endswith('"'):
            raw = raw[1:-1]
        values[key.strip()] = raw
    return values


def read_v2_defaults(xa_test: str) -> dict[str, str]:
    out = subprocess.run([xa_test, "--dump-settings"], capture_output=True, text=True,
                         encoding="utf-8", check=True).stdout
    values: dict[str, str] = {}
    for line in out.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, raw = line.partition("=")
        values[key.strip()] = raw.strip()
    return values


def same_number(a: str, b: str) -> bool:
    try:
        return float(a) == float(b)
    except ValueError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--x1", default=DEFAULT_X1)
    parser.add_argument("--xa-test", default=DEFAULT_XA_TEST)
    args = parser.parse_args()

    one = read_1x_defaults(args.x1)
    two = read_v2_defaults(args.xa_test)
    print(f"1.x: {len(one)} keys, v2: {len(two)} keys")

    problems = []

    for key, want in sorted(one.items()):
        if key in NOT_PORTED:
            continue
        if key in DROPPED:
            print(f"  - {key}: dropped on purpose - {DROPPED[key]}")
            continue
        if key not in two:
            problems.append(f"  {key}: 1.x has it ({want!r}), v2 does not")
            continue
        got = two[key]
        if got == want or same_number(got, want):
            continue
        if key in DELIBERATE:
            print(f"  ~ {key}: 1.x {want!r} vs v2 {got!r}")
            print(f"      on purpose: {DELIBERATE[key]}")
            continue
        problems.append(f"  {key}: 1.x {want!r} vs v2 {got!r}")

    for key in sorted(set(two) - set(one)):
        if key in V2_ONLY:
            print(f"  + {key}: v2 only - {V2_ONLY[key]}")
            continue
        problems.append(f"  {key}: v2 has it ({two[key]!r}), 1.x does not")

    missing = sorted(k for k in NOT_PORTED if k in one)
    if missing:
        print(f"  still to port: {', '.join(missing)}")

    print()
    if problems:
        print("settings disagree:")
        print("\n".join(problems))
        return 1
    compared = len(set(one) & set(two)) - len(DELIBERATE)
    print(f"every shared default matches ({compared} keys compared, "
          f"{len(DELIBERATE)} deliberate differences, {len(DROPPED)} dropped, "
          f"{len(missing)} not ported yet)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
