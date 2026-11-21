#!/usr/bin/env python3
"""Which interface carries an AirDrop transfer: awdl0 (AWDL through itlwm), en1 (the AX200 on the router, also itlwm) or
en0 (wired Ethernet, not itlwm)?

Samples `netstat -I <if> -b` while a transfer runs and prints each interface's bytes in and out, its share of the
total, and the peak rate. The 2026-11-20 captures showed the Mac -> iPhone bulk data (18-21 MB) leaving on the wired
en0 (QUIC multipath over the router) with only the first ~2 MB on awdl0, while the real Mac sent everything on awdl0.

    python3 -I tools/diag/airdrop_paths.py [seconds=60] [interfaces=en0,en1,awdl0]

Start it, then send the file. Exit status 1 when en0 carried more than 1% of the bytes (or any bytes when it is not
up), so it can sit in a check script.
"""
import subprocess, sys, time

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
names = (sys.argv[2] if len(sys.argv) > 2 else "en0,en1,awdl0").split(",")


def sample(name):
    out = subprocess.run(["netstat", "-I", name, "-b"], capture_output=True, text=True).stdout.splitlines()
    best = None
    for line in out[1:]:
        f = line.split()
        if not f or not f[0].rstrip("*") == name:
            continue
        nums = [x for x in f if x.isdigit()]
        if len(nums) >= 6 and "<Link#" in line:
            # Ipkts Ierrs Ibytes Opkts Oerrs Obytes Coll are the last seven numeric columns on the link row.
            return int(nums[-5]), int(nums[-2])
    return best


first = {n: sample(n) for n in names}
peak = {n: 0.0 for n in names}
prev, prev_t = dict(first), time.time()
end = prev_t + secs
while time.time() < end:
    time.sleep(0.5)
    now = time.time()
    for n in names:
        cur = sample(n)
        if cur and prev.get(n):
            rate = ((cur[0] - prev[n][0]) + (cur[1] - prev[n][1])) * 8 / (now - prev_t) / 1e6
            peak[n] = max(peak[n], rate)
        prev[n] = cur
    prev_t = now
tot = {}
for n in names:
    a, b = first[n], prev[n]
    tot[n] = ((b[0] - a[0]), (b[1] - a[1])) if a and b else (0, 0)
grand = sum(i + o for i, o in tot.values()) or 1
print("%-6s %12s %12s %7s %10s" % ("if", "bytes in", "bytes out", "share", "peak Mbit/s"))
for n in names:
    i, o = tot[n]
    print("%-6s %12d %12d %6.1f%% %10.1f" % (n, i, o, 100.0 * (i + o) / grand, peak[n]))
en0 = sum(tot.get("en0", (0, 0))) if "en0" in tot else 0
bad = en0 > 0.01 * grand
print("en0 carried %.1f%% of the bytes: %s" % (100.0 * en0 / grand, "NOT through itlwm" if bad else "ok"))
sys.exit(1 if bad else 0)
