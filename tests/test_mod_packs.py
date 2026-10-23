#!/usr/bin/env python3
"""Mod packs with the real ROM (issue #42).

Two packs made here replace the same 32X sprites and Mega Drive tiles on the
title screen with solid images of different colours (only the graphics'
names and sizes come from a dump; no pixel of the ROM is used). Then:

- the order decides: whichever pack is higher is the one seen, and lockstep
  holds with the packs on both machines;
- a disabled pack has no effect: with one off, the picture is the other's;
- loose files in the Mods folder's own Textures folder still load, and come
  above every pack;
- a pack's ROM patch is applied only while the pack is enabled.

usage: test_mod_packs.py HEADLESS ROM
"""
import glob
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

HEADLESS, ROM = sys.argv[1:3]
del sys.argv[1:3]

FRAMES = 1200


def run(*args):
    r = subprocess.run([HEADLESS, "--rom", ROM, "--frames", str(FRAMES), "--hash-every", "20", *args],
                       capture_output=True, text=True, timeout=900)
    out = r.stdout + r.stderr
    return out, dict(re.findall(r"^hash (\d+) ([0-9a-f]+)$", out, re.M))


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def png_size(path):
    with open(path, "rb") as f:
        data = f.read(24)
    return struct.unpack(">II", data[16:24])


def write_solid(path, width, height, index):
    """An 8-bit palette-indexed PNG with every pixel `index`."""
    palette = b"".join(bytes((i, i, i)) for i in range(256))
    raw = b"".join(b"\0" + bytes([index]) * width for _ in range(height))
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)) +
            chunk(b"PLTE", palette) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def ips(records):
    out = b"PATCH"
    for off, data in records:
        out += off.to_bytes(3, "big") + len(data).to_bytes(2, "big") + data
    return out + b"EOF"


class ModPacks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp(prefix="chaotix_mod_packs_")
        dump = os.path.join(cls.dir, "dump")
        os.makedirs(dump)
        out, cls.plain = run("--dump-graphics", dump)
        cls.names = [os.path.basename(p) for p in glob.glob(os.path.join(dump, "32x_??_??.png"))]
        cls.names += [os.path.basename(p) for p in glob.glob(os.path.join(dump, "md_*.png"))]
        cls.sizes = {n: png_size(os.path.join(dump, n)) for n in cls.names}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def textures(self, folder, index):
        for n in self.names:
            w, h = self.sizes[n]
            write_solid(os.path.join(folder, n), w, h, index)

    def mods(self, name):
        root = os.path.join(self.dir, name)
        shutil.rmtree(root, ignore_errors=True)
        os.makedirs(root)
        return root

    def pack(self, root, folder, index, ini=True):
        if ini:
            os.makedirs(os.path.join(root, folder), exist_ok=True)
            with open(os.path.join(root, folder, "mod.ini"), "w", encoding="utf-8") as f:
                f.write("name = %s\nauthor = test\nversion = 1.0\n" % folder)
        self.textures(os.path.join(root, folder, "Textures"), index)

    def test_order_decides_and_a_disabled_pack_has_no_effect(self):
        self.assertGreater(len([n for n in self.names if n.startswith("32x_")]), 5)
        self.assertGreater(len([n for n in self.names if n.startswith("md_")]), 50)
        root = self.mods("two")
        self.pack(root, "Red", 7)
        self.pack(root, "Blue", 9, ini=False)  # a pack unpacked without its mod.ini still counts
        red_top, a = run("--mods-dir", root, "--mods-order", "Red,Blue", "--lockstep")
        self.assertIn("bit-identical", red_top, red_top[-2000:])
        self.assertIn("mods: on  Red (Red)", red_top)
        self.assertIn("mods: on  Blue (Blue)", red_top)
        _, b = run("--mods-dir", root, "--mods-order", "Blue,Red")
        _, red_only = run("--mods-dir", root, "--mods-disable", "Blue")
        _, blue_only = run("--mods-dir", root, "--mods-order", "Red,Blue", "--mods-disable", "Red")
        _, none = run("--mods-dir", root, "--mods-disable", "Red,Blue")
        frames = sorted(self.plain, key=int)
        changed = [f for f in frames if a.get(f) != self.plain[f]]
        self.assertGreater(len(changed), len(frames) // 2, "the packs changed nothing")
        differ = [f for f in changed if a.get(f) != b.get(f)]
        self.assertGreater(len(differ), len(changed) // 2, "the order made no difference")
        self.assertEqual(a, red_only, "Red on top is not Red's picture")
        self.assertEqual(b, blue_only, "Blue on top is not Blue's picture")
        self.assertEqual(none, self.plain, "disabled packs changed the picture")

    def test_loose_files_still_load_and_come_first(self):
        root = self.mods("loose")
        self.textures(os.path.join(root, "Textures"), 5)
        out, loose = run("--mods-dir", root)
        self.assertIn("mods: on  Loose files (loose)", out)
        _, direct = run("--tiles", os.path.join(root, "Textures"))
        self.assertEqual(loose, direct, "loose files differ from the folder loaded on its own")
        self.assertNotEqual(loose, self.plain)
        self.pack(root, "Red", 7)
        _, both = run("--mods-dir", root)
        self.assertEqual(both, loose, "a pack came above the loose files")
        _, without = run("--mods-dir", root, "--mods-no-loose")
        _, red = run("--tiles", os.path.join(root, "Red", "Textures"))
        self.assertEqual(without, red)

    def test_a_disabled_packs_patch_is_not_applied(self):
        root = self.mods("patch")
        os.makedirs(os.path.join(root, "Hack", "Patches"))
        with open(os.path.join(root, "Hack", "mod.ini"), "w", encoding="utf-8") as f:
            f.write("name = Hack\n")
        # Two letters of the header's domestic name: data nothing runs.
        with open(os.path.join(root, "Hack", "Patches", "name.ips"), "wb") as f:
            f.write(ips([(0x120, b"KN")]))
        on, _ = run("--mods-dir", root, "--lockstep")
        self.assertIn("patch: 1 applied", on, on[-2000:])
        self.assertIn("bit-identical", on, on[-2000:])
        off, _ = run("--mods-dir", root, "--mods-disable", "Hack")
        self.assertIn("mods: off Hack (Hack)", off)
        self.assertNotRegex(off, r"(?m)^patch:")


if __name__ == "__main__":
    unittest.main()
