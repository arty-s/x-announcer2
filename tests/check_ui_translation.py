"""Every string the core can put on the checklist must have a Russian panel label.

The checklist deliberately speaks English inside the core: those exact strings
are what the differential run against 1.x compares through, so the panel
translates on the way to the screen instead. That split has one failure mode -
somebody adds a condition to the engine and the panel quietly shows it in
English, in the middle of a Russian window. The plugin does say so in the log,
but only once the flight has reached that phase, which may be never.

So the two lists are compared here, statically, from the source:

  * every `yes("...")` label and every `nextPhaseLabel` return in
    core/engine.cpp
  * every key of the translation table in plugin/ui/main_window.cpp

A key with no core string is reported too - that is a label that was renamed in
the engine and left behind in the panel, where it does nothing and misleads the
next reader into thinking it is covered.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "src" / "core" / "engine.cpp"
PANEL = ROOT / "src" / "plugin" / "ui" / "main_window.cpp"


def core_strings(text):
    labels = set(re.findall(r'yes\("([^"]+)"', text))

    # nextPhaseLabel's body only - the file has other switch statements over the
    # same enum, and their strings are not what the heading shows.
    body = re.search(r"nextPhaseLabel\([^)]*\)\s*const\s*\{(.*?)\n\}", text, re.S)
    if not body:
        raise SystemExit("cannot find nextPhaseLabel in engine.cpp - update this check")
    for value in re.findall(r'return\s+"([^"]+)"', body.group(1)):
        if value != "-":
            labels.add(value)
    return labels


def table_keys(text):
    body = re.search(r"kTable\s*=\s*\{(.*?)\n    \};", text, re.S)
    if not body:
        raise SystemExit("cannot find kTable in main_window.cpp - update this check")
    return set(re.findall(r'\{"([^"]+)",', body.group(1)))


def main():
    wanted = core_strings(ENGINE.read_text(encoding="utf-8"))
    have = table_keys(PANEL.read_text(encoding="utf-8"))

    missing = sorted(wanted - have)
    stale = sorted(have - wanted)

    for name in missing:
        print(f"NO TRANSLATION: '{name}' - the panel would show it in English")
    for name in stale:
        print(f"STALE ENTRY:    '{name}' - no such string in the engine any more")

    if missing or stale:
        return 1
    print(f"all {len(wanted)} checklist strings have a Russian label")
    return 0


if __name__ == "__main__":
    sys.exit(main())
