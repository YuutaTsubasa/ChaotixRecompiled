"""MENU SIZE in the real program (issue #49); supply a locally owned ROM.

Run: python tests/test_menu_size.py path/to/ChaotixRecompiled.exe path/to/game.32x

The program is run headless (SDL's dummy video driver, the software renderer)
with a menu page open at each MENU SIZE in a phone-shaped window, a portrait
one and a desktop one. At the end of the run it logs where the menu's touch
targets were; they must all be on the screen, and each size's must be taller
than the size's before. The screenshot it takes must be the window's size.
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

EXE, ROM = (Path(p).resolve() for p in sys.argv[1:3])
del sys.argv[1:3]

SIZES = ["normal", "large", "extralarge"]
LINE = re.compile(r"autotest: page (\w+), size (\d+), (\d+) rows, ([\d.]+) tall on average, "
                  r"within (-?[\d.]+),(-?[\d.]+)-(-?[\d.]+),(-?[\d.]+) of (\d+)x(\d+)")


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    assert head[:8] == b"\x89PNG\r\n\x1a\n", path
    return struct.unpack(">II", head[16:24])


class MenuSize(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="chaotix-menu-size-")
        cls.root = Path(cls.tmp.name)
        # One user folder for every run, so the menu's zone pictures are made
        # once and then come from its cache.
        cls.store = cls.root / "user data"

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_menu(self, window, page, size):
        shot = self.root / f"{page}_{window}_{size}.png"
        env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software")
        result = subprocess.run([str(EXE), "--user-dir", str(self.store), "--windowed", "--window-size", window,
                                 "--autotest", "200", "--autotest-shot", str(shot), "--menu", page,
                                 "--menu-size", size, str(ROM)],
                                cwd=self.root, env=env, capture_output=True, text=True, errors="replace",
                                timeout=180)
        out = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, out[-2000:])
        m = LINE.search(out)
        self.assertIsNotNone(m, out[-2000:])
        self.assertEqual(m.group(1), page)
        self.assertEqual(int(m.group(2)), SIZES.index(size))
        w, h = int(m.group(9)), int(m.group(10))
        self.assertEqual(f"{w}x{h}", window)
        self.assertEqual(png_size(shot), (w, h))
        rows = int(m.group(3))
        x0, y0, x1, y1 = (float(m.group(i)) for i in range(5, 9))
        where = f"{page} {window} {size}: {m.group(0)}"
        self.assertGreater(rows, 0, where)
        self.assertGreaterEqual(x0, -0.5, where)
        self.assertGreaterEqual(y0, -0.5, where)
        self.assertLessEqual(x1, w + 0.5, where)
        self.assertLessEqual(y1, h + 0.5, where)
        return float(m.group(4))

    def check(self, window, pages):
        for page in pages:
            heights = [self.run_menu(window, page, size) for size in SIZES]
            for smaller, larger in zip(heights, heights[1:]):
                self.assertGreater(larger, smaller + 1.0, f"{page} {window}: {heights}")

    def test_a_phone_in_landscape(self):
        self.check("800x360", ["front", "main", "video", "levelselect", "timeattack"])

    def test_a_portrait_window(self):
        self.check("412x860", ["front", "main", "video", "timeattack"])

    def test_a_desktop_window(self):
        self.check("1280x720", ["front", "video"])


if __name__ == "__main__":
    unittest.main()
