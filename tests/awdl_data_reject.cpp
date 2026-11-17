// classifyDataFrame must say None exactly when parseDataFrame accepts a frame, and name the right reason otherwise.
#include <AWDLDataFrame.hpp>
#include <AWDLPeerProbe.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace ItlAWDL;

static DataReject classify(const uint8_t *f, size_t n, bool fcs) { return classifyDataFrame(f, n, fcs); }

int main()
{
    // A valid frame, built the way tests/awdl_data_frame.cpp builds one: 24-byte header, FCS, a 40-byte IPv6 packet.
    uint8_t frame[84] = {8}, ip[40] = {0x60};
    const uint8_t bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    memset(frame + 4, 255, 6);
    frame[10] = 2;
    frame[15] = 1;
    memcpy(frame + 16, bssid, 6);
    write16(frame + 22, 0xabc0);
    size_t length;
    assert(buildIPv6DataPayload(ip, 40, 0x1234, frame + 24, 56, &length) == BuildResult::Ready);
    DataFrameView view;
    assert(parseDataFrame(frame, 84, true, &view) == ParseResult::Valid);
    assert(classify(frame, 84, true) == DataReject::None);

    // Each way a frame can be turned down, one at a time, and what the classifier calls it.
    uint8_t f[84];
    auto fresh = [&] { memcpy(f, frame, sizeof(f)); };
    fresh(); assert(classify(f, 27, true) == DataReject::Short);
    fresh(); assert(classify(f, 23, false) == DataReject::Short);
    fresh(); f[0] = 0xc8; assert(classify(f, 84, true) == DataReject::QosNull);
    fresh(); f[0] = 0x48; assert(classify(f, 84, true) == DataReject::NullData);
    fresh(); f[0] = 0x80; assert(classify(f, 84, true) == DataReject::OtherSubtype);   // a beacon is not data
    fresh(); f[16] = 1; assert(classify(f, 84, true) == DataReject::NotAwdlBssid);
    fresh(); f[1] = 0x40; assert(classify(f, 84, true) == DataReject::Protected);
    fresh(); f[1] = 0x80; assert(classify(f, 84, true) == DataReject::HtControl);
    fresh(); f[1] = 0x04; assert(classify(f, 84, true) == DataReject::Fragment);
    fresh(); f[22] |= 1; assert(classify(f, 84, true) == DataReject::Fragment);
    fresh(); f[1] = 0x01; assert(classify(f, 84, true) == DataReject::DsBits);
    fresh(); f[1] = 0x02; assert(classify(f, 84, true) == DataReject::DsBits);
    fresh(); f[10] = 1; assert(classify(f, 84, true) == DataReject::Addresses);          // multicast source
    fresh(); memset(f + 4, 0, 6); assert(classify(f, 84, true) == DataReject::Addresses);  // no destination
    // QoS data with the A-MSDU bit.
    uint8_t qos[86] = {};
    memcpy(qos, frame, 24);
    qos[0] = 0x88;
    memcpy(qos + 26, frame + 24, 60);
    assert(classify(qos, 86, true) == DataReject::None);
    qos[24] = 0x80;
    assert(classify(qos, 86, true) == DataReject::Amsdu);
    qos[24] = 0;
    // Payload problems.
    fresh(); f[24] = 0; assert(classify(f, 84, true) == DataReject::NotLlcSnap);
    fresh(); f[38] = 0x08; assert(classify(f, 84, true) == DataReject::NotIPv6);        // ethertype
    fresh(); f[40] = 0x70; assert(classify(f, 84, true) == DataReject::BadIPv6);        // IP version
    fresh(); f[24 + 16 + 4] = 0x7f; assert(classify(f, 84, true) == DataReject::BadIPv6);  // length field
    // Outer data header with a wrong length.
    static uint8_t w[1600];
    const uint8_t head[26] = {0x88, 0, 0x2c, 0, 2, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 0, 0x25, 0, 0xff, 0x94, 0x73, 0x80, 2, 0, 0};
    memcpy(w, head, 26);
    const uint8_t outer[8] = {0xaa, 0xaa, 3, 0, 0, 0, 0x05, 0xe9};
    memcpy(w + 26, outer, 8);
    uint8_t inner[1497] = {0x60};
    inner[4] = (1497 - 40) >> 8;
    inner[5] = (1497 - 40) & 255;
    size_t n2 = 0;
    assert(buildIPv6DataPayload(inner, 1497, 0x28, w + 34, 1500 + 16, &n2) == BuildResult::Ready);
    assert(parseDataFrame(w, 26 + 8 + n2 + 4, true, &view) == ParseResult::Valid);
    assert(classify(w, 26 + 8 + n2 + 4, true) == DataReject::None);
    w[26 + 6] = 0x01;   // an outer length that doesn't match the frame: several packets in one
    assert(parseDataFrame(w, 26 + 8 + n2 + 4, true, &view) == ParseResult::Unsupported);
    assert(classify(w, 26 + 8 + n2 + 4, true) == DataReject::OuterLength);
    w[26 + 6] = 0x05;

    // Plain RFC 1042 IPv6 (no AWDL header), as an iPhone sends the camera stream: accepted, sequence 0.
    {
        static uint8_t r[26 + 8 + 100];
        memcpy(r, head, 26);
        const uint8_t snap[8] = {0xaa, 0xaa, 3, 0, 0, 0, 0x86, 0xdd};
        memcpy(r + 26, snap, 8);
        r[34] = 0x60; r[38] = 0; r[39] = 60;
        assert(parseDataFrame(r, 26 + 8 + 100, false, &view) == ParseResult::Valid);
        assert(view.ip.length == 100 && view.ip.sequence == 0);
        assert(classify(r, 26 + 8 + 100, false) == DataReject::None);
        r[38] = 1;   // IPv6 length no longer matches
        assert(parseDataFrame(r, 26 + 8 + 100, false, &view) == ParseResult::Malformed);
        assert(classify(r, 26 + 8 + 100, false) == DataReject::BadIPv6);
    }

    // What llw0 sends is the same shape: built plain, it parses back as RFC 1042 with the packet intact.
    {
        uint8_t eth[14 + 40] = {2, 1, 1, 1, 1, 1, 2, 5, 5, 5, 5, 5, 0x86, 0xdd, 0x60};
        const uint8_t own[6] = {2, 5, 5, 5, 5, 5};
        uint8_t out[200];
        size_t n = 0;
        assert(buildDataFrameFromEthernet(eth, sizeof(eth), own, 7, out, sizeof(out), &n, true) == BuildResult::Ready);
        assert(n == 24 + 8 + 40 && out[24] == 0xaa && out[28] == 0 && out[30] == 0x86 && out[31] == 0xdd);
        assert(parseDataFrame(out, n, false, &view) == ParseResult::Valid && view.ip.rfc1042 && view.ip.length == 40);
        assert(!memcmp(view.ip.bytes, eth + 14, 40) && !memcmp(view.destination, eth, 6));
        assert(buildDataFrameFromEthernet(eth, sizeof(eth), own, 7, out, sizeof(out), &n, false) == BuildResult::Ready);
        assert(parseDataFrame(out, n, false, &view) == ParseResult::Valid && !view.ip.rfc1042);
    }

    // The two agree on everything: mutate a valid frame at random and compare, with and without FCS.
    uint32_t random = 1;
    unsigned accepted = 0, rejected[unsigned(DataReject::Count)] = {};
    for (unsigned i = 0; i < 200000; ++i) {
        uint8_t bytes[96] = {};
        if (i & 2)
            memcpy(bytes, qos, sizeof(qos));
        else
            memcpy(bytes, frame, sizeof(frame));
        for (unsigned k = 0, flips = 1 + (i % 4); k < flips; ++k) {
            random = random * 1664525 + 1013904223;
            const unsigned at = (random >> 8) % 86;
            random = random * 1664525 + 1013904223;
            bytes[at] = uint8_t(random >> 24);
        }
        random = random * 1664525 + 1013904223;
        const size_t n = 20 + (random >> 8) % 70;
        const bool fcs = i & 1;
        DataFrameView decoded;
        const bool parses = parseDataFrame(bytes, n, fcs, &decoded) == ParseResult::Valid;
        const DataReject reason = classifyDataFrame(bytes, n, fcs);
        assert(parses == (reason == DataReject::None));
        parses ? ++accepted : ++rejected[unsigned(reason)];
    }
    // The sweep must have reached several different reasons, or it proves little.
    unsigned reasons = 0;
    for (unsigned r = 1; r < unsigned(DataReject::Count); ++r)
        reasons += rejected[r] != 0;
    assert(accepted > 0 && reasons >= 8);

    puts("data frame reject classifier: passed");
    return 0;
}
