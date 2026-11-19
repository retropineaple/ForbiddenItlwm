#!/usr/bin/env python3
"""End-to-end AirDrop checker: says where an AirDrop attempt breaks, the same way every time.

  python3 -I tools/diag/airdrop_check.py                 read-only snapshot of the last 30 minutes
  python3 -I tools/diag/airdrop_check.py --minutes 10    a shorter log window
  python3 -I tools/diag/airdrop_check.py --window 40     also sample the driver's counters for 40 s: start the AirDrop
                                                         in that window and see what moved
  python3 -I tools/diag/airdrop_check.py --discover      browse _airdrop._tcp for 6 s and list who answers, on which interface
  python3 -I tools/diag/airdrop_check.py --ping          ping each awdl0 neighbour with growing payloads (the MTU ladder)
  python3 -I tools/diag/airdrop_check.py --sizes         send don't-fragment UDP datagrams of growing size and see which the stack refuses
  python3 -I tools/diag/airdrop_check.py --log file      classify a saved `log show` text instead of reading the system's

It separates the failure signatures AirDrop has shown so far, because each needs a different fix:
  missingEndpoint / "Ask failed"     discovery: the iPhone's AirDrop service was never found over AWDL
  "Message too long" / unexpected-40 a packet bigger than awdl0's MTU (usually a wired interface at 1500)
  unexpected-57 / not connected      the connection broke after it started
  no neighbour on awdl0              the iPhone was not heard (AWDL schedule, channel, country)
  ENOSPC / timeouts in the driver    the firmware command ring is wedged
and prints the driver's own counters (read-only ioreg) next to them. See docs/DEBUGGING.md.
"""
import argparse
import re
import subprocess
import sys
import time

REG_KEYS = ["ITLAWDLCoexPeerSyncFrames", "ITLAWDLCoexDataTx", "ITLAWDLCoexDataRxUcast", "ITLAWDLCoexUcastOK", "ITLAWDLCoexUcastFailed",
            "ITLAWDLCoexBaTxFrames", "ITLAWDLCoexBaTxAcked", "ITLAWDLCoexGateHolds", "ITLAWDLCoexDataRxMalformed",
            "ITLAWDLCmdTimeouts", "ITLAWDLCmdEnospcRing", "ITLAWDLCmdEnospcSlot", "ITLAWDLPmFlips", "ITLAWDLPmSent",
            "ItlwmAWDLTxPacketsOffered", "ItlwmAWDLTxDropped", "ItlwmLLWRxDelivered"]


def run(cmd, timeout=30):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.stdout + (p.stderr if not p.stdout else "")
    except Exception as e:  # noqa: BLE001
        return "ERROR: %s" % e


def reg_text():
    return run(["ioreg", "-l", "-w0"], 60)


def reg_num(text, key):
    m = re.search(r'"%s" = (-?\d+|Yes|No)\b' % re.escape(key), text)
    if not m:
        return None
    v = m.group(1)
    return {"Yes": 1, "No": 0}.get(v) if v in ("Yes", "No") else int(v)


def reg_str(text, key):
    m = re.search(r'"%s" = "([^"]*)"' % re.escape(key), text)
    return m.group(1) if m else None


# ---- interfaces -------------------------------------------------------------------------------------------------------

def parse_ifconfig(text):
    """{name: {'mtu': int, 'up': bool, 'active': bool, 'flags': str}} from `ifconfig -a`."""
    out, cur = {}, None
    for line in text.splitlines():
        m = re.match(r"^(\S+): flags=\w+<([^>]*)> mtu (\d+)", line)
        if m:
            cur = m.group(1)
            out[cur] = {"mtu": int(m.group(3)), "up": "UP" in m.group(2).split(","), "active": False, "flags": m.group(2)}
        elif cur and re.search(r"status:\s*active", line):
            out[cur]["active"] = True
    return out


def check_interfaces(ifc):
    res = []
    awdl = ifc.get("awdl0")
    if not awdl:
        res.append(("FAIL", "awdl0 does not exist: the driver's AirDrop interface is not up (kext not loaded, build not allowed, or -itlawdlnativeup missing)"))
        return res
    res.append(("PASS" if awdl["up"] else "FAIL", "awdl0 is %s, MTU %d" % ("up" if awdl["up"] else "DOWN", awdl["mtu"])))
    if awdl["mtu"] == 1484:
        res.append(("INFO", "awdl0 is at 1484, the family's value for any controller that is not AppleBCMWLANCore; Apple's own hardware gets "
                    "1500 (IO80211VirtualInterface::peerToPeerConfigureIfnet, asm). A driver build with itlawdlmtu support sets 1500 itself"))
    wider = [(n, i["mtu"]) for n, i in ifc.items() if n.startswith("en") and i["active"] and i["mtu"] > awdl["mtu"]]
    if wider:
        res.append(("FAIL", "active interface(s) with a bigger MTU than awdl0 (%d): %s. AirDrop's QUIC is sized from the wired path and "
                    "macOS then refuses its packets on awdl0 with EMSGSIZE (\"Message too long\", unexpected-40). Fix: "
                    "sudo networksetup -setMTU <service> %d" % (awdl["mtu"], ", ".join("%s=%d" % w for w in wider), awdl["mtu"])))
    else:
        res.append(("PASS", "no active interface has a bigger MTU than awdl0"))
    llw = ifc.get("llw0")
    res.append(("INFO", "llw0 %s" % ("MTU %d, %s" % (llw["mtu"], "up" if llw["up"] else "down") if llw else "absent (only needed for Continuity Camera)")))
    return res


# ---- neighbours -------------------------------------------------------------------------------------------------------

def parse_ndp(text):
    """[(address, mac)] for awdl0 from `ndp -an`."""
    out = []
    for line in text.splitlines():
        if re.search(r"\bawdl0\b", line):
            m = re.match(r"^(\S+)%awdl0\s+([0-9a-f:]{11,17})\s", line)
            if m:
                out.append((m.group(1), m.group(2)))
    return out


def check_neighbours(neigh, reg):
    res = []
    res.append(("PASS" if neigh else "FAIL", "%d neighbour(s) on awdl0%s" % (
        len(neigh), "" if neigh else ": macOS has no iPhone to talk to (the daemon publishes one from the phone's action frames)")))
    sync = reg_num(reg, "ITLAWDLCoexPeerSyncFrames")
    if sync is not None:
        res.append(("PASS" if sync else "FAIL", "AWDL sync frames heard from a phone: %d" % sync + ("" if sync else " (the iPhone is not on our channel, asleep, or not advertising)")))
    ch, ok, state = reg_num(reg, "ITLAWDLCoexChannel"), reg_num(reg, "ITLAWDLCoexAdded"), reg_str(reg, "ITLAWDLCoexMultiState")
    res.append(("INFO", "listener %s on channel %s, state %s, country learned %s" % (
        "added" if ok else "NOT added", ch, state, reg_num(reg, "ITLAWDLCoexCountryLearned"))))
    return res


# ---- logs -------------------------------------------------------------------------------------------------------------

SIGNATURES = [
    ("missingEndpoint", re.compile(r"failureSubType\"?\s*:\s*\"?missingEndpoint"), "discovery: the iPhone's AirDrop endpoint was never found"),
    ("ask-failed", re.compile(r"Ask failed Error Domain=Sharing\.SFAirDropSend\.Failure Code=(\d+)"), "the ask to the receiver failed"),
    ("emsgsize", re.compile(r"Message too long|unexpected-40|EMSGSIZE"), "packet larger than awdl0's MTU"),
    ("not-connected", re.compile(r"unexpected-57|ENOTCONN|Socket is not connected"), "connection broke after it started"),
    ("timeout", re.compile(r"failureSubType\"?\s*:\s*\"?(?:timeout|timedOut|connectionTimeout)"), "timed out"),
    ("cancelled", re.compile(r"failureSubType\"?\s*:\s*\"?cancelled"), "cancelled (by the user or before a path was chosen)"),
    ("companion", re.compile(r"Failed to create the companion low latency interface|Timed out while waiting for the low latency interface"), "wifip2pd could not get llw0"),
    ("pairverify", re.compile(r"PairVerify timeout|Pairing failed|PIN"), "Rapport pairing involved"),
]


def classify_log(text):
    counts, finishes = {}, []
    for line in text.splitlines():
        for name, rx, _ in SIGNATURES:
            if rx.search(line):
                counts[name] = counts.get(name, 0) + 1
        if "airdrop.send-finish" in line:
            f = {}
            for key in ("result", "failureSubType", "size", "bytesPerSec", "transport", "duration"):
                m = re.search(r'\\?"%s\\?"\s*:\s*\\?"?([^,"\\}\s]+)' % key, line)
                if m:
                    f[key] = m.group(1)
            stamp = line[:23]
            finishes.append((stamp, f))
    return counts, finishes


def check_logs(text, minutes):
    counts, finishes = classify_log(text)
    res = []
    if finishes:
        ok = [f for _, f in finishes if f.get("result") == "0"]
        res.append(("PASS" if ok else "FAIL", "%d AirDrop send(s) in the last %d min, %d succeeded" % (len(finishes), minutes, len(ok))))
        for stamp, f in finishes[-6:]:
            res.append(("INFO", "%s result=%s failure=%s size=%s transport=%s speed=%s" % (
                stamp, f.get("result"), f.get("failureSubType", "-"), f.get("size", "-"), f.get("transport", "-"), f.get("bytesPerSec", "-"))))
    else:
        res.append(("INFO", "no AirDrop send finished in the last %d min" % minutes))
    for name, _, meaning in SIGNATURES:
        if counts.get(name):
            level = "FAIL" if name in ("missingEndpoint", "emsgsize", "not-connected", "timeout", "companion") else "INFO"
            res.append((level, "%s x%d: %s" % (name, counts[name], meaning)))
    return res, counts, finishes


# ---- driver -----------------------------------------------------------------------------------------------------------

def check_driver(reg):
    res = []
    if "ITLAWDLCoexAdded" not in reg:
        res.append(("FAIL", "the driver publishes no AWDL state: ForbiddenItlwm is not loaded, or this build has no AWDL support on this macOS"))
        return res
    t, es, er = reg_num(reg, "ITLAWDLCmdTimeouts"), reg_num(reg, "ITLAWDLCmdEnospcSlot"), reg_num(reg, "ITLAWDLCmdEnospcRing")
    if t is not None:
        res.append(("PASS" if not (t or es or er) else "FAIL", "firmware command ring: timeouts %s, ENOSPC %s/%s" % (t, er, es)))
    tx, ok, bad = reg_num(reg, "ITLAWDLCoexDataTx"), reg_num(reg, "ITLAWDLCoexUcastOK"), reg_num(reg, "ITLAWDLCoexUcastFailed")
    if ok is not None and (ok or bad):
        pct = 100.0 * bad / (ok + bad)
        res.append(("PASS" if pct < 10 else "FAIL", "unicast frames acknowledged by the phone: %d ok, %d failed (%.1f%% failed)" % (ok, bad, pct)))
    ba = reg_str(reg, "ITLAWDLCoexBaTxStateName")
    res.append(("INFO", "send Block Ack %s, data frames sent %s, send gate holds %s" % (ba, tx, reg_num(reg, "ITLAWDLCoexGateHolds"))))
    mtu, mres = reg_num(reg, "ItlwmAWDLMtu"), reg_num(reg, "ItlwmAWDLMtuResult")
    if mtu is not None:
        res.append(("PASS" if mres == 0 else "FAIL", "driver set awdl0's MTU to %d (result %s)" % (mtu, mres)))
    flips = reg_num(reg, "ITLAWDLPmFlips")
    if flips is not None:
        res.append(("PASS" if flips <= 6 else "FAIL", "station power-state flips %d (more than a few pulls the radio off the AWDL channel)" % flips))
    return res


def sample(window):
    before = reg_text()
    time.sleep(window)
    after = reg_text()
    res = []
    for k in REG_KEYS:
        a, b = reg_num(before, k), reg_num(after, k)
        if a is not None and b is not None:
            res.append((k, b - a))
    return res


# ---- active probes ----------------------------------------------------------------------------------------------------

def discover(seconds=6):
    out = run(["dns-sd", "-B", "_airdrop._tcp", "local"], seconds + 4) if False else ""
    try:
        p = subprocess.Popen(["dns-sd", "-B", "_airdrop._tcp", "local"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(seconds)
        p.terminate()
        out, _ = p.communicate(timeout=5)
    except Exception as e:  # noqa: BLE001
        return [("FAIL", "dns-sd could not run: %s" % e)]
    found = [l for l in out.splitlines() if re.search(r"\bAdd\b", l)]
    ifs = sorted({l.split()[3] for l in found if len(l.split()) > 3})
    res = [("PASS" if found else "FAIL", "%d _airdrop._tcp service(s) seen%s" % (len(found), (" on interface index(es) " + ", ".join(ifs)) if ifs else ""))]
    if not found:
        res.append(("INFO", "nothing answered: the iPhone must be awake, unlocked, near, and AirDrop set to Everyone (or Contacts Only with you in its contacts)"))
    return res


def probe_sizes(neigh, sizes=(1200, 1400, 1432, 1440, 1452, 1453, 1460, 1472)):
    """Which UDP datagram sizes the IPv6 stack accepts on awdl0 with don't-fragment set (what QUIC does). Local only:
    nothing needs to answer; EMSGSIZE (40) comes back at once from ip6_output when the packet is over the MTU. A UDP payload
    of N bytes is an IPv6 packet of N + 48."""
    import socket
    res = []
    try:
        scope = socket.if_nametoindex("awdl0")
    except OSError:
        return [("FAIL", "awdl0 does not exist")]
    if not neigh:
        return [("FAIL", "no awdl0 neighbour to send to")]
    addr = neigh[0][0]
    s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IPV6, 62, 1)               # IPV6_DONTFRAG
    s.settimeout(2)
    accepted, refused = [], []
    for n in sizes:
        try:
            s.sendto(b"\0" * n, (addr, 9, 0, scope))
            accepted.append(n)
        except OSError as e:
            if e.errno == 40:
                refused.append(n)
            else:
                res.append(("INFO", "payload %d: %s" % (n, e)))
    s.close()
    if accepted:
        res.append(("INFO", "accepted (DF set): %s -> largest IPv6 packet %d" % (accepted, max(accepted) + 48)))
    if refused:
        res.append(("INFO", "refused with EMSGSIZE: %s -> smallest refused packet %d" % (refused, min(refused) + 48)))
    mtu = None
    if accepted and refused:
        mtu = max(accepted) + 48
        res.append(("INFO", "the stack's limit on awdl0 is between %d and %d bytes" % (mtu, min(refused) + 48)))
    res.append(("PASS" if accepted and not [r for r in refused if r + 48 <= 1500] else "FAIL",
                "datagrams that fit a 1500-byte packet are accepted" if not [r for r in refused if r + 48 <= 1500] else
                "a datagram that fits 1500 bytes was refused: the limit is lower than the interface MTU"))
    return res


def ping_ladder(neigh, sizes=(56, 600, 1200, 1432)):
    res = []
    for addr, mac in neigh[:3]:
        for size in sizes:
            o = run(["ping6", "-c", "2", "-s", str(size), "-i", "0.3", "%s%%awdl0" % addr], 8)
            m = re.search(r"(\d+) packets received", o) or re.search(r"(\d+) received", o)
            got = int(m.group(1)) if m else 0
            res.append(("PASS" if got else "FAIL", "ping6 %s%%awdl0 payload %d: %d/2 replies" % (addr, size, got)))
    return res


# ---- report -----------------------------------------------------------------------------------------------------------

def verdicts(counts, ifaces, neigh, reg):
    """The single most likely cause, from everything above."""
    awdl = ifaces.get("awdl0")
    if "ITLAWDLCoexAdded" not in reg or not awdl:
        return "the AirDrop interface or the driver is not there: fix that first"
    if counts.get("emsgsize"):
        wider = [n for n, i in ifaces.items() if n.startswith("en") and i["active"] and i["mtu"] > awdl["mtu"]]
        return "packets too big for awdl0 (EMSGSIZE)" + (": set %s to MTU %d" % (", ".join(wider), awdl["mtu"]) if wider else "; no wired interface is bigger now, so retry on a fresh connection")
    if not neigh:
        return "the iPhone was never heard (no neighbour on awdl0): look at AWDL sync frames, the channel and the country"
    if counts.get("missingEndpoint") or counts.get("ask-failed"):
        return "discovery: awdl0 has a neighbour but sharingd found no AirDrop endpoint on it; run --discover and compare with the phone's visibility setting"
    if counts.get("not-connected") or counts.get("timeout"):
        return "the connection started and then broke: compare the driver's unicast failure rate and Block Ack state"
    return "no known failure signature in this window"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--minutes", type=int, default=30)
    ap.add_argument("--window", type=int, default=0)
    ap.add_argument("--discover", action="store_true")
    ap.add_argument("--ping", action="store_true")
    ap.add_argument("--sizes", action="store_true", help="send DF datagrams of growing size to the neighbour and see which the stack refuses")
    ap.add_argument("--log")
    a = ap.parse_args()

    print("== system")
    print("INFO  %s" % run(["sw_vers", "-productVersion"]).strip(), run(["sw_vers", "-buildVersion"]).strip())
    reg = reg_text()
    print("INFO  driver build", reg_str(reg, "ITLAWDLBuild") or reg_str(reg, "ItlwmBuild") or "(not published)")
    ifaces = parse_ifconfig(run(["ifconfig", "-a"]))

    blocks = [("interfaces", check_interfaces(ifaces)),
              ("neighbours and discovery state", check_neighbours(parse_ndp(run(["ndp", "-an"])), reg)),
              ("driver", check_driver(reg))]
    logtext = open(a.log).read() if a.log else run(["/usr/bin/log", "show", "--last", "%dm" % a.minutes, "--info", "--style", "compact", "--predicate",
                                                    'process == "sharingd" OR process == "wifip2pd" OR process == "rapportd"'], 240)
    lres, counts, finishes = check_logs(logtext, a.minutes)
    blocks.append(("AirDrop log (sharingd, wifip2pd, rapportd)", lres))
    if a.discover:
        blocks.append(("discovery (dns-sd _airdrop._tcp)", discover()))
    if a.sizes:
        blocks.append(("UDP sizes the IPv6 stack accepts on awdl0 (DF set)", probe_sizes(parse_ndp(run(["ndp", "-an"])))))
    if a.ping:
        blocks.append(("MTU ladder over awdl0", ping_ladder(parse_ndp(run(["ndp", "-an"])))))
    fails = 0
    for title, items in blocks:
        print("== " + title)
        for level, text in items:
            print("%-5s %s" % (level, text))
            fails += level == "FAIL"
    if a.window:
        print("== counters over %d s (run the AirDrop now)" % a.window)
        for k, d in sample(a.window):
            print("INFO  %-34s %+d" % (k, d))
    print("== most likely cause")
    print(verdicts(counts, ifaces, parse_ndp(run(["ndp", "-an"])), reg))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
