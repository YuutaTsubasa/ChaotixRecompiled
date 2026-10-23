#!/usr/bin/env python3
"""Netplay over real UDP (issue #46): two chaotix_headless processes on this
machine, one hosting and one joining, play a stretch of a level in two-player
mode with fuzzed pads. Each must end with its state at the last frame equal
to the other's and to a single machine fed the confirmed pads of both.

usage: test_netplay_udp.py <chaotix_headless> <rom>
"""
import random
import re
import socket
import subprocess
import sys
import time

RESULT = re.compile(r"^netplay: frame (\d+): (player\d) ([0-9a-f]+), the other player ([0-9a-f]+), "
                    r"reference ([0-9a-f]+): (\w+)", re.M)


def free_port():
    # A port nothing listens on now (the host binds it a moment later).
    for _ in range(20):
        port = random.randint(20000, 60000)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.bind(("", port))
            return port
        except OSError:
            continue
        finally:
            s.close()
    return 47845


def main():
    headless, rom = sys.argv[1], sys.argv[2]
    port = free_port()
    common = ["--rom", rom, "--frames", "600", "--netplay-timeout", "120"]
    host = subprocess.Popen([headless] + common + ["--stage", "300:4:2:2:2:7:1", "--netplay-host", str(port),
                                                   "--netplay-fuzz", "5"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    time.sleep(1.0)
    client = subprocess.run([headless] + common + ["--netplay-join", "127.0.0.1:%d" % port, "--netplay-fuzz", "5",
                                                   "--netplay-delay", "1"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, timeout=300)
    host_out, _ = host.communicate(timeout=300)
    outs = {"host": (host.returncode, host_out), "client": (client.returncode, client.stdout)}
    hashes = []
    ok = True
    for name, (code, out) in outs.items():
        lines = [l for l in out.splitlines() if l.startswith("netplay:")]
        print("%s (exit %d):" % (name, code))
        for l in lines:
            print("  " + l)
        m = RESULT.search(out)
        if code != 0 or not m or m.group(6) != "identical":
            ok = False
            continue
        hashes.append(m.group(3))
    if ok and (len(hashes) != 2 or hashes[0] != hashes[1]):
        print("the two processes ended with different states: %s" % hashes)
        ok = False
    print("netplay over UDP: %s" % ("both peers identical" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
