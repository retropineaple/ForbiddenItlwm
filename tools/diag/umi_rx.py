#!/usr/bin/env python3
"""What the peer sent in the 3 s after our UMI was acknowledged (ItlwmAWDLUmiRxRing), decoded.
   python3 -I tools/diag/umi_rx.py"""
import re
import struct
import subprocess
import sys

out = subprocess.run(["ioreg", "-l", "-w0"], capture_output=True, text=True).stdout
m = re.search(r'"ItlwmAWDLUmiRxRing" = <([0-9a-f]+)>', out)
if not m:
    sys.exit("no ItlwmAWDLUmiRxRing yet: it is published when the first frame arrives more than 3 s after an acknowledged UMI")
raw = bytes.fromhex(m.group(1))
size = 20
print(f"{len(raw) // size} action frames in the 3 s after the UMI (ms after, to us / from the UMI's peer / unicast, subtype, length, rssi, channel, TLV types)")
names = {0: "PSF", 3: "MIF"}
for i in range(len(raw) // size):
    ms, flags, subtype, length, rssi, channel, count = struct.unpack_from("<HBBHbBB", raw, i * size)
    types = list(raw[i * size + 9:i * size + 9 + min(count, 11)])
    more = "" if count <= 11 else f" (+{count - 11} more)"
    print(f"  {ms:5d} ms  {'to-us ' if flags & 1 else '      '}{'from-peer ' if flags & 2 else '          '}{'unicast ' if flags & 4 else 'bcast   '}"
          f"{names.get(subtype, subtype)} len {length} rssi {rssi} ch {channel} TLVs {types}{more}")
