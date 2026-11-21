#!/usr/bin/env python3
"""Tests for tools/diag/airdrop_check.py: the parsers, the failure signatures and the verdict, on the real output of the
2026-11-18 failure (awdl0 at 1484 under two wired interfaces at 1500; five sends dying with unexpected-40)."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("airdrop_check", root / "tools/diag/airdrop_check.py")
ac = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ac)

IFCONFIG = """lo0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST> mtu 16384
\toptions=1203<RXCSUM,TXCSUM,TXSTATUS,SW_TIMESTAMP>
en0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500
\tether 4e:8d:78:d7:3c:7c
\tmedia: autoselect (1000baseT <full-duplex>)
\tstatus: active
en1: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500
\tmedia: autoselect
\tstatus: active
awdl0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1484
\tmedia: autoselect
\tstatus: active
llw0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1484
\tstatus: active
en5: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500
\tstatus: inactive
"""
ifc = ac.parse_ifconfig(IFCONFIG)
assert ifc["awdl0"]["mtu"] == 1484 and ifc["en0"]["mtu"] == 1500 and ifc["en0"]["active"] and not ifc["en5"]["active"]
assert ifc["lo0"]["mtu"] == 16384 and not ifc["lo0"]["active"]

# The failure: active wired interfaces bigger than awdl0.
res = ac.check_interfaces(ifc)
fails = [t for lv, t in res if lv == "FAIL"]
assert len(fails) == 1 and "en0=1500" in fails[0] and "en1=1500" in fails[0] and "en5" not in fails[0], fails
assert any("1484, the family's value" in t for lv, t in res)           # the explanation of why awdl0 is 1484
# Equal MTUs pass; a missing awdl0 fails first.
ifc2 = ac.parse_ifconfig(IFCONFIG.replace("mtu 1484\n\tmedia: autoselect\n\tstatus: active\nllw0", "mtu 1500\n\tmedia: autoselect\n\tstatus: active\nllw0"))
assert ifc2["awdl0"]["mtu"] == 1500 and not [1 for lv, t in ac.check_interfaces(ifc2) if lv == "FAIL"]
assert ac.check_interfaces({"en0": ifc["en0"]})[0][0] == "FAIL"

NDP = """Neighbor                        Linklayer Address  Netif Expire    1s 5s
fe80::b433:bdff:fe88:718c%awdl0 b6:33:bd:88:71:8c  awdl0 permanent R
fe80::5895:b0ff:fee1:aac0%llw0  5a:95:b0:e1:aa:c0   llw0 permanent R
fe80::1%lo0                     (incomplete)          lo0 permanent R
"""
n = ac.parse_ndp(NDP)
assert n == [("fe80::b433:bdff:fe88:718c", "b6:33:bd:88:71:8c")], n

# Log signatures, from the real lines.
LOG = "\n".join([
    '2026-11-18 01:52:27.574 E  sharingd[461:693d] [com.apple.network:connection] [C25 com.apple.sharingd.AirDrop,5CC6 udp] reporting state cancelled error Message too long',
    '2026-11-18 01:52:31.697 Df sharingd[461:60ed] [com.apple.sharing:AirDropNW.Discovery] Ask failed Error Domain=Sharing.SFAirDropSend.Failure Code=6 - waiting for other asks if appropriate',
    '2026-11-18 01:50:49.608 Df sharingd[461:73e4] [com.apple.sharing:Metrics] MetricEvent \'com.apple.sharing.airdrop.send-finish\' : { "failureSubType" : "unexpected-40", "size" : 50988039, "result" : 1, "transport" : "awdl" }',
    '2026-11-18 01:53:17.747 Df sharingd[461:73e4] [com.apple.sharing:Metrics] MetricEvent \'com.apple.sharing.airdrop.send-finish\' : { "failureSubType" : "missingEndpoint", "transferID" : "68DF", "size" : 0, "result" : 1, "transport" : "other" }',
])
counts, finishes = ac.classify_log(LOG)
assert counts["emsgsize"] >= 2 and counts["ask-failed"] == 1 and counts["missingEndpoint"] == 1, counts
assert len(finishes) == 2 and finishes[0][1]["failureSubType"] == "unexpected-40" and finishes[0][1]["size"] == "50988039"
assert finishes[1][1]["failureSubType"] == "missingEndpoint"
lres, _, _ = ac.check_logs(LOG, 30)
assert lres[0] == ("FAIL", "2 AirDrop send(s) in the last 30 min, 0 succeeded")
# A success is read as one.
okc, okf = ac.classify_log('MetricEvent \'com.apple.sharing.airdrop.send-finish\' : { "result" : 0, "size" : 1000, "bytesPerSec" : 34000000, "transport" : "awdl" }')
assert okf and okf[0][1]["result"] == "0" and okf[0][1]["bytesPerSec"] == "34000000" and not okc

# The verdict: MTU first, then no neighbour, then discovery.
reg = '"ITLAWDLCoexAdded" = Yes\n'
v = ac.verdicts(counts, ifc, n, reg)
assert "EMSGSIZE" in v and "en0" in v and "1484" in v, v
assert "never heard" in ac.verdicts({"missingEndpoint": 1}, ifc2, [], reg)
assert "discovery" in ac.verdicts({"missingEndpoint": 1}, ifc2, n, reg)
assert "not there" in ac.verdicts({}, ifc2, n, "")
assert "no known failure" in ac.verdicts({}, ifc2, n, reg)

# Driver checks on registry text.
drv = ('"ITLAWDLCoexAdded" = Yes\n"ITLAWDLCmdTimeouts" = 0\n"ITLAWDLCmdEnospcSlot" = 0\n"ITLAWDLCmdEnospcRing" = 0\n'
       '"ITLAWDLCoexUcastOK" = 900\n"ITLAWDLCoexUcastFailed" = 20\n"ITLAWDLCoexBaTxStateName" = "agreed"\n'
       '"ItlwmAWDLMtu" = 1500\n"ItlwmAWDLMtuResult" = 0\n"ITLAWDLPmFlips" = 2\n')
d = ac.check_driver(drv)
assert all(lv != "FAIL" for lv, t in d), d
assert any("MTU to 1500" in t for lv, t in d)
bad = ac.check_driver('"ITLAWDLCoexAdded" = Yes\n"ITLAWDLCmdTimeouts" = 2\n"ITLAWDLCmdEnospcSlot" = 5\n"ITLAWDLCmdEnospcRing" = 0\n"ITLAWDLCoexUcastOK" = 10\n"ITLAWDLCoexUcastFailed" = 90\n')
assert sum(lv == "FAIL" for lv, t in bad) == 2
assert ac.check_driver("")[0][0] == "FAIL"
print("airdrop_check: passed")
