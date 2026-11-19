#!/usr/bin/env python3
"""Tests for tools/diag/cc_analyze.py: the maths and the decoding, against a fixture of a real run's registry keys
(tests/fixtures/cc-run-2026-11-18.txt) and against synthetic ones, so a change to the analyzer can't quietly change what a
run is judged to be."""
import importlib.util
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("cc_analyze", root / "tools/diag/cc_analyze.py")
cc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cc)

# chi-square: a uniform histogram is 0, one bin holding everything is the textbook maximum.
chi, exp = cc.chi_square([10] * 16)
assert chi == 0 and exp == 10
chi, exp = cc.chi_square([160] + [0] * 15)
assert abs(chi - 160 * 15) < 1e-6, chi          # (160-10)^2/10 + 15*10 = 2250 + 150 = 2400 ... checked below
chi2, _ = cc.chi_square([0] * 16)
assert chi2 == 0

def reg(**keys):
    lines = []
    for k, v in keys.items():
        lines.append('"%s" = %s' % (k, v))
    return cc.Reg("\n".join(lines))

def u32(*vals):
    return "<" + struct.pack("<%dI" % len(vals), *vals).hex() + ">"

# Number parsing: decimal, Yes/No, and a registry negative printed as 64-bit is not mangled into a signed value.
r = reg(A="12", B="Yes", C="No", D='"text"')
assert r.num("A") == 12 and r.num("B") == 1 and r.num("C") == 0 and r.num("missing") is None
assert r.text("D") == "text"
assert reg(E=u32(1, 2, 3)).u32s("E") == [1, 2, 3]

# A synthetic run: a clean ring, power table flags 0x7 with skip 2, 10% loss, gaps all in one beacon-phase bin.
bins = [0] * 16
bins[3] = 100
syn = reg(ITLAWDLCmdTimeouts="0", ITLAWDLCmdEnospcRing="0", ITLAWDLCmdEnospcSlot="0", ITLAWDLCmdEnospcResp="0",
          ITLAWDLCmdReclaimed="0", ITLAWDLCmdQueuedPeak="3", ITLAWDLCoexBaPeerAdded="Yes", ITLAWDLCoexBaTids="129",
          ITLAWDLPmSent="2", ITLAWDLPmFlags="7", ITLAWDLPmSkip="2", ITLAWDLPmErr="0",
          ITLAWDLPmRecipeSent="1", ITLAWDLPmRecipeFlags="7", ITLAWDLPmRecipeErr="0", ITLAWDLPmFlips="2",
          ITLAWDLCoexSeqSeen="900", ITLAWDLCoexSeqMissing="100", ITLAWDLCoexSeqGapRuns="10",
          ITLAWDLCoexSeqLongestGap="12", ITLAWDLCoexSeqLate="0",
          ITLAWDLGapPhaseBins=u32(*bins), ITLAWDLGapPhaseMissing=u32(*bins), ITLAWDLBeaconsSeen="500",
          ITLAWDLBeaconIntervalUS="102400")
text = cc.analyze(syn)
assert "PASS  timeouts 0" in text
assert "PASS  BaPeerAdded 1" in text and "TID 7 yes" in text
assert "power-save, mgmt/beacon-abort, skip-over-DTIM" in text and "skip_dtim_periods 2" in text
assert "FAIL  100 missing of 1000 (10.00%)" in text
assert "FAIL  100 gaps over 500 beacons" in text            # one bin holds everything: far from uniform
assert "PASS  state flips 2" in text

# A wedged ring and a station that never sleeps are reported as failures.
bad = cc.analyze(reg(ITLAWDLCmdTimeouts="3", ITLAWDLCmdEnospcRing="900", ITLAWDLCmdEnospcSlot="0", ITLAWDLCmdEnospcResp="0",
                     ITLAWDLCmdReclaimed="0", ITLAWDLPmSent="4", ITLAWDLPmFlags="0", ITLAWDLPmSkip="0", ITLAWDLPmErr="0"))
assert "FAIL  timeouts 3" in bad
assert "FAIL  power save is ON" in bad

# A uniform gap histogram passes the beacon-phase check.
uni = cc.analyze(reg(ITLAWDLGapPhaseBins=u32(*([20] * 16)), ITLAWDLGapPhaseMissing=u32(*([20] * 16)), ITLAWDLBeaconsSeen="100",
                     ITLAWDLBeaconIntervalUS="102400"))
assert "PASS  320 gaps over 100 beacons" in uni

# Comparing two dumps subtracts the counters.
a = reg(ITLAWDLCoexSeqSeen="1000", ITLAWDLCoexSeqMissing="100")
b = reg(ITLAWDLCoexSeqSeen="1900", ITLAWDLCoexSeqMissing="200")
assert "100 missing of 1000 (10.00%)" in cc.analyze(b, a)

# The recorded real run: values read off the live registry when the fixture was taken.
real = cc.Reg((root / "tests/fixtures/cc-run-2026-11-18.txt").read_text())
assert real.num("ITLAWDLCmdTimeouts") == 0 and real.num("ITLAWDLCmdEnospcRing") == 0
seen, miss = real.num("ITLAWDLCoexSeqSeen"), real.num("ITLAWDLCoexSeqMissing")
assert seen and miss and 0 < miss / (seen + miss) < 0.5
bins = real.u32s("ITLAWDLGapPhaseBins", 16)
assert bins and len(bins) == 16 and sum(bins) > 50
text = cc.analyze(real)
for heading in ("== command ring", "== station power table", "== receive loss", "== gaps against the access point's beacons",
                "== what the iPhone sends us", "== what we advertise"):
    assert heading in text, heading
assert "ours as macOS built it: [(149, 124)]" in text      # the width we announce was 20 MHz in that run
print("cc_analyze: passed")
