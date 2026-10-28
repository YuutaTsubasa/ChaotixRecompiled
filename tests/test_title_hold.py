"""The title held under the front end, in the real program (issue #3); supply a locally owned ROM.

Run: python tests/test_title_hold.py path/to/ChaotixRecompiled.exe path/to/game.32x

The program is run headless (SDL's dummy video driver) and says at the end of
an autotest what scene the game is in, the title's idle counter, whether the
game has been handed over and what the menu shows (runtime/title_hold.h):

- with the front end left open well past the point the attract demo starts
  (about frame 2024), the game is still on its title, the entries on it;
- with the front end closed, the demo starts as in the original;
- before PUSH START is up (the intro) the front end shows no entries, and a
  press of Enter there skips to the title without choosing anything;
- START GAME after that skip hands the game over as before.

Only temporary user data is touched.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

EXE, ROM = (Path(p).resolve() for p in sys.argv[1:3])
del sys.argv[1:3]

SCENE = re.compile(r"autotest: game mode ([0-9A-F]{4}), title counter ([0-9A-F]{4}), (handed over|front end), menu (\w+)")
ROWS = re.compile(r"autotest: page (\w+), size \d+, (\d+) rows")

TITLE, DEMO = "0008", "0038"


class TitleHold(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="chaotix-title-")
        cls.root = Path(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_app(self, frames, keys=None):
        store = self.root / f"user data {frames} {keys or 'none'}".replace(":", "_").replace(",", "_")
        (store / "Config").mkdir(parents=True, exist_ok=True)
        (store / "Config" / "chaotix.ini").write_text("[Achievements]\nEnabled = false\n", encoding="utf-8")
        env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software")
        args = [str(EXE), "--user-dir", str(store), "--windowed", "--window-size", "1280x720",
                "--autotest", str(frames)]
        if keys:
            args += ["--keys", keys]
        args.append(str(ROM))
        result = subprocess.run(args, cwd=self.root, env=env, capture_output=True, text=True, errors="replace",
                                timeout=600)
        out = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, out[-2000:])
        s = SCENE.search(out)
        self.assertIsNotNone(s, out[-2000:])
        r = ROWS.search(out)
        mode, counter, who, menu = s.groups()
        return mode, int(counter, 16), who, menu, int(r.group(2)) if r else None, out

    def test_held_on_the_title_with_the_front_end_open(self):
        mode, counter, who, menu, rows, out = self.run_app(4300)
        self.assertEqual(mode, TITLE, out[-2000:])
        self.assertEqual(who, "front end")
        self.assertEqual(menu, "front")
        self.assertLess(counter, 0x400)
        self.assertGreater(rows, 0)

    def test_the_demo_starts_with_the_front_end_closed(self):
        mode, _, who, menu, _, out = self.run_app(2300, "10:Escape")
        self.assertEqual(mode, DEMO, out[-2000:])
        self.assertEqual(who, "handed over")
        self.assertEqual(menu, "closed")

    def test_only_the_hint_before_push_start(self):
        mode, counter, who, menu, rows, out = self.run_app(1000)
        self.assertEqual((mode, counter), (TITLE, 0), out[-2000:])  # the intro
        self.assertEqual((who, menu), ("front end", "front"))
        self.assertEqual(rows, 0)

    def test_a_press_in_the_intro_skips_to_the_title_choosing_nothing(self):
        mode, counter, who, menu, rows, out = self.run_app(700, "200:Return")
        self.assertEqual(mode, TITLE, out[-2000:])
        self.assertGreater(counter, 0)
        self.assertEqual((who, menu), ("front end", "front"))
        self.assertGreater(rows, 0)

    def test_start_game_after_the_skip_hands_over(self):
        _, _, who, menu, _, out = self.run_app(1200, "200:Return,800:Return")
        self.assertEqual((who, menu), ("handed over", "closed"), out[-2000:])


if __name__ == "__main__":
    unittest.main(verbosity=2)
