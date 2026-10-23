#!/usr/bin/env python3
"""The LiveSplit block (issue #47, src/frontend/livesplit.h) is in each program
an auto-splitter attaches to, exactly once, as tools/livesplit/ChaotixRecompiled.asl
expects: the magic, NUL padded to 24 bytes, then version 1 and size 72, and
the RAM pointer still 0 (it is set when the game starts).

usage: test_livesplit_signature.py PROGRAM...
"""
import struct
import sys

MAGIC = b"CHAOTIX_" + b"LIVESPLIT"


def check(path):
    with open(path, "rb") as f:
        data = f.read()
    hits = []
    at = data.find(MAGIC)
    while at >= 0:
        hits.append(at)
        at = data.find(MAGIC, at + 1)
    if len(hits) != 1:
        print(f"{path}: the magic appears {len(hits)} times, expected once")
        return False
    at = hits[0]
    block = data[at:at + 72]
    if len(block) < 72:
        print(f"{path}: the block is cut short")
        return False
    magic, version, size, wram = struct.unpack_from("<24sIIQ", block)
    if magic != MAGIC + b"\0" * (24 - len(MAGIC)):
        print(f"{path}: the magic is not NUL padded to 24 bytes: {magic!r}")
        return False
    if version != 1 or size != 72 or wram != 0:
        print(f"{path}: version {version}, size {size}, RAM pointer {wram:#x}; expected 1, 72, 0")
        return False
    print(f"{path}: block at file offset {at:#x}, version {version}, size {size}")
    return True


def main():
    ok = all([check(p) for p in sys.argv[1:]])
    print("livesplit signature: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
