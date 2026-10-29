"""The title screen in widescreen (issue #77); supply a locally owned ROM.

Run: python tests/test_wide_title.py path/to/chaotix_headless path/to/game.32x

The title's horizon (the road and the bands of floor) is the Mega Drive's
planes, which the game fills wider than the screen; the margins show it as the
game drew it instead of black bars. Rendered headless at 16:9 (53 px a side)
and at the widest margin (80), with a 4:3 run beside them:

- the road (frame 1320) and PUSH START (1700): the 4:3 centre is unchanged;
  the margins hold the floor on every line below the horizon and the black sky
  above it, and carry on across the 4:3 edge in the edge's colours; the left margin is the exact mirror of the right one, as the
  floor is (anything the game had not drawn there would not be);
- the SEGA logo (100) and the city (600), which are all 32X: black margins.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

EXE, ROM = (Path(p).resolve() for p in sys.argv[1:3])
del sys.argv[1:3]

FRAMES = (100, 600, 1320, 1700)
HORIZON = 97  # the first line of the floor on the settled title


def read_png(path):
    """The pixels of a PNG as the program writes it (8-bit RGB or RGBA, any filter)."""
    with open(path, "rb") as f:
        data = f.read()
    pos, idat, w, h, bpp = 8, b"", 0, 0, 3
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), (depth, ctype)
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        ft = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1: line[i] = (line[i] + a) & 255
            elif ft == 2: line[i] = (line[i] + b) & 255
            elif ft == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
    return w, h, rows


class WideTitle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="chaotix-wide-title-")
        cls.root = Path(cls.tmp.name)
        cls.shots = {}
        for wide in (0, 53, 80):
            out = cls.root / f"wide{wide}"
            out.mkdir()
            args = [str(EXE), "--rom", str(ROM), "--frames", str(max(FRAMES) + 1), "--out", str(out),
                    "--shot", ",".join(map(str, FRAMES))]
            if wide:
                args += ["--wide", str(wide)]
            r = subprocess.run(args, capture_output=True, text=True, errors="replace", timeout=600)
            assert r.returncode == 0, (r.stdout + r.stderr)[-2000:]
            for f in FRAMES:
                cls.shots[wide, f] = read_png(out / f"frame_{f:05d}.png")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def margins(self, wide, frame):
        w, h, rows = self.shots[wide, frame]
        self.assertEqual((w, h), (320 + 2 * wide, 224))
        return rows, [r[:wide] for r in rows], [r[w - wide:] for r in rows]

    def test_the_centre_is_the_4_3_picture(self):
        for wide in (53, 80):
            for f in FRAMES:
                rows = self.shots[wide, f][2]
                native = self.shots[0, f][2]
                self.assertTrue(all(r[wide:wide + 320] == n for r, n in zip(rows, native)), (wide, f))

    def test_the_horizon_goes_on_in_the_margins(self):
        black = (0, 0, 0)
        for wide in (53, 80):
            for f in (1320, 1700):
                rows, left, right = self.margins(wide, f)
                # The floor is symmetric about the centre; the margins are its
                # mirror images, line for line.
                for y in range(224):
                    self.assertEqual(left[y], right[y][::-1], (wide, f, y))
                # The margins continue the picture across its 4:3 edge: on
                # the floor's lines the column beside the edge is the edge's
                # colour (all but where a band's slanted edge crosses it).
                floor = range(HORIZON + 3, 224)
                same = sum(rows[y][wide - 1] == rows[y][wide] and rows[y][-wide] == rows[y][-wide - 1] for y in floor)
                self.assertGreaterEqual(same, len(floor) * 9 // 10, (wide, f))
                # Floor on every line below the horizon, black sky above it.
                for y in range(HORIZON + 3, 224):
                    self.assertNotIn(black, left[y], (wide, f, y))
                if f == 1700:
                    for y in range(HORIZON - 1):
                        self.assertEqual(set(left[y]), {black}, (wide, f, y))

    def test_the_sega_logo_and_the_city_keep_their_bars(self):
        for wide in (53, 80):
            for f in (100, 600):
                rows, left, right = self.margins(wide, f)
                self.assertEqual({p for side in (left, right) for r in side for p in r}, {(0, 0, 0)}, (wide, f))
                self.assertTrue(any(p != (0, 0, 0) for r in rows for p in r), (wide, f))


if __name__ == "__main__":
    unittest.main()
