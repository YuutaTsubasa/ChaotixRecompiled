#!/usr/bin/env python3
"""The music player (issue #48) on the real game.

 - the track list is read from the ROM: the SOUND TEST's 41 songs, from
   DOOR INTO SUMMER to DECISION 2ND, ids 01..29;
 - tracks rendered by the player's own machine (--jukebox TRACK:FRAMES --wav)
   are music, not silence, and two different tracks sound different;
 - the recompiled code and the interpreters render a track identically;
 - a track a music mod replaces plays the mod's file (a tone made here), and
   the list says it is replaced;
 - the player's machine is untouched: a run that opens the music player in
   the middle of a level (--jukebox-at) has every picture and whole-machine
   state identical to the same run without it, and lockstep still holds.

usage: test_music_player.py <chaotix_headless> <rom>
"""
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave

RATE = 53693175.0 / 1008  # the machine's output rate (system.h, kAudioRate)


def run(headless, rom, args):
    p = subprocess.run([headless, "--rom", rom] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       universal_newlines=True)
    return p.returncode, p.stdout + "\n" + p.stderr


def read_wav(path):
    with wave.open(path) as w:
        n = w.getnframes()
        raw = w.readframes(n)
    return struct.unpack("<%dh" % (len(raw) // 2), raw)


def level(samples):
    if not samples:
        return 0, 0.0
    peak = max(abs(v) for v in samples)
    rms = math.sqrt(sum(v * v for v in samples) / len(samples))
    return peak, rms


def tone_wav(path, freq, seconds, rate=44100):
    """A stereo 16-bit sine, for a music mod."""
    frames = int(seconds * rate)
    data = bytearray()
    for i in range(frames):
        v = int(12000 * math.sin(2 * math.pi * freq * i / rate))
        data += struct.pack("<hh", v, v)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(data))


def zero_crossings_hz(samples):
    """Rough pitch of the left channel from its zero crossings."""
    left = samples[0::2]
    crossings = sum(1 for a, b in zip(left, left[1:]) if (a < 0) != (b < 0))
    return crossings / 2.0 / (len(left) / RATE)


def hashes(out):
    return re.findall(r"^(?:hash|statehash) \d+ [0-9a-f]+$", out, re.M)


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

    code, out = run(headless, rom, ["--jukebox-list"])
    tracks = re.findall(r"^jukebox track (\d+) id ([0-9A-F]{2}) (.*?)( \[replaced by mod\])?$", out, re.M)
    check(code == 0 and "jukebox: 41 tracks from the ROM" in out and len(tracks) == 41,
          "the SOUND TEST's 41 songs are read from the ROM", out)
    check(len(tracks) == 41 and tracks[0][2] == "DOOR INTO SUMMER" and tracks[-1][2] == "DECISION 2ND"
          and tracks[0][1] == "01" and tracks[-1][1] == "29" and tracks[8][2] == "WALKIN'",
          "first and last names and ids match the game's SOUND TEST", out)

    with tempfile.TemporaryDirectory() as tmp:
        def render(track, frames, extra=()):
            path = os.path.join(tmp, "t%s_%d.wav" % (track, len(extra))).replace("\\", "/")
            c, o = run(headless, rom, ["--jukebox", "%s:%d" % (track, frames), "--wav", path] + list(extra))
            return c, o, (read_wav(path) if c == 0 and os.path.exists(path) else [])

        c1, o1, s1 = render(1, 600)
        c4, o4, s4 = render(4, 600)
        p1, r1 = level(s1)
        p4, r4 = level(s4)
        print("   track 1: peak %d rms %.0f; track 4: peak %d rms %.0f" % (p1, r1, p4, r4))
        check(c1 == 0 and c4 == 0 and r1 > 300 and r4 > 300,
              "two tracks rendered by the music player are music, not silence", o1 + o4)
        n = min(len(s1), len(s4))
        diff = sum(1 for a, b in zip(s1[:n], s4[:n]) if a != b)
        check(n > 0 and diff > n // 2, "different tracks sound different (%d of %d samples differ)" % (diff, n))
        ci, oi, si = render(4, 600, ["--jukebox-interp"])
        check(ci == 0 and "interpreted" in oi and si == s4,
              "the interpreters render the track exactly as the recompiled code does", oi)

        # A music mod replacing track 5 with a 440 Hz tone.
        music = os.path.join(tmp, "Music")
        os.makedirs(music)
        tone_wav(os.path.join(music, "05.wav"), 440.0, 4.0)
        code, out = run(headless, rom, ["--jukebox-list", "--music-dir", music])
        check(re.search(r"^jukebox track 05 id 05 MIDNIGHT GREENHOUSE \[replaced by mod\]$", out, re.M) is not None
              and len(re.findall(r"\[replaced by mod\]", out)) == 1,
              "the list says which track a mod replaces", out)
        cm, om, sm = render(5, 180, ["--music-dir", music])
        hz = zero_crossings_hz(sm[int(RATE * 0.5) * 2:]) if sm else 0
        pm, rm = level(sm)
        print("   replaced track 5: peak %d rms %.0f, pitch about %.0f Hz" % (pm, rm, hz))
        check(cm == 0 and "from the mod's file" in om and 420 < hz < 460 and rm > 300,
              "a replaced track plays the mod's file", om)
        c5, o5, s5 = render(5, 180)
        check(c5 == 0 and "from the mod's file" not in o5 and level(s5)[1] > 300 and s5 != sm,
              "without the mod the same track is the game's own", o5)

    # The player's machine is untouched by the music player opened mid-level.
    live = ["--frames", "1800", "--stage", "300:4:2:2:2:7:0", "--fuzz", "36:700:1800"]
    common = live + ["--hash-every", "1", "--state-hash-every", "1"]
    code_a, without = run(headless, rom, common)
    code_b, with_jb = run(headless, rom, common + ["--jukebox-at", "1000:9:600"])
    a, b = hashes(without), hashes(with_jb)
    check(code_a == 0 and code_b == 0 and "jukebox: track 09" in with_jb and len(a) == 2 * 1800 and a == b,
          "the game is identical, frame by frame and state by state, with and without the music player",
          with_jb[-2000:])
    code, lock = run(headless, rom, live + ["--lockstep", "--jukebox-at", "1000:9:300"])
    check(code == 0 and re.search(r"lockstep: 1800 frames bit-identical", lock) is not None
          and "jukebox: track 09" in lock,
          "lockstep holds with the music player used mid-level", lock[-2000:])

    print("%d failed" % len(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
