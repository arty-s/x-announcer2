#!/usr/bin/env python3
"""Fly every scenario through BOTH implementations and diff the two traces.

This is the guard against the one failure mode a port has that new code does
not: behaviour drifting quietly. Unit tests written against the new code cannot
catch it, because they were written from the same misunderstanding. Only the old
implementation knows what the old implementation did.

    python tests/diff_vs_1x.py [--x1 D:\\claude2\\x-announcer] [--scenario NAME]

It drives the 1.x Lua plugin through the harness that ships with it (lupa +
tests/sim_test.py), reading the SAME .scn files the C++ bench uses, and compares:

  * the order of announcements and phase changes - must be identical;
  * when each happened - allowed to differ by up to 2 simulated seconds, since
    the two runtimes service their queues on slightly different sub-frames.

Durations are controlled exactly: for each declared event it writes a 44-byte
WAV whose header claims the wanted length. 1.x reads the length out of the chunk
header, so the old plugin sees precisely the duration the scenario asked for -
no dependence on a real sound library, and no drift from one machine to another.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import sys
import tempfile

PACK = "TST"
WAV_RATE = 8000  # bytes per second claimed in the header


def write_wav(path: str, seconds: float) -> None:
    """A WAV header that claims `seconds` of audio and carries no payload.

    1.x computes duration as data-chunk-size / byte-rate and never reads the
    samples, and the bench stubs playback out entirely, so the bytes would be
    dead weight - a 120 second music bed would otherwise be 10 MB of silence.
    """
    data_size = int(round(seconds * WAV_RATE))
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + data_size))
        f.write(b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, WAV_RATE, WAV_RATE, 1, 8))
        f.write(b"data")
        f.write(struct.pack("<I", data_size))


# --- the scenario format, parsed the same way the C++ bench parses it ---------

class Scenario:
    def __init__(self, path: str):
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.config: dict[str, str] = {}
        self.library: dict[str, float] = {}
        self.steps: list[tuple] = []
        self.touches_seatbelt = False
        self.environment_events: list[str] = []

        with open(path, encoding="utf-8") as f:
            for raw in f:
                line = raw.split("#", 1)[0].strip()
                if not line:
                    continue
                words = line.split()
                verb, rest = words[0], words[1:]
                if verb == "config":
                    for token in rest:
                        key, _, value = token.partition("=")
                        self.config[key] = value
                elif verb == "library":
                    for token in rest:
                        event, _, seconds = token.partition(":")
                        self.library[event] = float(seconds) if seconds else 8.0
                elif verb == "set":
                    changes = {}
                    for token in rest:
                        key, _, value = token.partition("=")
                        changes[key] = value
                        if key == "seatbelt":
                            self.touches_seatbelt = True
                    self.steps.append(("set", changes))
                elif verb == "advance":
                    rate = 1.0
                    for token in rest[1:]:
                        key, _, value = token.partition("=")
                        if key == "rate":
                            rate = float(value)
                    self.steps.append(("advance", float(rest[0]), rate))
                elif verb == "expect":
                    pass  # checked by the C++ bench; the diff compares traces
                elif verb == "event":
                    # Something the simulator TELLS the plugin. There is nothing
                    # to compare here: in 1.x the same events restart FlyWithLua's
                    # Lua engine, so its "handling" is the plugin being loaded
                    # again from zero, which this bench cannot express. The
                    # scenario is skipped, out loud, rather than half-flown.
                    self.environment_events.append(rest[0] if rest else verb)
                else:
                    raise SystemExit(f"{path}: unknown directive '{verb}'")


CONFIG_KEYS = {
    "enabled", "boarding_music", "cabin_noise", "auto_boarding", "pilot_welcome",
    "door_calls", "night_dim", "landing_reaction", "boarding_repeat", "music_max_loops",
}

BOOLS = {"enabled", "boarding_music", "cabin_noise", "auto_boarding", "pilot_welcome",
         "door_calls", "night_dim", "landing_reaction"}


def config_ini(scenario: Scenario) -> str:
    lines = [
        "airline_mode = manual",
        f"airline_manual = {PACK}",
        "auto_find = false",  # never wander off looking for a real UA_Sounds folder
    ]
    for key, value in scenario.config.items():
        if key not in CONFIG_KEYS:
            raise SystemExit(f"{scenario.name}: config key '{key}' has no 1.x equivalent")
        if key in BOOLS:
            value = "true" if value in ("1", "true", "yes") else "false"
        lines.append(f"{key} = {value}")
    return "\n".join(lines) + "\n"


def apply_set(sim, changes: dict[str, str]) -> None:
    """Translate the scenario's units into the datarefs 1.x actually reads."""
    def flag(v):
        return 1 if v in ("1", "true", "yes") else 0

    for key, value in changes.items():
        if key == "on_ground":
            sim.set(on_ground=flag(value))
        elif key == "gs_kt":
            sim.set(gs_ms=float(value) / 1.94384)
        elif key == "agl_ft":
            sim.set(agl_m=float(value) / 3.28084)
        elif key in ("alt_ft", "vs_fpm", "g"):
            sim.set(**{key: float(value)})
        elif key in ("beacon", "nav", "strobe", "landing", "taxi", "battery"):
            sim.set(**{key: flag(value)})
        elif key == "parkbrake":
            sim.set(parkbrake=1.0 if flag(value) else 0.0)
        elif key == "engines":
            sim.set(engines=int(value) > 0)
        elif key == "hour":
            sim.set(local_hour=int(value))
        elif key == "paused":
            sim.values["sim/time/paused"] = flag(value)
        elif key == "replay":
            sim.values["sim/time/is_in_replay"] = flag(value)
        elif key == "seatbelt":
            sim.set(seatbelt=flag(value))
        elif key in ("logo", "logo_dref"):
            pass  # no logo dataref in the stock sim; 1.x only reads add-on ones
        else:
            raise SystemExit(f"unknown sim field '{key}'")


def run_1x(x1_root: str, scenario: Scenario) -> list[tuple[float, str, str]]:
    sys.path.insert(0, os.path.join(x1_root, "tests"))
    import sim_test  # noqa: E402  (path has to be set first)

    library = tempfile.mkdtemp(prefix="xa_diff_lib_")
    pack = os.path.join(library, PACK)
    os.makedirs(pack)
    for event, seconds in scenario.library.items():
        write_wav(os.path.join(pack, event + ".wav"), seconds)

    sim = sim_test.Sim()
    if not scenario.touches_seatbelt:
        # An aircraft that publishes no sign at all is not the same as one whose
        # sign is off, and the C++ core models that with a third value. Match it
        # here by removing the dataref rather than leaving it at zero.
        sim.values.pop("sim/cockpit2/switches/fasten_seat_belts", None)

    lua, _tmp = sim_test.build_runtime(sim, library, config_ini(scenario))
    sim_test.run_script(lua)

    trace: list[tuple[float, str, str]] = []
    seen_plays = len(sim.played)
    # Loading logs its own "flight reset (startup)"; the C++ engine has no such
    # event, so the scenario starts from a clean slate on both sides.
    seen_log = len(sim.log)

    # 1.x's own simulator clock, reproduced by the same rule it uses: it stops
    # while the sim is paused or replaying. The raw network_time_sec dataref does
    # NOT stop, and using it would make every scenario with a pause look as
    # though the two implementations disagreed by exactly the pause length.
    clock = 0.0

    def poll():
        """Phase changes come from the plugin's own log, not from polling state.

        Polling misses transitions that happen twice inside one tick - lifting
        off straight through TAKEOFF into CLIMB, for instance - and those are
        real events the C++ engine records. Reading the log sees every one.
        """
        nonlocal seen_plays, seen_log
        while seen_log < len(sim.log):
            line = sim.log[seen_log]
            seen_log += 1
            if "phase -> " in line:
                trace.append((round(clock, 1), "phase", line.split("phase -> ", 1)[1].strip()))
            elif "flight reset" in line and " -> " in line:
                trace.append((round(clock, 1), "phase", line.rsplit(" -> ", 1)[1].strip()))
        while seen_plays < len(sim.played):
            _when, bus, filename = sim.played[seen_plays]
            seen_plays += 1
            if filename == "<stop>":
                continue
            kind = "play" if bus == "interior" else "music"
            trace.append((round(clock, 1), kind, os.path.splitext(filename)[0]))

    for step in scenario.steps:
        if step[0] == "set":
            apply_set(sim, step[1])
        else:
            _, seconds, rate = step
            frozen = (sim.values.get("sim/time/paused", 0) == 1 or
                      sim.values.get("sim/time/is_in_replay", 0) == 1)
            remaining = seconds
            while remaining > 0:
                chunk = min(1.0, remaining)
                sim_test.advance(lua, sim, chunk, sim_rate=rate)
                remaining -= chunk
                if not frozen:
                    clock += chunk * rate
                poll()
    poll()
    shutil.rmtree(library, ignore_errors=True)
    return trace


def read_cpp_trace(path: str) -> list[tuple[float, str, str]]:
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip():
                continue
            parts = line.split()
            when = float(parts[0])
            kind = parts[1]
            event = parts[2] if len(parts) > 2 and not parts[2].startswith("(") else ""
            if kind in ("stop", "music-stop", "note"):
                continue
            # A reset IS a phase change - it is how 1.x reports the turnaround
            # and the teleport recovery, and that is what its log says too.
            if kind == "reset":
                kind = "phase"
            out.append((when, kind, event))
    return out


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser()
    parser.add_argument("--x1", default=r"D:\claude2\x-announcer",
                        help="root of the 1.x checkout")
    parser.add_argument("--scenario", default=None, help="run a single scenario by name")
    parser.add_argument("--tolerance", type=float, default=2.0,
                        help="allowed timing difference in simulated seconds")
    args = parser.parse_args()

    scenario_dir = os.path.join(here, "scenarios")
    golden_dir = os.path.join(here, "golden")

    names = sorted(n for n in os.listdir(scenario_dir) if n.endswith(".scn"))
    if args.scenario:
        names = [n for n in names if args.scenario in n]

    failures = 0
    for name in names:
        scenario = Scenario(os.path.join(scenario_dir, name))
        golden = os.path.join(golden_dir, scenario.name + ".trace")
        if not os.path.exists(golden):
            print(f"-- {scenario.name}: SKIP (no C++ trace; run xa_test first)")
            continue
        if scenario.environment_events:
            events = ", ".join(scenario.environment_events)
            print(f"-- {scenario.name}: SKIP ({events} - in 1.x this restarts the "
                  f"Lua engine, so there is no behaviour to compare)")
            continue

        cpp = read_cpp_trace(golden)
        lua = run_1x(args.x1, scenario)

        cpp_order = [(k, e) for _t, k, e in cpp]
        lua_order = [(k, e) for _t, k, e in lua]

        if cpp_order != lua_order:
            failures += 1
            print(f"-- {scenario.name}: DIFFERS")
            for i in range(max(len(cpp_order), len(lua_order))):
                a = cpp_order[i] if i < len(cpp_order) else ("<missing>", "")
                b = lua_order[i] if i < len(lua_order) else ("<missing>", "")
                if a != b:
                    print(f"     v2  : {a[0]} {a[1]}")
                    print(f"     1.x : {b[0]} {b[1]}")
            continue

        worst = 0.0
        worst_at = ""
        for (t1, k, e), (t2, _k, _e) in zip(cpp, lua):
            delta = abs(t1 - t2)
            if delta > worst:
                worst, worst_at = delta, f"{k} {e}"
        if worst > args.tolerance:
            failures += 1
            print(f"-- {scenario.name}: same order, but {worst_at} is {worst:.1f} s apart")
        else:
            print(f"-- {scenario.name}: identical ({len(cpp_order)} events, "
                  f"worst timing gap {worst:.1f} s)")

    print()
    print("all scenarios match 1.x" if failures == 0 else f"{failures} scenario(s) differ")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
