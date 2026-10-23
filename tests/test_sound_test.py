#!/usr/bin/env python3
"""The game's own SOUND TEST plays (issue #50).

OPTIONS > SOUND TEST, choose an entry, press A. Its vertical-blank routine
takes the Z80's bus a few dozen 68K cycles into the line on which the Z80 is
interrupted; the Z80 must have taken that interrupt by then (it runs up to
the 68K's time whenever the 68K requests its bus), or it never reads the
request and the screen is silent. See docs/ISSUE_50_SOUND_TEST.md.

 - a song (entry 01, DOOR INTO SUMMER) and a sound effect (S.E. 6E, the
   last entry) are heard after A, not silence;
 - the interpreters render the song exactly as the recompiled code does,
   and lockstep holds through it.

usage: test_sound_test.py <chaotix_headless> <rom>
"""
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

RATE = 53693175.0 / 1008  # the machine's output rate (system.h, kAudioRate)
FPS = 53693175.0 / (3420 * 262)  # NTSC frames a second

# Title > OPTIONS > SOUND TEST (the screen's entry 0, its header), at frame 2400.
TO_SOUND_TEST = ["--press", "1800:start:6", "--press", "1900:down:4", "--press", "1930:down:4",
                 "--press", "1980:start:6", "--press", "2150:down:4", "--press", "2250:start:6"]
STATE_FRAME = 2400


def run(headless, rom, args):
    p = subprocess.run([headless, "--rom", rom] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       universal_newlines=True)
    return p.returncode, p.stdout + "\n" + p.stderr


def read_wav(path):
    with wave.open(path) as w:
        raw = w.readframes(w.getnframes())
    return struct.unpack("<%dh" % (len(raw) // 2), raw)


def level(samples):
    if not samples:
        return 0, 0.0
    return max(abs(v) for v in samples), math.sqrt(sum(v * v for v in samples) / len(samples))


def main():
    headless, rom = sys.argv[1], sys.argv[2]
    failed = []

    def check(cond, what, out=""):
        if not cond:
            failed.append(what)
            print("FAIL: " + what)
            if out:
                print(out[-3000:])
        else:
            print("ok: " + what)

    with tempfile.TemporaryDirectory() as tmp:
        state = os.path.join(tmp, "sound_test.state").replace("\\", "/")
        code, out = run(headless, rom, ["--frames", str(STATE_FRAME)] + TO_SOUND_TEST
                        + ["--save-state", "%d:%s" % (STATE_FRAME, state)])
        check(code == 0 and os.path.exists(state), "the SOUND TEST is reached", out)
        if failed:
            return 1

        def choose(button, extra=()):
            """From the SOUND TEST's header: one press of button, then A; 4 s of sound after A."""
            a = STATE_FRAME + 12
            frames = a + 240
            path = os.path.join(tmp, "%s_%d.wav" % (button, len(extra))).replace("\\", "/")
            c, o = run(headless, rom, ["--load-state", state, "--frames", str(frames),
                                       "--press", "%d:%s:3" % (STATE_FRAME + 2, button),
                                       "--press", "%d:a:4" % a, "--wav", path] + list(extra))
            s = read_wav(path) if c == 0 and os.path.exists(path) else []
            before = s[int((a - STATE_FRAME - 6) / FPS * RATE) * 2:int((a - STATE_FRAME) / FPS * RATE) * 2]
            after = s[int((a - STATE_FRAME + 30) / FPS * RATE) * 2:]
            return c, o, s, level(before), level(after)

        cs, os_, ss, (pb, rb), (pa, ra) = choose("right")
        print("   song 01: before A peak %d rms %.0f; after A peak %d rms %.0f" % (pb, rb, pa, ra))
        check(cs == 0 and rb < 30 and ra > 300, "a song plays from the SOUND TEST", os_)

        ce, oe, se, (pb, rb), (pa, ra) = choose("left")
        print("   S.E. 6E: before A peak %d rms %.0f; after A peak %d rms %.0f" % (pb, rb, pa, ra))
        check(ce == 0 and rb < 30 and ra > 300, "a sound effect plays from the SOUND TEST", oe)

        ci, oi, si, _, _ = choose("right", ["--interp"])
        check(ci == 0 and len(si) > 0 and si == ss,
              "the interpreters play the song exactly as the recompiled code does", oi)

        cl, ol, _, _, _ = choose("right", ["--lockstep"])
        check(cl == 0 and "bit-identical" in ol, "lockstep holds while the song starts", ol)

    if failed:
        print("%d check(s) failed" % len(failed))
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
