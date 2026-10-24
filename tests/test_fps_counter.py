"""SHOW FPS in the real program (issue #54); supply a locally owned ROM.

Run: python tests/test_fps_counter.py path/to/ChaotixRecompiled.exe path/to/game.32x

The program is run headless (SDL's dummy video driver, the software renderer)
into a level, once with SHOW FPS off and once with it on, in a phone-shaped
window with the touch controls, a portrait one and a desktop one. The counter
is drawn by the host over the window: the game's own picture hashes the same
both times, the window captures are byte-identical outside the box the
program says it drew the counter in, and that box is inside the window, in
its top half, clear of the touch buttons, and larger at a larger MENU SIZE.
Only temporary user data is touched.
"""
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

EXE, ROM = (Path(p).resolve() for p in sys.argv[1:3])
del sys.argv[1:3]

BOX = re.compile(r"autotest: counter at (\d+),(\d+) size (\d+)x(\d+) in (\d+)x(\d+)")
HASH = re.compile(r"autotest: picture hash ([0-9a-f]{16})")


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
        rows.append(bytes(line))
    return w, h, bpp, rows


class FpsCounter(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="chaotix-fps-")
        cls.root = Path(cls.tmp.name)
        cls.store = cls.root / "user data"

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_app(self, window, show, extra=()):
        # The same settings for every run (a run keeps what it was given when
        # its menu closes). No achievements: an unlock's notice stays up for
        # seconds of real time, so it could be on one capture and gone from
        # the other.
        (self.store / "Config").mkdir(parents=True, exist_ok=True)
        (self.store / "Config" / "chaotix.ini").write_text("[Achievements]\nEnabled = false\n", encoding="utf-8")
        shot = self.root / f"{window}_{'on' if show else 'off'}_{'_'.join(extra) or 'plain'}.png"
        env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software")
        # Into BOTANIC BASE 1, where the menu is closed and the HUD is up.
        args = [str(EXE), "--user-dir", str(self.store), "--windowed", "--window-size", window,
                "--autotest", "500", "--autotest-shot", str(shot), "--stage", "100:0:1:0:2:7:0",
                "--show-fps" if show else "--no-show-fps", *extra, str(ROM)]
        result = subprocess.run(args, cwd=self.root, env=env, capture_output=True, text=True, errors="replace",
                                timeout=300)
        out = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, out[-2000:])
        h = HASH.search(out)
        self.assertIsNotNone(h, out[-2000:])
        box = BOX.search(out)
        if show:
            self.assertIsNotNone(box, out[-2000:])
        else:
            self.assertIsNone(box, out[-2000:])
        return shot, h.group(1), tuple(int(v) for v in box.groups()) if box else None

    def check(self, window, extra=()):
        off_shot, off_hash, _ = self.run_app(window, False, extra)
        on_shot, on_hash, box = self.run_app(window, True, extra)
        where = f"{window} {extra}: {box}"
        # The machine's picture is not touched.
        self.assertEqual(on_hash, off_hash, where)
        x, y, bw, bh, ww, wh = box
        self.assertEqual(f"{ww}x{wh}", window, where)
        self.assertGreater(bw, 0, where)
        self.assertGreater(bh, 0, where)
        self.assertGreaterEqual(x, 0, where)
        self.assertGreaterEqual(y, 0, where)
        self.assertLessEqual(x + bw, ww, where)
        self.assertLessEqual(y + bh, wh // 2, where)  # top of the window, away from the touch buttons
        self.assertGreater(x + bw / 2, ww / 2, where)  # the right half: the HUD is top left
        w0, h0, bpp0, off = read_png(off_shot)
        w1, h1, bpp1, on = read_png(on_shot)
        self.assertEqual((w0, h0, bpp0), (w1, h1, bpp1))
        self.assertEqual((w0, h0), (ww, wh))
        inside = outside = 0
        ox0 = oy0 = 1 << 30
        ox1 = oy1 = -1
        for row in range(h0):
            if off[row] == on[row]:
                continue
            for col in range(w0):
                a = off[row][col * bpp0:(col + 1) * bpp0]
                if a == on[row][col * bpp0:(col + 1) * bpp0]:
                    continue
                if x <= col < x + bw and y <= row < y + bh:
                    inside += 1
                else:
                    outside += 1
                    ox0, oy0, ox1, oy1 = min(ox0, col), min(oy0, row), max(ox1, col), max(oy1, row)
        self.assertEqual(outside, 0, f"{where}: {outside} pixels changed outside the counter's box, "
                                     f"within {ox0},{oy0}-{ox1},{oy1}")
        # Its text at least (the box's shade is invisible over black bars).
        self.assertGreater(inside, 200, f"{where}: the counter drew only {inside} pixels")
        return box

    def test_a_phone_in_landscape_with_touch_controls(self):
        normal = self.check("800x360", ("--touch", "--menu-size", "normal"))
        large = self.check("800x360", ("--touch", "--menu-size", "extralarge"))
        # MENU SIZE makes it larger too.
        self.assertGreater(large[3], normal[3])
        self.assertGreater(large[2], normal[2])

    def test_a_portrait_window(self):
        self.check("412x860", ("--touch",))

    def test_a_desktop_window(self):
        self.check("1280x720")


if __name__ == "__main__":
    unittest.main()
