#!/usr/bin/env python3
"""tools/diag/awdl_ctl.py builds the AWDL_CTL request the way the family's setAWDL_CTL reads it (24H232 0xffffff800221bd6f):
exact length 0x3B8A0, name at +4, value at +0x88, extra-data length at +0x90 (0 would make the handler succeed without doing
anything, so it must never be 0)."""
import importlib.util
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("awdl_ctl", root / "tools/diag/awdl_ctl.py")
ctl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ctl)

body = ctl.build_payload("ndpRemovalTimeoutmSec", 100000)
assert len(body) == 0x3B8A0
assert body[4:4 + 21] == b"ndpRemovalTimeoutmSec" and body[4 + 21] == 0
assert struct.unpack_from("<Q", body, 0x88)[0] == 100000
assert struct.unpack_from("<I", body, 0x90)[0] == 4          # never 0: the handler returns success without calling setAwdlCtl
assert body[0:4] == bytes(4) and body[0x94:0x98] == bytes(4)

hdr = ctl.build_request_header("awdl0", 0x1234)
assert len(hdr) == 0x28
name, rtype, rval, rlen, pad, ptr = struct.unpack("<16sIiIIQ", hdr)
assert name.rstrip(b"\0") == b"awdl0" and rtype == 162 and rlen == 0x3B8A0 and ptr == 0x1234
assert ctl.SIOCSA80211 == 0x802869C8

for bad in (lambda: ctl.build_payload("", 1), lambda: ctl.build_payload("x" * 128, 1), lambda: ctl.build_payload("a", 1, 0)):
    try:
        bad()
    except ValueError:
        continue
    raise AssertionError("expected ValueError")
print("awdl_ctl request layout: passed")
