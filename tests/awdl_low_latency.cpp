// llw0's framing: QoS data on TID 7, AWDL's BSSID, plain LLC/SNAP, then IPv6 (the iPad's llw0 sends this).
#include <AWDLDataFrame.hpp>
#include <AWDLPeerProbe.hpp>
#include <cassert>
#include <cstring>
int main() {
    using namespace ItlAWDL;
    const uint8_t own[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t peer[6] = {0x06, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    // An Ethernet frame from llw0: IPv6 UDP with 26 bytes of payload, between two fd00::/8 addresses.
    uint8_t eth[14 + 40 + 34] = {};
    memcpy(eth, peer, 6);
    memcpy(eth + 6, own, 6);
    eth[12] = 0x86; eth[13] = 0xdd;
    uint8_t *ip = eth + 14;
    ip[0] = 0x60; ip[4] = 0; ip[5] = 34; ip[6] = 17; ip[7] = 64;
    ip[8] = 0xfd; ip[24] = 0xfd;
    for (unsigned i = 40; i < 74; ++i) ip[i] = uint8_t(i);
    uint8_t frame[256];
    size_t written = 0;
    assert(buildLowLatencyFrameFromEthernet(eth, sizeof(eth), own, 0x123, frame, sizeof(frame), &written) ==
           BuildResult::Ready);
    assert(written == 26 + 8 + 74);
    assert(frame[0] == 0x88 && frame[1] == 0);
    assert(!memcmp(frame + 4, peer, 6) && !memcmp(frame + 10, own, 6) && !memcmp(frame + 16, bssid, 6));
    assert(read16(frame + 22) >> 4 == 0x123);
    assert(frame[24] == kLowLatencyTid && frame[25] == 0);
    const uint8_t snap[8] = {0xaa, 0xaa, 0x03, 0, 0, 0, 0x86, 0xdd};
    assert(!memcmp(frame + 26, snap, 8) && !memcmp(frame + 34, ip, 74));
    // Parsed back as llw0's, with the IPv6 packet in place.
    DataFrameView view = {};
    assert(parseDataFrame(frame, written, false, &view) == ParseResult::Valid);
    assert(view.lowLatency && view.ip.bytes == frame + 34 && view.ip.length == 74);
    assert(view.source == frame + 10 && view.destination == frame + 4 && view.wifiSequence == 0x123);
    // With the FCS the receive path keeps.
    uint8_t withFcs[256];
    memcpy(withFcs, frame, written);
    memset(withFcs + written, 0, 4);
    assert(parseDataFrame(withFcs, written + 4, true, &view) == ParseResult::Valid && view.lowLatency);
    // A truncated packet is refused rather than passed on.
    assert(parseDataFrame(frame, written - 1, false, &view) == ParseResult::Malformed);
    // Group frames ask for no ACK.
    uint8_t group[sizeof(eth)];
    memcpy(group, eth, sizeof(eth));
    const uint8_t allNodes[6] = {0x33, 0x33, 0, 0, 0, 1};
    memcpy(group, allNodes, 6);
    assert(buildLowLatencyFrameFromEthernet(group, sizeof(group), own, 1, frame, sizeof(frame), &written) ==
           BuildResult::Ready);
    assert(frame[24] == (kLowLatencyTid | 0x20));
    // Only IPv6 from our own address, and only with room for all of it.
    assert(buildLowLatencyFrameFromEthernet(eth, sizeof(eth), peer, 1, frame, sizeof(frame), &written) ==
           BuildResult::Invalid);
    assert(buildLowLatencyFrameFromEthernet(eth, sizeof(eth), own, 1, frame, 26 + 8 + 73, &written) ==
           BuildResult::NoSpace);
    uint8_t ipv4[sizeof(eth)];
    memcpy(ipv4, eth, sizeof(eth));
    ipv4[12] = 0x08; ipv4[13] = 0x00;
    assert(buildLowLatencyFrameFromEthernet(ipv4, sizeof(ipv4), own, 1, frame, sizeof(frame), &written) ==
           BuildResult::Invalid);
    // AWDL's own data frames are still AWDL's.
    assert(buildDataFrameFromEthernet(eth, sizeof(eth), own, 7, frame, sizeof(frame), &written) == BuildResult::Ready);
    assert(parseDataFrame(frame, written, false, &view) == ParseResult::Valid && !view.lowLatency);
    return 0;
}
