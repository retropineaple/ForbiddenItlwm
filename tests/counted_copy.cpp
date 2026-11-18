// Bounded copies used by the station interface (getSSID, getRATE_SET, scan results) and the AWDL
// transmit path (include/ItlCountedCopy.hpp). Audit P-S01, ST01, ST06, P-A03, D02.
#include <ItlCountedCopy.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace ItlCopy;
int main() {
    // P-S01: a 32-byte SSID with no terminator, followed by other data (a kernel pointer, in the driver).
    struct { uint8_t essid[32]; uint8_t after[16]; } field;
    memset(field.essid, 'A', sizeof(field.essid));
    memset(field.after, 0xEE, sizeof(field.after));
    uint8_t reply[32];
    memset(reply, 0, sizeof(reply));
    assert(copyCounted(reply, sizeof(reply), field.essid, sizeof(field.essid), 32) == 32);
    for (uint8_t b : reply) assert(b == 'A');
    // A stored count larger than the field, or negative, never reaches past it.
    assert(countedLength(48, 32, 32) == 32);
    assert(countedLength(-1, 32, 32) == 0);
    assert(countedLength(0, 32, 32) == 0);
    assert(countedLength(5, 32, 32) == 5);
    // ST06: 15 rates into a 15-entry reply, 20 claimed: 15.
    uint8_t rates[15];
    for (unsigned i = 0; i < 15; ++i) rates[i] = uint8_t(2 * (i + 1));
    uint32_t out[15] = {};
    assert(copyCounted(out, 15, rates, 15, 20) == 15 && out[0] == 2 && out[14] == 30);
    // A smaller destination wins.
    uint32_t small[4] = {};
    assert(copyCounted(small, 4, rates, 15, 8) == 4 && small[3] == 8);
    uint8_t *none = nullptr;
    assert(copyCounted(none, 4, rates, 15, 8) == 0);
    // P-A03: offset and length inside a 2048-byte buffer, with no overflow.
    assert(frameInBuffer(0x40, 1514, 2048));
    assert(frameInBuffer(0, 2048, 2048));
    assert(frameInBuffer(2048, 0, 2048));
    assert(!frameInBuffer(2049, 0, 2048));
    assert(!frameInBuffer(0x40, 2048, 2048));
    assert(!frameInBuffer(1, 0xffffffffu, 2048));
    assert(!frameInBuffer(0xffffffffu, 2, 2048));
    puts("counted copy bounds: passed");
}
