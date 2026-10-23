#!/usr/bin/env python3
"""Save states (issue #40): a state saved mid-play and loaded again carries on
bit-identically to the run that was never stopped.

For each scene: run A plays to END, saving a state at SAVE and printing the
picture's hash and the whole machine's fingerprint (--state-hash-every) every
frame; run B loads the state and plays from SAVE to END with the same input.
Every hash after SAVE must match. Then the interpreter and the recompiled code
run in lockstep from the loaded state, which must hold too.

usage: test_save_states.py <chaotix_headless> <rom>
"""
import os
import re
import subprocess
import sys
import tempfile

SCENES = [
    # name, save frame, end frame, arguments
    ("level", 1500, 1800,
     ["--stage", "300:0:1:0:2:7:0", "--press", "1300:right:400", "--press", "1520:c:10"]),
    ("special_stage", 1000, 1300,
     ["--stage", "300:10:3:0:4:3:0", "--fuzz", "7:700:2400"]),
    ("widescreen_level", 1200, 1500,
     ["--wide", "53", "--hud-edges", "--stage", "300:4:2:2:2:7:0", "--fuzz", "36:700:2000"]),
    ("widescreen_bonus_stage", 1000, 1300,
     ["--wide", "53", "--stage", "300:9:0:0:2:7:0", "--fuzz", "3:700:2400"]),
    # Issue #43: SOLO with the partner called back (held CALL at 1100): the
    # state keeps that, so the loaded run does not send it away again.
    ("ring_tether_solo_called_back", 1250, 1500,
     ["--tether-partner", "solo", "--tether-strength", "strong", "--stage", "300:0:1:0:2:7:0",
      "--press", "880:right:200", "--press", "1100:a:40", "--press", "1300:right+b:120"]),
]

HASH = re.compile(r"^(hash|statehash) (\d+) ([0-9a-f]+)$", re.M)


def run(headless, rom, args):
    p = subprocess.run([headless, "--rom", rom] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True)
    return p.returncode, p.stdout


def main():
    headless, rom = sys.argv[1], sys.argv[2]
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, save, end, args in SCENES:
            state = os.path.join(tmp, name + ".state").replace("\\", "/")
            common = ["--frames", str(end)] + args + ["--hash-every", "1", "--state-hash-every", "1"]
            code, a = run(headless, rom, common + ["--save-state", "%d:%s" % (save, state)])
            if code != 0 or "state: saved" not in a:
                print("%s: the first run failed (%d)\n%s" % (name, code, a[-2000:]))
                failed += 1
                continue
            code, b = run(headless, rom, common + ["--load-state", state])
            if code != 0 or "state: loaded" not in b:
                print("%s: the run from the state failed (%d)\n%s" % (name, code, b[-2000:]))
                failed += 1
                continue
            ha = [m.groups() for m in HASH.finditer(a) if int(m.group(2)) > save]
            hb = [m.groups() for m in HASH.finditer(b)]
            if len(ha) != 2 * (end - save) or ha != hb:
                first = next((x for x, y in zip(ha, hb) if x != y), None)
                print("%s: DIVERGED after loading (%d vs %d hashes; first difference %s)" % (name, len(ha), len(hb), first))
                failed += 1
                continue
            size = re.search(r"state: saved .* \((\d+) bytes, (\d+) raw\)", a)
            print("%s: frames %d-%d identical after loading (state %s bytes, %s raw)" %
                  (name, save + 1, end, size.group(1), size.group(2)))
            code, c = run(headless, rom, ["--frames", str(end)] + args + ["--load-state", state, "--lockstep"])
            want = "lockstep: %d frames bit-identical" % (end - save)
            if code != 0 or want not in c:
                print("%s: lockstep from the state FAILED (%d)\n%s" % (name, code, c[-2000:]))
                failed += 1
                continue
            print("%s: %s from the state" % (name, want))
    if failed:
        print("%d scene(s) failed" % failed)
        return 1
    print("all scenes carry on bit-identically from a save state")
    return 0


if __name__ == "__main__":
    sys.exit(main())
