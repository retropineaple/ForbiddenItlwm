#!/usr/bin/env python3
"""Reads the association timeline the driver keeps (ItlwmWCLTimeline) and prints each step with the time since the
previous one, so a slow scan or join can be taken apart.

    python3 -I tools/diag/assoc_timeline.py            # from the live registry
    python3 -I tools/diag/assoc_timeline.py ioreg.txt  # from a saved `ioreg -l -w0`

Entries are 8 bytes: u32 milliseconds since the first mark, u8 kind, u8 arg, u16 val (the last 96, oldest first).
"""
import re, struct, subprocess, sys

KINDS = {1: "scan requested", 2: "scan accepted", 3: "scan refused", 4: "scan done event (net80211 state in arg)",
         5: "scan finished (WCL)", 6: "join requested", 7: "join started", 8: "join refused",
         9: "join channel (0 = all channels)", 10: "association done (status in val)", 11: "first beacon",
         12: "link up", 13: "link down", 14: "deauth (reason in val)", 15: "join finished (status in arg)",
         16: "driver ready", 17: "driver stopped", 18: "scan parameters (arg: type, 2 = passive; val: channels asked for, 0 = all)",
         19: "beacons and probe responses heard during that scan (val)"}


def entries(text):
    m = re.search(r'"ItlwmWCLTimeline" = <([0-9a-f]*)>', text)
    if not m:
        return []
    raw = bytes.fromhex(m.group(1))
    return [struct.unpack("<IBBH", raw[i:i + 8]) for i in range(0, len(raw) - 7, 8)]


def main():
    if len(sys.argv) > 1:
        text = open(sys.argv[1], errors="ignore").read()
    else:
        text = subprocess.run(["ioreg", "-l", "-w0"], capture_output=True, text=True).stdout
    rows = entries(text)
    if not rows:
        print("no ItlwmWCLTimeline in the registry (driver without the association timeline, or no scan or join yet)")
        return 1
    prev = None
    print("%9s %8s  %s" % ("at ms", "+ms", "step"))
    for ms, kind, arg, val in rows:
        name = KINDS.get(kind, "kind %d" % kind)
        extra = " arg=%d" % arg if arg else ""
        extra += " val=%d (0x%x)" % (val, val) if val else ""
        print("%9d %8s  %s%s" % (ms, "" if prev is None else "+%d" % (ms - prev), name, extra))
        prev = ms
    return 0


if __name__ == "__main__":
    sys.exit(main())
