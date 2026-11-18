"""Run the actual net80211 decision of whether entering RUN raises the link (WCL builds).

The BSD interface's output thread runs only while the link is up, so this decides
whether frames from user space can be sent right after association. Only the
flags are simulated: no firmware, RF, or macOS. From runmain's patch in #69.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()


def block(start):
    opening = source.index("{", source.index(start))
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[opening:end]


signature = "\nieee80211_wcl_run_raises_link(struct ieee80211com *ic, struct ieee80211_node *ni)"
program = r'''
#include <cassert>
#include <cstdio>
constexpr unsigned IEEE80211_F_RSNON = 1;
struct ieee80211_node { int ni_port_valid = 0; };
struct ieee80211com { unsigned ic_flags = 0; bool ic_wcl_enterprise = false; };
static int ieee80211_wcl_run_raises_link(struct ieee80211com *ic, struct ieee80211_node *ni)
''' + block(signature) + r'''
static bool raised(bool rsn, bool enterprise, bool portValid) {
    ieee80211com ic;
    ieee80211_node ni;
    ic.ic_flags = rsn ? IEEE80211_F_RSNON : 0;
    ic.ic_wcl_enterprise = enterprise;
    ni.ni_port_valid = portValid;
    return ieee80211_wcl_run_raises_link(&ic, &ni) != 0;
}
int main() {
    // 802.1X: the system supplicant's EAP frames must go out before any key exists.
    assert(raised(true, true, false) && "an 802.1X network needs the link up to authenticate");
    // Personal: the driver's own handshake raises the link once the keys are in.
    assert(!raised(true, false, false) && "a personal network waits for its keys");
    // A handshake that finished before the RUN transition ran.
    assert(raised(true, false, true) && raised(true, true, true));
    // Open.
    assert(raised(false, false, false));
    puts("WCL link at RUN: up for 802.1X, deferred for personal until keys, up for open (host fixture)");
}
'''
with tempfile.TemporaryDirectory(prefix="wcl-enterprise-link-") as directory:
    cpp = Path(directory) / "test.cpp"
    exe = Path(directory) / "test"
    cpp.write_text(program)
    subprocess.run(["clang++", "-std=c++14", "-Wall", "-Wextra", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
