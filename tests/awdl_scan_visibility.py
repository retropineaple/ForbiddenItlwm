"""Exercise the production scan exception and AWDL receive-isolation filters.

The fixture starts after AWDL's handoff and stops before net80211 input. It checks
which frames reach that input, not scan parsing, firmware reception or joining.
"""
from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
source = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
rx_start = source.index("iwx_rx_frame(struct")
start = source.index("    // While a scan runs,", rx_start)
end = source.index("\n    if (awdlRx) {", start)
filters = source[start:end]
# The exception must remain below the AWDL handoff's consume-and-return path.
handoff = source.index("if ((awdlFrame || baToUs) && awdlCoexRx)", rx_start)
assert handoff < start
assert "mbuf_freem(m);\n            return;" in source[handoff:start]

program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

enum { IEEE80211_S_RUN = 1, IEEE80211_S_SCAN = 2, IEEE80211_F_BGSCAN = 4 };
struct ieee80211_channel { unsigned number; };
struct Phy { ieee80211_channel *channel; };
struct iwx_node { Phy *in_phyctxt; };
struct ieee80211com {
    iwx_node *ic_bss;
    int ic_state;
    unsigned ic_flags;
#ifdef AIRPORT_WCL
    bool ic_wcl_scan_active;
#endif
};
struct Packet { uint8_t *data; size_t length; bool freed = false; };
#define mtod(m, type) reinterpret_cast<type>((m)->data)
static size_t mbuf_len(Packet *m) { return m->length; }
static void mbuf_freem(Packet *m) { assert(!m->freed); m->freed = true; }
static unsigned ieee80211_chan2ieee(ieee80211com *, ieee80211_channel *c) { return c->number; }

static bool awdlSchedCh6, awdlSchedCh6Added, awdlCoexProbe;
static bool awdlCoexPromiscLive, awdlCoexPromiscUncertain, awdlRxMacMaskValid;
static unsigned awdlRxMacMask, awdlCoexChannel;
static unsigned awdlScanFramesPassed, awdlSchedCh6Dropped;
static unsigned awdlCoexPromiscDropped, awdlCoexPromiscAmbiguous;
static constexpr unsigned kAwdlSchedCh6Channel = 6, kAwdlCoexMacId = 1;

static void receiveFilters(ieee80211com *ic, Packet *m, int chanidx, bool chanValid) {
''' + filters + r'''
}

int main() {
    ieee80211_channel stationChannel{36};
    Phy phy{&stationChannel};
    iwx_node node{&phy};
    ieee80211com ic{};
    ic.ic_bss = &node;
    unsigned cases = 0;
    auto reset = [&]() {
        ic.ic_state = IEEE80211_S_RUN;
        ic.ic_flags = 0;
#ifdef AIRPORT_WCL
        ic.ic_wcl_scan_active = false;
#endif
        stationChannel.number = 36;
        awdlSchedCh6 = awdlSchedCh6Added = true;
        awdlCoexProbe = awdlCoexPromiscLive = true;
        awdlCoexPromiscUncertain = false;
        awdlRxMacMaskValid = false;
        awdlRxMacMask = 0;
        awdlCoexChannel = 149;
        awdlScanFramesPassed = awdlSchedCh6Dropped = 0;
        awdlCoexPromiscDropped = awdlCoexPromiscAmbiguous = 0;
    };
    auto receive = [&](size_t length, uint8_t frameControl, unsigned channel, bool valid = true) {
        // Exact-size allocation lets ASan catch any access past a short frame.
        auto *bytes = static_cast<uint8_t *>(calloc(length ? length : 1, 1));
        assert(bytes);
        if (length)
            bytes[0] = frameControl;
        Packet packet{bytes, length};
        receiveFilters(&ic, &packet, int(channel), valid);
        free(bytes);
        ++cases;
        return !packet.freed;
    };

    const unsigned scanModes =
#ifdef AIRPORT_WCL
        4;
#else
        3;
#endif
    for (unsigned mode = 0; mode < scanModes; ++mode) {
        for (unsigned channel : {6U, 44U, 149U}) {
            for (unsigned subtype : {0x80U, 0x50U, 0x40U, 0xd0U, 0x08U, 0x88U}) {
                for (size_t length = 0; length <= 25; ++length) {
                    reset();
                    awdlCoexChannel = channel == 6 ? 149 : channel;
                    if (mode == 1)
                        ic.ic_state = IEEE80211_S_SCAN;
                    if (mode == 2)
                        ic.ic_flags = IEEE80211_F_BGSCAN;
#ifdef AIRPORT_WCL
                    if (mode == 3)
                        ic.ic_wcl_scan_active = true;
#endif
                    const bool expected = mode != 0 && length >= 24 &&
                        (subtype == 0x80 || subtype == 0x50);
                    assert(receive(length, uint8_t(subtype), channel) == expected);
                    assert(awdlScanFramesPassed == (expected ? 1U : 0U));
                    assert(awdlSchedCh6Dropped + awdlCoexPromiscDropped == (expected ? 0U : 1U));
                }
            }
        }
    }
    // A listener-only MAC mask also isolates frames when channel metadata is absent.
    // A filter whose applied state is uncertain must still allow scan results.
    for (bool scanning : {false, true}) {
        for (bool uncertain : {false, true}) {
            reset();
            ic.ic_state = scanning ? IEEE80211_S_SCAN : IEEE80211_S_RUN;
            awdlCoexPromiscLive = !uncertain;
            awdlCoexPromiscUncertain = uncertain;
            awdlRxMacMaskValid = true;
            awdlRxMacMask = 1U << kAwdlCoexMacId;
            assert(receive(24, 0x80, 0, false) == scanning);
        }
    }
    // Traffic for the station's own channel remains eligible without any scan.
    for (unsigned channel : {6U, 44U, 149U}) {
        reset();
        stationChannel.number = channel;
        awdlCoexChannel = channel;
        assert(receive(24, 0x08, channel));
        assert(!awdlScanFramesPassed && !awdlSchedCh6Dropped && !awdlCoexPromiscDropped);
    }
    // Without the AWDL filters, normal net80211 input is unchanged.
    reset();
    awdlSchedCh6 = awdlCoexProbe = false;
    assert(receive(24, 0x08, 149));
    assert(!awdlScanFramesPassed);
    puts("Scan visibility: production receive filters passed (ASan/UBSan; host fixture)");
    printf("%u frame/state/bounds cases\n", cases);
}
'''
with tempfile.TemporaryDirectory(prefix="awdl-scan-visibility-") as directory:
    src = Path(directory) / "test.cpp"
    src.write_text(program)
    for target in ("wcl", "non-wcl"):
        exe = Path(directory) / target
        command = ["xcrun", "clang++", "-std=c++14", "-fsanitize=address,undefined"]
        if target == "wcl":
            command.append("-DAIRPORT_WCL")
        subprocess.run(command + [str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
