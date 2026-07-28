#!/usr/bin/env python3
"""Ask both implementations who owns every livery on this machine, and diff.

The 27 hand-checked folders in tests/liveries.txt say the port is right about the
cases somebody thought of. This says it is right about the ones nobody did - it
walks X-Plane's Aircraft folder and puts every real livery name through the 1.x
Lua detector and the C++ one, then compares verdict by verdict.

    python tests/diff_airlines_vs_1x.py [--xplane DIR] [--x1 DIR] [--limit N]

The Lua side boots the plugin ONCE and then drives it by setting XA_LIVERY_PATH
and ticking, which is how the plugin itself notices a livery change. Booting per
folder would be correct too, and roughly a thousand times slower.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

DEFAULT_XPLANE = r"E:\SteamLibrary\steamapps\common\X-Plane 12"
DEFAULT_X1 = r"D:\claude2\x-announcer"
DEFAULT_XA_TEST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               "build", "xa_test.exe")


def collect_liveries(xplane_dir: str, limit: int) -> list[str]:
    """Every folder that sits directly inside an aircraft's `liveries` folder."""
    names: list[str] = []
    seen: set[str] = set()
    aircraft = os.path.join(xplane_dir, "Aircraft")
    for root, dirs, _files in os.walk(aircraft):
        if os.path.basename(root).lower() != "liveries":
            continue
        for name in sorted(dirs):
            # Tabs and newlines would break the line-based exchange format, and
            # a livery folder has no business containing either.
            if "\t" in name or "\n" in name or name in seen:
                continue
            seen.add(name)
            names.append(name)
            if limit and len(names) >= limit:
                return names
        dirs[:] = []  # a livery's own subfolders are not liveries
    return names


def run_cpp(xa_test: str, names: list[str]) -> dict[str, str]:
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False,
                                     encoding="utf-8", newline="\n") as f:
        f.write("\n".join(names) + "\n")
        path = f.name
    try:
        out = subprocess.run([xa_test, f"--detect-file={path}"], capture_output=True,
                             text=True, encoding="utf-8", check=True).stdout
    finally:
        os.unlink(path)

    verdicts = {}
    for line in out.splitlines():
        if "\t" in line:
            folder, code = line.split("\t", 1)
            verdicts[folder] = code
    return verdicts


def run_lua(x1_root: str, names: list[str]) -> dict[str, str]:
    sys.path.insert(0, os.path.join(x1_root, "tests"))
    import sim_test  # noqa: E402

    library = tempfile.mkdtemp(prefix="xa_airline_lib_")
    os.makedirs(os.path.join(library, "TST"))
    sim = sim_test.Sim()
    lua, _tmp = sim_test.build_runtime(sim, library, "airline_mode = auto\nauto_find = false\n")
    sim_test.run_script(lua)

    read_code = lua.eval("function() return XA_DEBUG.airline().code end")
    verdicts = {}
    for name in names:
        lua.globals().XA_LIVERY_PATH = "Aircraft/Test/liveries/%s/" % name
        lua.globals().xa_tick()
        verdicts[name] = read_code()
    return verdicts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xplane", default=DEFAULT_XPLANE)
    parser.add_argument("--x1", default=DEFAULT_X1)
    parser.add_argument("--xa-test", default=DEFAULT_XA_TEST)
    parser.add_argument("--limit", type=int, default=0, help="stop after N liveries")
    args = parser.parse_args()

    names = collect_liveries(args.xplane, args.limit)
    if not names:
        print(f"no liveries found under {args.xplane}\\Aircraft")
        return 2
    print(f"{len(names)} livery folders found")

    cpp = run_cpp(args.xa_test, names)
    lua = run_lua(args.x1, names)

    def norm(code: str) -> str:
        # 1.x reports the fallback as the pack name "DEFAULT", the core as the
        # airline "Default". Same verdict, different spelling of the sentinel.
        return "DEFAULT" if code.upper() == "DEFAULT" else code

    differ = []
    for name in names:
        a = norm(cpp.get(name, "<missing>"))
        b = norm(lua.get(name, "<missing>"))
        if a != b:
            differ.append((name, a, b))

    for name, a, b in differ[:40]:
        print(f"  {name}\n     v2 : {a}\n     1.x: {b}")
    if len(differ) > 40:
        print(f"  ... and {len(differ) - 40} more")

    detected = sum(1 for n in names if cpp.get(n) not in ("Default", "<missing>"))
    print()
    print(f"recognised by v2: {detected}/{len(names)}")
    if differ:
        print(f"{len(differ)} of {len(names)} liveries disagree")
        return 1
    print("both implementations agree on every livery on this machine")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
