// The channels a fixed local AWDL schedule may name, and how our frames encode them. Only channels with no
// radar duty in common regulatory tables are listed; whether the firmware lets us send on one at all is
// still decided at send time from its own channel flags.
#pragma once
#include <stdint.h>

namespace ItlAWDL {

inline bool fixedScheduleChannel(uint8_t channel) {
    if (channel >= 1 && channel <= 13) return true;
    if (channel >= 36 && channel <= 48) return channel % 4 == 0;
    if (channel >= 149 && channel <= 165) return (channel - 149) % 4 == 0;
    return false;
}

// The 20 MHz global operating class for such a channel (IEEE 802.11 Annex E, table E-4); 0 if not listed.
// A captured peer encodes channel 6 as 81, and 149 as 124.
inline uint8_t globalOperatingClass20(uint8_t channel) {
    if (!fixedScheduleChannel(channel)) return 0;
    if (channel <= 13) return 81;
    if (channel <= 48) return 115;
    if (channel <= 161) return 124;
    return 125;
}

// A channel allowed in our schedule only as a router (infra) entry: the fixed-schedule channels plus the
// radar (DFS) channels a station can be associated on, 52-64 and 100-144. Such an entry only says the Mac is
// away with its router there; the listener never transmits on a radar channel, so it never takes one.
inline bool routerEntryChannel(uint8_t channel) {
    if (fixedScheduleChannel(channel)) return true;
    if (channel >= 52 && channel <= 64) return channel % 4 == 0;
    if (channel >= 100 && channel <= 144) return channel % 4 == 0;
    return false;
}

// The 20 MHz global operating class for a router entry (IEEE 802.11 Annex E, table E-4): 118 for 52-64 and
// 121 for 100-144, else the fixed-schedule class; 0 if not listed.
inline uint8_t routerOperatingClass20(uint8_t channel) {
    if (!routerEntryChannel(channel)) return 0;
    if (channel >= 52 && channel <= 64) return 118;
    if (channel >= 100 && channel <= 144) return 121;
    return globalOperatingClass20(channel);
}

// Flags byte of a legacy channel-sequence slot: 0x2b carries the 2.4 GHz band bit, 0x1b the 5 GHz one.
inline uint8_t legacySlotFlags(uint8_t channel) { return channel <= 14 ? 0x2b : 0x1b; }

}
