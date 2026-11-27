// -btleadvlog (or with -btleadv and itlawdlsched=3): what bluetoothd asks the controller to advertise, read from the
// outgoing HCI commands only. For the 2026-11-27 idle stall, where an iPhone heard this Mac's AirDrop advert once at
// -73 dBm (-53 a few minutes earlier) and didn't act on it: per advertising set, the interval, primary channels and
// TX power bluetoothd requested, and when sets were enabled or disabled. Nothing is changed; the controller's replies
// (the TX power it actually selected) aren't seen here.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace BTLEAdv {

enum : uint16_t { kOpLeSetExtAdvParams = 0x2036, kOpLeSetExtAdvEnable = 0x2039 };
constexpr unsigned kAdvLogRing = 24, kAdvLogSets = 8;

struct AdvSetParams {
    bool known;
    uint16_t properties;
    uint32_t intervalMin, intervalMax;   // 0.625 ms units
    uint8_t channelMap;                  // bit 0 = 37, bit 1 = 38, bit 2 = 39
    int8_t txPower;                      // dBm requested; 127 = no preference
    uint8_t primaryPhy;
    bool enabled;
    uint32_t enables, disables;
};

struct AdvLogEntry {
    uint32_t ms;
    uint8_t kind;      // 'P' parameters, 'E' enable, 'D' disable
    uint8_t handle;
    uint16_t intervalMs10;   // P: maximum interval in tenths of a ms; E: duration in 10 ms units
    uint8_t channelMap;
    int8_t txPower;
};

struct AdvLog {
    AdvSetParams sets[kAdvLogSets];
    AdvLogEntry ring[kAdvLogRing];
    uint32_t ringNext;
    uint32_t paramsSeen, enablesSeen, malformed;

    void push(uint64_t nowMs, uint8_t kind, uint8_t handle, uint16_t v, uint8_t map, int8_t tx) {
        AdvLogEntry &e = ring[ringNext % kAdvLogRing];
        ++ringNext;
        e.ms = uint32_t(nowMs);
        e.kind = kind;
        e.handle = handle;
        e.intervalMs10 = v;
        e.channelMap = map;
        e.txPower = tx;
    }

    static uint32_t le24(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16; }

    // hci: the whole command, opcode first, then the parameter length and the parameters.
    void onCommand(const uint8_t *hci, size_t length, uint64_t nowMs) {
        if (!hci || length < 3)
            return;
        const uint16_t op = uint16_t(hci[0] | hci[1] << 8);
        const uint8_t plen = hci[2];
        const uint8_t *p = hci + 3;
        const size_t have = length - 3 < plen ? length - 3 : plen;
        if (op == kOpLeSetExtAdvParams) {
            // handle, properties(2), interval min(3), max(3), channel map, own address type, peer address type,
            // peer address(6), filter policy, TX power, primary PHY, ...
            if (plen < 21 || have < 21) {
                ++malformed;
                return;
            }
            ++paramsSeen;
            const uint8_t handle = p[0];
            AdvSetParams s{};
            s.known = true;
            s.properties = uint16_t(p[1] | p[2] << 8);
            s.intervalMin = le24(p + 3);
            s.intervalMax = le24(p + 6);
            s.channelMap = p[9];
            s.txPower = int8_t(p[19]);
            s.primaryPhy = p[20];
            if (handle < kAdvLogSets) {
                s.enabled = sets[handle].enabled;
                s.enables = sets[handle].enables;
                s.disables = sets[handle].disables;
                sets[handle] = s;
            }
            const uint32_t tenths = s.intervalMax * 625 / 100;   // 0.625 ms units -> tenths of a ms
            push(nowMs, 'P', handle, uint16_t(tenths > 0xffff ? 0xffff : tenths), s.channelMap, s.txPower);
        } else if (op == kOpLeSetExtAdvEnable) {
            // enable, number of sets, then per set: handle, duration(2, 10 ms units), max events
            if (plen < 2 || have < 2) {
                ++malformed;
                return;
            }
            const bool enable = p[0] != 0;
            const uint8_t n = p[1];
            if (have < 2 + 4 * size_t(n)) {
                ++malformed;
                return;
            }
            ++enablesSeen;
            if (n == 0) {   // all sets (a disable with 0 sets disables every set)
                for (auto &s : sets)
                    if (s.known && s.enabled != enable) {
                        s.enabled = enable;
                        ++(enable ? s.enables : s.disables);
                    }
                push(nowMs, enable ? 'E' : 'D', 0xff, 0, 0, 0);
                return;
            }
            for (unsigned i = 0; i < n; ++i) {
                const size_t off = 2 + 4 * size_t(i);
                const uint8_t handle = p[off];
                const uint16_t duration = uint16_t(p[off + 1] | p[off + 2] << 8);
                if (handle < kAdvLogSets) {
                    sets[handle].enabled = enable;
                    ++(enable ? sets[handle].enables : sets[handle].disables);
                }
                push(nowMs, enable ? 'E' : 'D', handle, duration, 0, 0);
            }
        }
    }
};

}  // namespace BTLEAdv
