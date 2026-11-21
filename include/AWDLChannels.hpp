// The channels a local AWDL schedule may name, and how our frames encode them. Radar (DFS) channels are
// listed too: AWDL meets a peer on the Wi-Fi network's channel when both are associated there, as Apple's
// devices do (an iPad answering a Sidecar start lists its network's channel, 60 or 112, in 14 of its 16
// entries). Whether the firmware lets us send on one at all is still decided at send time from its own
// channel flags, and a radar channel only ever while our station is associated on it.
#pragma once
#include <stdint.h>

namespace ItlAWDL {

inline bool fixedScheduleChannel(uint8_t channel) {
    if (channel >= 1 && channel <= 13) return true;
    if (channel >= 36 && channel <= 64) return channel % 4 == 0;
    if (channel >= 100 && channel <= 144) return channel % 4 == 0;
    if (channel >= 149 && channel <= 165) return (channel - 149) % 4 == 0;
    return false;
}

// The 20 MHz global operating class for such a channel (IEEE 802.11 Annex E, table E-4); 0 if not listed.
// A captured peer encodes channel 6 as 81, and 149 as 124.
inline uint8_t globalOperatingClass20(uint8_t channel) {
    if (!fixedScheduleChannel(channel)) return 0;
    if (channel <= 13) return 81;
    if (channel <= 48) return 115;
    if (channel <= 64) return 118;
    if (channel <= 144) return 121;
    if (channel <= 161) return 124;
    return 125;
}

// Flags byte of a legacy channel-sequence slot: 0x2b carries the 2.4 GHz band bit, 0x1b the 5 GHz one.
inline uint8_t legacySlotFlags(uint8_t channel) { return channel <= 14 ? 0x2b : 0x1b; }

// How many of a peer's 16 sequence entries name `channel`.
inline unsigned entriesOn(const uint8_t channels[16], uint8_t channel) {
    unsigned n = 0;
    for (unsigned k = 0; k < 16; ++k) n += channel && channels[k] == channel;
    return n;
}

// The Wi-Fi network's channel mode. A peer that lists our station's channel in at least 8 of its 16 entries
// is met there; it is left once it lists it in 3 or fewer, or goes unheard. Sidecar's real-time group lists
// it in 14; idle and discovery schedules in 0 to 2.
constexpr unsigned kInfraFollowEnterEntries = 8;
constexpr unsigned kInfraFollowLeaveEntries = 3;
inline bool infraFollowWanted(bool following, unsigned entries) {
    return following ? entries > kInfraFollowLeaveEntries : entries >= kInfraFollowEnterEntries;
}

}
