#include <AWDLFrameBuilder.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>

int main()
{
    using namespace ItlAWDL;
    uint8_t sync[27] = {}; sync[0] = 6; sync[3] = 6; sync[5] = 16;
    const uint8_t tlvs[] = {0xfe, 2, 0, 0xaa, 0xbb};
    LocalFrame p = {{2, 0, 0, 0, 0, 1}, {0,0x25,0,0xff,0x94,0x73}, 3, 0xabc,
                    0x12345678, 0x10203040, sync, sizeof(sync), tlvs, sizeof(tlvs)};
    uint8_t bytes[128]; memset(bytes, 0xa5, sizeof(bytes));
    size_t written = 999;
    assert(buildLocalFrame(p, bytes+1, sizeof(bytes)-2, &written) == BuildResult::Ready);
    const uint8_t expectedPrefix[] = {0xd0,0,0,0,0,0x25,0,0xff,0x94,0x73,
                                     2,0,0,0,0,1,0,0x25,0,0xff,0x94,0x73,0xc0,0xab};
    assert(written == 75 && memcmp(bytes+1, expectedPrefix, 24) == 0);
    assert(bytes[0] == 0xa5 && bytes[written+1] == 0xa5);
    const uint8_t expectedAction[] = {0x7f,0,0x17,0xf2,8,0x10,3,0,
                                    0x78,0x56,0x34,0x12,0x40,0x30,0x20,0x10};
    assert(memcmp(bytes+25, expectedAction, sizeof(expectedAction)) == 0);
    ActionFrame decoded;
    assert(parseAction(bytes+1, written, &decoded) == ParseResult::Valid);
    assert(decoded.hasSync && decoded.awPeriod == 16 && decoded.masterChannel == 6);
    assert(memcmp(bytes+1+written-sizeof(tlvs), tlvs, sizeof(tlvs)) == 0);
    // Every undersized output must preserve both buffer and length result.
    for (size_t capacity = 0; capacity < written; ++capacity) {
        uint8_t target[128]; memset(target, 0xa5, sizeof(target)); size_t n = 999;
        assert(buildLocalFrame(p, target, capacity, &n) == BuildResult::NoSpace);
        assert(n == 999); for (auto b : target) assert(b == 0xa5);
    }
    const uint8_t duplicate[] = {4,0,0}; p.staticTLVs = duplicate; p.staticLength = 3;
    assert(buildLocalFrame(p, bytes, sizeof(bytes), &written) == BuildResult::Invalid);
    const uint8_t truncated[] = {1,255,255}; p.staticTLVs = truncated;
    assert(buildLocalFrame(p, bytes, sizeof(bytes), &written) == BuildResult::Invalid);
    p.staticTLVs = nullptr; p.staticLength = 0; p.sequence = 4096;
    assert(buildLocalFrame(p, bytes, sizeof(bytes), &written) == BuildResult::Invalid);
    p.sequence = 0; p.source[0] = 1;
    assert(buildLocalFrame(p, bytes, sizeof(bytes), &written) == BuildResult::Invalid);
    const uint8_t source[] = {2,1,2,3,4,5};
    memset(bytes, 0xa5, sizeof(bytes)); written = 999;
    assert(buildTransportProbe(source, 0x12345678, bytes+1, 40, &written) == BuildResult::Ready);
    assert(written == 40 && bytes[0] == 0xa5 && bytes[41] == 0xa5);
    const uint8_t expectedProbe[] = {
        0xd0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,2,1,2,3,4,5,
        0,0x25,0,0xff,0x94,0x73,0,0,0x7f,0,0x17,0xf2,8,0x10,3,0,
        0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};
    assert(memcmp(bytes+1, expectedProbe, sizeof(expectedProbe)) == 0);
    assert(parseAction(bytes+1, written, &decoded) == ParseResult::Valid && !decoded.hasSync);
    for (size_t n = 0; n < 40; ++n) {
        memset(bytes, 0xa5, sizeof(bytes)); written = 999;
        assert(buildTransportProbe(source, 0, bytes, n, &written) == BuildResult::NoSpace);
        assert(written == 999); for (auto b : bytes) assert(b == 0xa5);
    }
    const uint8_t zero[6] = {}, multicast[6] = {1,0,0,0,0,1};
    assert(buildTransportProbe(zero, 0, bytes, sizeof(bytes), &written) == BuildResult::Invalid);
    assert(buildTransportProbe(multicast, 0, bytes, sizeof(bytes), &written) == BuildResult::Invalid);
    assert(buildTransportProbe(nullptr, 0, bytes, sizeof(bytes), &written) == BuildResult::Invalid);
    assert(buildTransportProbe(source, 0, nullptr, sizeof(bytes), &written) == BuildResult::Invalid);
    assert(buildTransportProbe(source, 0, bytes, sizeof(bytes), nullptr) == BuildResult::Invalid);
    puts("AWDL local frame construction and transport probe: passed (offline only)");
}
