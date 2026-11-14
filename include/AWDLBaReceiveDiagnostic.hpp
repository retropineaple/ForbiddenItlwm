#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace ItlAWDL {
// Bounded read-only observation. Never decides whether a packet is accepted.
struct BaRxDiagnostic {
    static constexpr unsigned stages = 8, kinds = 4, capacity = 64;
    struct Snapshot {
        uint32_t stage, length, status, flags2, channel, macMask, gp2, serial;
        uint8_t frame[48];
    };
    uint32_t calls[stages]{}, owned[stages][kinds]{}, foreign[stages][kinds]{};
    uint32_t invalidRecords = 0, sampleCount = 0;
    Snapshot samples[capacity]{};
    void note(unsigned stage, const uint8_t *f, size_t length, bool padded,
              const uint8_t *own, bool ownKnown, uint32_t status, uint32_t flags2,
              uint32_t channel, uint32_t mask, uint32_t gp2) {
        if (stage >= stages) return;
        ++calls[stage];
        if (!f || length < 24 || f[0] != 0xd0) return;
        const size_t body = padded ? 26 : 24;
        if (length < body + 2 || f[body] != 3) return;
        const bool ours = ownKnown && own && !memcmp(f + 4, own, 6);
        static const uint8_t bssid[6] = {0,0x25,0,0xff,0x94,0x73};
        if (!ours && memcmp(f + 16, bssid, 6)) return;
        const unsigned action = f[body + 1] < 3 ? f[body + 1] : 3;
        ++(ours ? owned[stage][action] : foreign[stage][action]);
        if (!ours || action != 1 || (stage != 0 && stage != 3) || sampleCount >= capacity) return;
        Snapshot &out = samples[sampleCount];
        out.stage=stage;out.length=uint32_t(length);out.status=status;out.flags2=flags2;
        out.channel=channel;out.macMask=mask;out.gp2=gp2;out.serial=sampleCount;
        const size_t n = length < sizeof(out.frame) ? length : sizeof(out.frame);
        memcpy(out.frame, f, n);
        ++sampleCount;
    }
};
static_assert(sizeof(BaRxDiagnostic::Snapshot)==80,"Diagnostic snapshot ABI");
}
