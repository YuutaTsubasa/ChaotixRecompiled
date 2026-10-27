"""Wait-loop fast-forward (issue #62); supply a locally owned ROM.

Run: python tests/test_idle_skip.py path/to/chaotix_headless path/to/game.32x

The recompiled code charges the remaining iterations of a wait loop at once
instead of running them (recomp_api.h). It must change nothing: in a level,
the lobby and a special stage, with input, the machine's state hash every 50
frames is the same with the fast-forward on and off (--no-idle-skip), and it
does take most of the SH-2s' cycles (else this would test nothing).
Lockstep against the interpreter is checked by the lockstep_* tests.
"""
from pathlib import Path
import re
import subprocess
import sys
import unittest

EXE, ROM = (Path(p).resolve() for p in sys.argv[1:3])
del sys.argv[1:3]

SHARE = re.compile(r"wait loops fast-forwarded: 68K ([0-9.]+)%, MSH2 ([0-9.]+)%, SSH2 ([0-9.]+)% of cycles( \(off\))?")
SCENES = {
    "BOTANIC BASE 1": "300:0:1:0:2:7:0",
    "the lobby": "300:7:0:0:2:7:0",
    "a special stage": "300:10:0:0:2:7:0",
}


def run(stage, *extra):
    out = subprocess.run([str(EXE), "--rom", str(ROM), "--frames", "1500", "--stage", stage,
                          "--fuzz", "62:200:1500", "--state-hash-every", "50", *extra],
                         capture_output=True, text=True, timeout=600)
    if out.returncode != 0:
        raise AssertionError(f"chaotix_headless failed ({out.returncode}):\n{out.stdout}\n{out.stderr}")
    hashes = [l for l in out.stdout.splitlines() if l.startswith("statehash ")]
    share = SHARE.search(out.stdout)
    if not share:
        raise AssertionError("no fast-forward statistics in the output:\n" + out.stdout)
    return hashes, share


class IdleSkip(unittest.TestCase):
    def test_same_state_on_and_off(self):
        for name, stage in SCENES.items():
            with self.subTest(scene=name):
                on, share_on = run(stage)
                off, share_off = run(stage, "--no-idle-skip")
                self.assertEqual(len(on), 30)
                self.assertEqual(on, off)
                self.assertIsNone(share_on.group(4))
                self.assertEqual(share_off.group(4), " (off)")
                self.assertEqual([float(g) for g in share_off.groups()[:3]], [0.0, 0.0, 0.0])
                # Most of both SH-2s' time is waiting, and it is skipped.
                self.assertGreater(float(share_on.group(2)), 30.0)
                self.assertGreater(float(share_on.group(3)), 50.0)
                self.assertGreater(float(share_on.group(1)), 10.0)
                print(f"{name}: {share_on.group(0)}; {len(on)} state hashes equal")


if __name__ == "__main__":
    unittest.main(verbosity=2)
