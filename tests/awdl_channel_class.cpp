// The operating-class rewrite of TLV 18: only the classes of served slots change, to the 20 MHz class of their channel,
// and nothing else in the frame moves. The layout is the one in AWDL_TLV18_OPCLASS_PARSER.md: TLV header (type, length
// u16 LE), then n-1, encoding, dup, step, fill u16, then n pairs of {channel, class}.
#include <AWDLChannelClass.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace ItlAWDL;

static size_t build(uint8_t *f, unsigned encoding, const uint8_t (*pairs)[2], unsigned n, bool precedingTlv)
{
    memset(f, 0, 200);
    size_t i = 40;
    if (precedingTlv) {                       // a TLV before 18 must be skipped over
        f[i] = 4; f[i + 1] = 3; f[i + 2] = 0; f[i + 3] = 9; f[i + 4] = 9; f[i + 5] = 9;
        i += 6;
    }
    f[i] = 18;
    const size_t len = 6 + 2 * n;
    f[i + 1] = uint8_t(len); f[i + 2] = uint8_t(len >> 8);
    f[i + 3] = uint8_t(n - 1); f[i + 4] = uint8_t(encoding); f[i + 5] = 3; f[i + 6] = 3; f[i + 7] = 0xff; f[i + 8] = 0xff;
    for (unsigned j = 0; j < n; ++j) { f[i + 9 + 2 * j] = pairs[j][0]; f[i + 10 + 2 * j] = pairs[j][1]; }
    return i + 3 + len;
}

int main()
{
    // Channel to class.
    assert(class20ForChannel(6) == 81 && class20ForChannel(14) == 82 && class20ForChannel(36) == 115 &&
           class20ForChannel(48) == 115 && class20ForChannel(52) == 118 && class20ForChannel(100) == 121 &&
           class20ForChannel(149) == 125 && class20ForChannel(161) == 125 && class20ForChannel(165) == 125 &&
           class20ForChannel(0) == 0 && class20ForChannel(200) == 0 && class20ForChannel(20) == 0);
    // Class widths, and that every class the rewrite produces is 20 MHz.
    assert(classWidthMHz(124) == 20 && classWidthMHz(126) == 40 && classWidthMHz(128) == 80 && classWidthMHz(129) == 160 &&
           classWidthMHz(0) == 0 && classWidthMHz(99) == 0);
    for (unsigned ch = 0; ch < 256; ++ch)
        if (class20ForChannel(uint8_t(ch)))
            assert(classWidthMHz(class20ForChannel(uint8_t(ch))) == 20);

    // The Mac's real sequence as seen on the air: 149 at 80 MHz (class 128) in some slots, channel 6 at 20 MHz, empty slots.
    const uint8_t seq[16][2] = {{149, 128}, {149, 128}, {149, 128}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                                {6, 81}, {149, 128}, {149, 128}, {44, 128}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    uint8_t f[200], orig[200];
    for (int preceding = 0; preceding < 2; ++preceding) {
        const size_t n = build(f, 3, seq, 16, preceding);
        memcpy(orig, f, sizeof(f));
        const unsigned changed = forceClass20(f, n);
        assert(changed == 6);                                   // 149 x5 and 44, not channel 6 (already 20), not {0,0}
        const size_t base = 40 + (preceding ? 6 : 0) + 9;
        for (unsigned j = 0; j < 16; ++j) {
            assert(f[base + 2 * j] == seq[j][0]);                // channels never change
            const uint8_t want = seq[j][0] == 149 ? 125 : seq[j][0] == 44 ? 115 : seq[j][1];
            assert(f[base + 2 * j + 1] == want);
        }
        for (size_t k = 0; k < sizeof(f); ++k)                  // nothing outside the class bytes moved
            if (f[k] != orig[k]) {
                assert(k >= base && k < base + 32 && ((k - base) & 1));
            }
        assert(forceClass20(f, n) == 0);                        // idempotent
    }

    // Other encodings carry no class; a truncated or empty frame is left alone.
    size_t n = build(f, 0, seq, 16, false);
    memcpy(orig, f, sizeof(f));
    assert(forceClass20(f, n) == 0 && !memcmp(f, orig, sizeof(f)));
    n = build(f, 3, seq, 16, false);
    assert(forceClass20(f, n - 5) == 0);                        // TLV runs past the frame
    assert(forceClass20(nullptr, 100) == 0 && forceClass20(f, 20) == 0);
    memset(f, 0, sizeof(f));
    assert(forceClass20(f, 120) == 0);                          // no TLV 18 at all

    // A sequence that is already 20 MHz is left as sent.
    const uint8_t narrow[3][2] = {{149, 125}, {149, 124}, {6, 81}};
    n = build(f, 3, narrow, 3, false);
    assert(forceClass20(f, n) == 0);

    puts("AWDL TLV 18 class rewrite: passed");
    return 0;
}
