// llw0's QoS TID 7 framing (-itlawdlllwqos): QoS data, AWDL's BSSID, plain LLC/SNAP, then IPv6.
#include <AWDLDataFrame.hpp>
#include <AWDLPeerProbe.hpp>
#include <cassert>
#include <cstring>
int main() {
    using namespace ItlAWDL;
    const uint8_t own[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t peer[6] = {0x06, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
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
    assert(buildDataFrameFromEthernet(eth, sizeof(eth), own, 0x123, frame, sizeof(frame), &written, true, true) ==
           BuildResult::Ready);
    assert(written == 26 + 8 + 74);
    assert(frame[0] == 0x88 && frame[1] == 0);
    assert(!memcmp(frame + 4, peer, 6) && !memcmp(frame + 10, own, 6) && !memcmp(frame + 16, bssid, 6));
    assert(read16(frame + 22) >> 4 == 0x123);
    assert(frame[24] == 7 && frame[25] == 0);
    const uint8_t snap[8] = {0xaa, 0xaa, 0x03, 0, 0, 0, 0x86, 0xdd};
    assert(!memcmp(frame + 26, snap, 8) && !memcmp(frame + 34, eth + 14, 74));
    // No space for the longer header.
    assert(buildDataFrameFromEthernet(eth, sizeof(eth), own, 1, frame, 26 + 8 + 73, &written, true, true) == BuildResult::NoSpace);
    // The default plain form is unchanged: non-QoS, 24-byte header.
    assert(buildDataFrameFromEthernet(eth, sizeof(eth), own, 1, frame, sizeof(frame), &written, true) == BuildResult::Ready);
    assert(written == 24 + 8 + 74 && frame[0] == 0x08 && !memcmp(frame + 24, snap, 8));
    return 0;
}
