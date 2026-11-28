// Host checks for the -itlawdlframedump ring (AWDLFrameDump.hpp).
#include <AWDLFrameDump.hpp>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace ItlAWDL;

static std::vector<uint8_t> syncFrame(size_t length, uint8_t subtype, uint8_t tag)
{
    std::vector<uint8_t> f(length, tag);
    f[0] = 0xd0;
    f[24] = 0x7f; f[25] = 0x00; f[26] = 0x17; f[27] = 0xf2; f[28] = 0x08; f[29] = 0x10; f[30] = subtype;
    return f;
}

int main()
{
    unsigned cases = 0;
    // Only PSF and MI frames count as sync frames.
    auto mi = syncFrame(300, 3, 0xa1), psf = syncFrame(80, 0, 0xa2), other = syncFrame(80, 6, 0xa3);
    assert(frameDumpIsSync(mi.data(), mi.size()) && frameDumpIsSync(psf.data(), psf.size()));
    assert(!frameDumpIsSync(other.data(), other.size()) && !frameDumpIsSync(mi.data(), 31));
    auto data = mi;
    data[0] = 0x08;
    assert(!frameDumpIsSync(data.data(), data.size()));
    cases += 5;

    static FrameDump d;
    uint8_t out[kFrameDumpEntries * (kFrameDumpHeader + kFrameDumpBytes)];
    assert(frameDumpSerialize(d, out, sizeof(out)) == 0);
    // The gap: a second frame within 100 ms is skipped and counted.
    assert(frameDumpNote(d, 1000000, mi.data(), mi.size(), 149));
    assert(!frameDumpNote(d, 1050000, psf.data(), psf.size(), 149) && d.skipped == 1);
    assert(frameDumpNote(d, 1100000, psf.data(), psf.size(), 6));
    size_t n = frameDumpSerialize(d, out, sizeof(out));
    assert(n == 2 * kFrameDumpHeader + 300 + 80);
    // Header: uptime ms, length, channel, cut; then the bytes.
    assert(out[0] == (1000 & 0xff) && out[1] == (1000 >> 8) && out[4] == (300 & 0xff) && out[5] == (300 >> 8));
    assert(out[6] == 149 && out[7] == 0 && out[kFrameDumpHeader + 1] == 0xa1);
    assert(out[kFrameDumpHeader + 300 + 6] == 6 && out[kFrameDumpHeader + 300 + kFrameDumpHeader + 1] == 0xa2);
    cases += 7;
    // A long frame is cut to kFrameDumpBytes and marked.
    auto big = syncFrame(1200, 3, 0xb0);
    assert(frameDumpNote(d, 1300000, big.data(), big.size(), 149));
    n = frameDumpSerialize(d, out, sizeof(out));
    assert(out[n - kFrameDumpBytes - kFrameDumpHeader + 7] == 1);
    assert(out[n - kFrameDumpBytes - kFrameDumpHeader + 4] == (kFrameDumpBytes & 0xff));
    cases += 3;
    // The ring keeps the newest kFrameDumpEntries, oldest first.
    for (unsigned i = 0; i < 20; ++i) {
        auto f = syncFrame(64, 0, uint8_t(i));
        assert(frameDumpNote(d, 2000000 + i * 200000ULL, f.data(), f.size(), 149));
    }
    n = frameDumpSerialize(d, out, sizeof(out));
    assert(n == kFrameDumpEntries * (kFrameDumpHeader + 64));
    assert(out[kFrameDumpHeader + 1] == uint8_t(20 - kFrameDumpEntries));
    assert(out[n - 64 + 1] == 19);
    cases += 3;
    // An entry that doesn't fit whole is left out; nothing is written past the capacity.
    uint8_t small[kFrameDumpHeader + 64 + 10];
    assert(frameDumpSerialize(d, small, sizeof(small)) == kFrameDumpHeader + 64);
    // Empty or null frames aren't kept.
    assert(!frameDumpNote(d, 9000000, nullptr, 10, 0) && !frameDumpNote(d, 9000000, mi.data(), 0, 0));
    cases += 2;
    printf("awdl_frame_dump: %u cases passed\n", cases);
    return 0;
}
