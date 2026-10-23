#!/usr/bin/env python3
"""TIME ATTACK ghosts (issue #41) on the real game.

A recorded session that finishes a level (recordings/finished_run.txt, BOTANIC
BASE 1 in 0'34"66) is turned into a ghost by chaotix_headless --ghost-record.
Then a different run of the same stage, with other characters and random
input, races it (--ghost):

 - the ghost replays exactly: it reaches the goal and ends with the recorded
   time, on the recorded last frame ("in sync");
 - it is drawn: every pixel the ghost changes lies in its box where its world
   position lands on the live screen, and it is drawn on some frames;
 - the live run is untouched: every frame's picture and whole-machine state
   is identical to the same run without a ghost;
 - the ghost played on its own thread is drawn exactly as the one played in
   step;
 - lockstep (interpreter against recompiled code) still holds with a ghost;
 - a ghost whose inputs were changed does not replay as recorded, and says so.

usage: test_ghosts.py <chaotix_headless> <rom> <recordings dir>
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

RECORDED_TIME = 2086
LIVE = ["--stage", "300:0:1:0:2:7:0", "--fuzz", "1:700:4400"]
FRAMES = "4400"


def run(headless, rom, args):
    # The log goes to stderr, kept apart: merged, a log line can land in the
    # middle of a hash line.
    p = subprocess.run([headless, "--rom", rom] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       universal_newlines=True)
    return p.returncode, p.stdout + "\n" + p.stderr


def hashes(out):
    return re.findall(r"^(?:hash|statehash) \d+ [0-9a-f]+$", out, re.M)


def lz4_literals(data):
    """An LZ4 block holding `data` as literals only (runtime/lz.h reads it)."""
    n = len(data)
    out = bytearray()
    if n < 15:
        out.append(n << 4)
    else:
        out.append(0xF0)
        rest = n - 15
        while rest >= 255:
            out.append(255)
            rest -= 255
        out.append(rest)
    return bytes(out) + data


def tamper(src, dst):
    """The ghost with every input let go, its CRC put right: a well-formed
    ghost that is not the run it claims to be."""
    b = open(src, "rb").read()
    header = 172
    frames, changes, start, packed, _ = struct.unpack_from("<5I", b, 152)
    body = b[header:]
    settings = body[:changes * 24]
    state = body[changes * 24:changes * 24 + start]
    inputs = bytearray(frames * 4)
    new_packed = lz4_literals(bytes(inputs))
    payload = settings + state + new_packed
    head = bytearray(b[:header])
    struct.pack_into("<5I", head, 152, frames, changes, start, len(new_packed), zlib.crc32(payload) & 0xFFFFFFFF)
    open(dst, "wb").write(bytes(head) + payload)


def main():
    headless, rom, recordings = sys.argv[1], sys.argv[2], sys.argv[3]
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
        g = os.path.join(tmp, "botanic_base_1.ghost").replace("\\", "/")
        code, out = run(headless, rom, ["--frames", "4700", "--replay-input",
                                        os.path.join(recordings, "finished_run.txt"), "--ghost-record", g])
        m = re.search(r"ghost: recorded .*, (\d+) frames, time \S+ \((\d+) frames\), (\d+) bytes", out)
        check(code == 0 and m and int(m.group(2)) == RECORDED_TIME and os.path.exists(g),
              "a finished run is recorded as a ghost", out)
        if not m:
            return 1
        print("   ghost file: %s bytes, %s input frames" % (m.group(3), m.group(1)))

        common = ["--frames", FRAMES] + LIVE + ["--hash-every", "1", "--state-hash-every", "1"]
        code, with_ghost = run(headless, rom, common + ["--ghost", g, "--ghost-log", "1"])
        check(code == 0 and re.search(r"ghost: reached the goal at ghost frame \d+, time \S+ \(%d frames\)"
                                      % RECORDED_TIME, with_ghost) is not None,
              "the ghost reaches the goal in the recorded time", with_ghost)
        check(re.search(r"ghost: ended after \d+ frames, time \S+ \(%d frames\), recorded \S+ \(%d frames\): in sync"
                        % (RECORDED_TIME, RECORDED_TIME), with_ghost) is not None,
              "the ghost ends where the recorded run did (in sync)", with_ghost)
        d = re.search(r"ghost: drawn on (\d+) of (\d+) frames, (\d+) pixels, (\d+) outside its box, (\d+) frames misplaced",
                      with_ghost)
        check(d is not None and int(d.group(1)) > 50 and int(d.group(4)) == 0 and int(d.group(5)) == 0,
              "the ghost is drawn, and only inside its box on the live screen", with_ghost[-1500:])

        code, without = run(headless, rom, common)
        a, b = hashes(with_ghost), hashes(without)
        check(code == 0 and len(a) == 2 * int(FRAMES) and a == b,
              "the live run is identical, frame by frame, with and without the ghost")

        code, threaded = run(headless, rom, ["--frames", FRAMES] + LIVE + ["--ghost", g, "--ghost-log", "1",
                                                                          "--ghost-threaded"])
        log_a = re.findall(r"^ghost f=.*$", with_ghost, re.M)
        log_b = re.findall(r"^ghost f=.*$", threaded, re.M)
        check(code == 0 and len(log_a) > 1000 and log_a == log_b and "in sync" in threaded,
              "the ghost on its own thread is drawn exactly as the one played in step", threaded[-1500:])

        code, lock = run(headless, rom, ["--frames", "1800", "--lockstep", "--ghost", g] + LIVE)
        check(code == 0 and re.search(r"lockstep: 1800 frames bit-identical", lock) is not None
              and "ghost: started" in lock,
              "lockstep holds with a ghost racing", lock[-1500:])

        bad = os.path.join(tmp, "tampered.ghost").replace("\\", "/")
        tamper(g, bad)
        code, out = run(headless, rom, common[:2] + LIVE + ["--ghost", bad])
        check(code == 0 and re.search(r"ghost: ended after .*: DESYNC", out) is not None,
              "a ghost whose inputs were changed is told apart", out[-1500:])

    print("%d failed" % len(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
