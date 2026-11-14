#!/usr/bin/env python3
"""Validate event-mask bytes against an actual 25G332/25G342 bluetoothd."""
import argparse
import re
import struct
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--set", choices=("2672", "2673"), default="2673")
p.add_argument("--binary", type=Path, default=Path("/usr/sbin/bluetoothd"))
args = p.parse_args()
source = (Path(__file__).resolve().parents[1] / "BTLEAdvFixup/BTLEAdvEventMask.hpp").read_text()
raw = args.binary.read_bytes()
if raw[:4] == bytes.fromhex("cafebabe"):
    for i in range(struct.unpack_from(">I", raw, 4)[0]):
        cpu, _, offset, size, _ = struct.unpack_from(">5I", raw, 8 + 20*i)
        if cpu == 0x01000007:
            raw = raw[offset:offset+size]
            break
assert raw[:4] == bytes.fromhex("cffaedfe"), "expected x86_64 Mach-O"
def table(kind, suffix):
    match = re.search(r"eventMask" + kind + suffix + r"\[\] = \{(.*?)\}", source)
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", match[1]))
find, replace = table("Find", args.set), table("Replace", args.set)
assert len(find) == len(replace) == 20
assert find[:16] == replace[:16] and replace[16:] == bytes.fromhex("0f1f4000")
sites = [m.start() for m in re.finditer(re.escape(find), raw)]
sites += [m.start() for m in re.finditer(re.escape(replace), raw)]
assert len(sites) == 1, sites
assert sites[0] // 4096 == (sites[0] + len(find) - 1) // 4096
other = "2672" if args.set == "2673" else "2673"
assert raw.count(table("Find", other)) == raw.count(table("Replace", other)) == 0
print("BTLEAdv event mask", args.set, "unique in-page site:", hex(sites[0]), "(offline only)")
