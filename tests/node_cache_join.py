"""Run the actual net80211 node-cache eviction bodies with isolated host state.

A scan in a place with more BSSs than the cache holds evicts the lowest
addresses first. Only the cache is simulated: no firmware, RF, or macOS.
From runmain's patch in #69.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()


def block(start):
    opening = source.index("{", source.index(start))
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[opening:end]


clean = block("\nieee80211_clean_nodes(struct ieee80211com *ic, int cache_timeout)")
alloc = block("\nieee80211_alloc_node_helper(struct ieee80211com *ic)")
program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define IEEE80211_STA_ONLY
typedef unsigned u_int;
constexpr unsigned IEEE80211_F_DESBSSID = 1;
constexpr int IEEE80211_M_STA = 1;
#define IEEE80211_ADDR_EQ(a, b) (memcmp(a, b, 6) == 0)
struct ieee80211_node {
    uint8_t ni_macaddr[6] = {}, ni_bssid[6] = {};
    unsigned ni_scangen = 0;
    int ni_refcnt = 0;
    ieee80211_node *next = nullptr;
};
struct ieee80211com {
    ieee80211_node *ic_tree = nullptr; // Ascending by address, as the driver's tree iterates.
    int ic_nnodes = 0, ic_max_nnodes = 4, ic_opmode = IEEE80211_M_STA;
    unsigned ic_scangen = 1, ic_flags = 0;
    uint8_t ic_des_bssid[6] = {};
    struct { unsigned is_node_timeout = 0; } ic_stats;
    ieee80211_node *(*ic_node_alloc)(ieee80211com *) = [](ieee80211com *) { return new ieee80211_node; };
};
#define RB_MIN(name, head) (*(head))
#define RB_NEXT(name, head, node) ((node)->next)
static int splnet() { return 0; }
static void splx(int) {}
static void ieee80211_free_node(ieee80211com *ic, ieee80211_node *ni) {
    for (ieee80211_node **link = &ic->ic_tree; *link; link = &(*link)->next)
        if (*link == ni) { *link = ni->next; break; }
    --ic->ic_nnodes;
    delete ni;
}
void ieee80211_clean_nodes(struct ieee80211com *ic, int cache_timeout)
''' + clean + r'''
struct ieee80211_node *ieee80211_alloc_node_helper(struct ieee80211com *ic)
''' + alloc + r'''
static ieee80211_node *find(ieee80211com &ic, uint8_t last) {
    for (ieee80211_node *ni = ic.ic_tree; ni; ni = ni->next)
        if (ni->ni_bssid[5] == last) return ni;
    return nullptr;
}
// A beacon from a BSS not yet cached, as the receive path adds it.
static bool hear(ieee80211com &ic, uint8_t last) {
    ieee80211_node *ni = ieee80211_alloc_node_helper(&ic);
    if (!ni) return false;
    ni->ni_macaddr[5] = ni->ni_bssid[5] = last;
    ieee80211_node **link = &ic.ic_tree;
    while (*link && (*link)->ni_bssid[5] < last) link = &(*link)->next;
    ni->next = *link;
    *link = ni;
    ++ic.ic_nnodes;
    return true;
}
int main() {
    {   // The BSS a join asked for has the lowest address in a full cache.
        ieee80211com ic;
        ic.ic_flags = IEEE80211_F_DESBSSID;
        ic.ic_des_bssid[5] = 1;
        for (uint8_t last = 1; last <= 4; ++last) assert(hear(ic, last));
        for (uint8_t last = 10; last < 40; ++last) assert(hear(ic, last) && "other BSSs still enter the cache");
        assert(find(ic, 1) && "cache pressure must not evict the BSS a join asked for");
        assert(ic.ic_nnodes == ic.ic_max_nnodes && find(ic, 39));
    }
    {   // No join target: a full cache still makes room, lowest address first.
        ieee80211com ic;
        for (uint8_t last = 1; last <= 4; ++last) assert(hear(ic, last));
        assert(hear(ic, 10) && !find(ic, 1) && find(ic, 2) && ic.ic_nnodes == 4);
    }
    {   // Only the join target is free: nothing is evicted and the newcomer is refused.
        ieee80211com ic;
        ic.ic_flags = IEEE80211_F_DESBSSID;
        ic.ic_des_bssid[5] = 1;
        for (uint8_t last = 1; last <= 4; ++last) assert(hear(ic, last));
        for (uint8_t last = 2; last <= 4; ++last) find(ic, last)->ni_refcnt = 1;
        assert(!hear(ic, 10) && find(ic, 1) && ic.ic_nnodes == 4);
    }
    {   // The periodic timeout sweep is unchanged: it frees every unreferenced node.
        ieee80211com ic;
        ic.ic_flags = IEEE80211_F_DESBSSID;
        ic.ic_des_bssid[5] = 1;
        for (uint8_t last = 1; last <= 4; ++last) assert(hear(ic, last));
        find(ic, 3)->ni_refcnt = 1;
        ieee80211_clean_nodes(&ic, 1);
        assert(ic.ic_nnodes == 1 && find(ic, 3));
    }
    puts("node cache join target: kept under pressure, others admitted, refusal and timeout sweep unchanged (host fixture)");
}
'''
with tempfile.TemporaryDirectory(prefix="node-cache-join-") as directory:
    cpp = Path(directory) / "test.cpp"
    exe = Path(directory) / "test"
    cpp.write_text(program)
    subprocess.run([
        "clang++", "-std=c++14", "-Wall", "-Wextra", "-Wno-unused-variable",
        "-Wno-unused-function", str(cpp), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
