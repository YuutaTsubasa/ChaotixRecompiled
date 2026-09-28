"""Frontend ROM persistence regressions; supply a locally owned verified ROM.

Run: python tests/test_rom_startup.py path/to/ChaotixRecompiled.exe path/to/game.32x
Only temporary user data is touched. No game data is stored in the repository.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE, ROM = (Path(p).resolve() for p in sys.argv[1:3])
del sys.argv[1:3]
EXPECTED = "0c2fff7bc79ed26507c08ac47464c3af19f7ced7"


class RomStartup(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="chaotix-issue4-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.store = self.root / "user data"
        self.installed = self.store / "Game/chaotix.32x"
        self.config = self.store / "Config/chaotix.ini"
        self.good = self.root / "verified game.32x"
        self.good.write_bytes(ROM.read_bytes())
        self.assertEqual(self.hash(self.good), EXPECTED)
        self.bad = self.root / "unknown.32x"
        data = bytearray(512 * 1024)
        data[0x100:0x108] = b"SEGA 32X"
        self.bad.write_bytes(data)

    def hash(self, path):
        return hashlib.sha1(path.read_bytes()).hexdigest()

    def run_app(self, *args):
        env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy",
                   SDL_RENDER_DRIVER="software")
        return subprocess.run([str(EXE), "--user-dir", str(self.store), *map(str, args)],
                              cwd=self.root, env=env, capture_output=True, text=True,
                              errors="replace", timeout=30)

    def seed_legacy_install(self):
        self.installed.parent.mkdir(parents=True)
        self.installed.write_bytes(self.bad.read_bytes())
        self.config.parent.mkdir(parents=True)
        self.config.write_text("[Game]\nInstalled = true\nRomPath = " + str(self.installed)
                               + "\nRomSha1 = " + self.hash(self.bad) + "\n")

    def test_install_rejects_unknown_without_replacing_good_copy(self):
        self.assertEqual(self.run_app("--install", self.good).returncode, 0)
        config = self.config.read_bytes()
        result = self.run_app("--install", self.bad)
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertIn(self.hash(self.bad), result.stderr)
        self.assertIn(EXPECTED, result.stderr)
        self.assertIn(str(self.bad), result.stderr)
        self.assertEqual(self.hash(self.installed), EXPECTED)
        self.assertEqual(self.config.read_bytes(), config)

    def test_drag_to_exe_installs_and_survives_source_removal(self):
        result = self.run_app(self.good, "--autotest", "2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.installed.exists(), "positional ROM was not installed")
        self.assertEqual(self.hash(self.installed), EXPECTED)
        self.good.unlink()
        result = self.run_app("--autotest", "2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("static recompilation", result.stderr)

    def test_drag_to_exe_replaces_legacy_unknown_install(self):
        self.seed_legacy_install()
        result = self.run_app(self.good, "--autotest", "2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.hash(self.installed), EXPECTED)
        self.assertIn(EXPECTED, self.config.read_text())
        self.good.unlink()
        result = self.run_app("--autotest", "2")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unknown_legacy_install_requires_setup_instead_of_running(self):
        self.seed_legacy_install()
        result = self.run_app("--autotest", "2")
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertIn("no usable ROM", result.stderr)
        self.assertIn(self.hash(self.bad), result.stderr)
        self.assertNotIn("execution:", result.stderr)

    def test_explicit_invalid_rom_does_not_replace_or_boot_installed_copy(self):
        self.assertEqual(self.run_app("--install", self.good).returncode, 0)
        config = self.config.read_bytes()
        result = self.run_app(self.bad, "--autotest", "2")
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertIn("no usable ROM", result.stderr)
        self.assertIn(str(self.bad), result.stderr)
        self.assertNotIn("execution:", result.stderr)
        self.assertEqual(self.hash(self.installed), EXPECTED)
        self.assertEqual(self.config.read_bytes(), config)


if __name__ == "__main__":
    unittest.main()
