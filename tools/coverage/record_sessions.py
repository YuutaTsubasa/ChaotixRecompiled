#!/usr/bin/env python3
"""Records execution-coverage traces for the static recompiler.

Each session is a deterministic headless run (scripted or pseudo-random
input) executed with the reference interpreter; the executed basic blocks
are written to coverage/<name>.cov (addresses/offsets only, no ROM data).
The CMake build feeds every coverage/*.cov file to chaotix_recomp.

usage: record_sessions.py --rom ROM [--headless PATH] [--only NAME ...] [--list]
"""
import argparse
import os
import subprocess
import sys
import time

# Menu timings (frames) valid for Knuckles' Chaotix (Japan, USA) from power-on.
TITLE = 1750          # title screen accepts Start
MENU = 1950           # Scenario Quest / Training / Options menu
BOOT_TO_MENU = ["--press", f"{TITLE}:start:5"]

SESSIONS = {
    # Boot -> title -> Scenario Quest -> DATA LOAD -> Isolated Island, simple play.
    "s01_boot_to_level": {
        "frames": 3200,
        "args": BOOT_TO_MENU + ["--press", f"{MENU}:start:5", "--press", "2300:start:5",
                                "--press", "2600:right:200", "--press", "2750:c:10",
                                "--press", "2900:left:100", "--press", "3050:c:5"],
    },
    # Attract mode: intro and gameplay demos of several zones.
    "s02_attract": {"frames": 30000, "args": []},
    # Scenario Quest, long pseudo-random play in the hub world.
    "s03_scenario_fuzz": {
        "frames": 14000,
        "args": BOOT_TO_MENU + ["--press", f"{MENU}:start:5", "--press", "2300:start:5",
                                "--fuzz", "1:2600:14000"],
    },
}
# Training mode, acts 1-4 of Isolated Island with pseudo-random play.
for act in range(1, 5):
    args = BOOT_TO_MENU + ["--press", f"{MENU}:down:4", "--press", "2000:start:5"]
    for k in range(act - 1):
        args += ["--press", f"{2300 + 60 * k}:down:4"]
    args += ["--press", f"{2300 + 60 * act}:start:5", "--fuzz", f"{10 + act}:2800:9000"]
    SESSIONS[f"s1{act}_training_act{act}"] = {"frames": 9000, "args": args}
# Options menu navigation.
SESSIONS["s20_options"] = {
    "frames": 3600,
    "args": BOOT_TO_MENU + ["--press", f"{MENU}:down:4", "--press", f"{MENU + 40}:down:4",
                            "--press", f"{MENU + 80}:start:5"]
            + [x for i in range(12) for x in ("--press", f"{2300 + i * 90}:{['down', 'right', 'up', 'left'][i % 4]}:4")]
            + ["--press", "3400:b:5"],
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--headless", default=os.path.join("build", "chaotix_headless"))
    ap.add_argument("--out", default="coverage")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.list:
        for name, s in SESSIONS.items():
            print(f"{name}: {s['frames']} frames")
        return 0
    os.makedirs(a.out, exist_ok=True)
    for name, s in SESSIONS.items():
        if a.only and name not in a.only:
            continue
        out = os.path.join(a.out, name + ".cov")
        cmd = [a.headless, "--rom", a.rom, "--interp", "--frames", str(s["frames"]), "--coverage", out] + s["args"]
        t = time.time()
        r = subprocess.run(cmd, capture_output=True, text=True)
        line = next((l for l in r.stdout.splitlines() if l.startswith("coverage written")), r.stdout[-200:])
        print(f"{name}: {line} ({time.time() - t:.1f}s)")
        if r.returncode != 0:
            print(r.stdout, r.stderr, file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
