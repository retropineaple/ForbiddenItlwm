// Which width an AWDL peer may use toward us, as carried by the operating classes of TLV 18 (channel sequence).
// IO80211AWDLPeer::channelSequenceMinimumBW (asm, IO80211Family 24H232) takes the minimum over the slots' widths, so
// the classes we announce bound the iPhone's transmit width. Class widths (AWDL_TLV18_OPCLASS_PARSER.md, from
// convertOpClassToChannel's table): 81 82 115 118 121 124 125 = 20 MHz; 83 84 116 117 119 120 122 123 126 127 = 40;
// 128 130 = 80; 129 = 160.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ItlAWDL {
// The 20 MHz operating class that contains a channel, 0 if there is none.
inline uint8_t class20ForChannel(uint8_t channel) {
    if (channel >= 1 && channel <= 13) return 81;
    if (channel == 14) return 82;
    if (channel >= 36 && channel <= 48) return 115;
    if (channel >= 52 && channel <= 64) return 118;
    if (channel >= 100 && channel <= 144) return 121;
    if (channel >= 149 && channel <= 165) return 125;
    return 0;
}
// Width in MHz of an operating class AWDL accepts; 0 if the class is not one of them.
inline unsigned classWidthMHz(uint8_t c) {
    if (c == 81 || c == 82 || c == 115 || c == 118 || c == 121 || c == 124 || c == 125) return 20;
    if (c == 83 || c == 84 || c == 116 || c == 117 || c == 119 || c == 120 || c == 122 || c == 123 || c == 126 || c == 127) return 40;
    if (c == 128 || c == 130) return 80;
    if (c == 129) return 160;
    return 0;
}
// Rewrites, in an AWDL action frame (802.11 header and fixed fields, then TLVs from byte 40), the operating class
// of every served slot of TLV 18 (encoding 3: {channel, class} pairs from payload byte 6) to the 20 MHz class of its
// channel. Returns how many entries changed. Anything malformed, another encoding or no TLV 18: 0, frame untouched.
inline unsigned forceClass20(uint8_t *frame, size_t length) {
    if (!frame || length < 43) return 0;
    for (size_t i = 40; i + 3 <= length;) {
        const size_t n = size_t(frame[i + 1]) | size_t(frame[i + 2]) << 8;
        if (i + 3 + n > length) return 0;
        if (frame[i] == 18) {
            if (n < 6 || frame[i + 4] != 3) return 0;
            const size_t slots = size_t(frame[i + 3]) + 1;
            unsigned changed = 0;
            for (size_t j = 0; j < slots && 6 + 2 * j + 2 <= n; ++j) {
                uint8_t *entry = frame + i + 3 + 6 + 2 * j;
                const uint8_t cls = class20ForChannel(entry[0]);
                if (entry[0] && cls && classWidthMHz(entry[1]) != 20) {   // already 20 MHz: as sent
                    entry[1] = cls;
                    ++changed;
                }
            }
            return changed;
        }
        i += 3 + n;
    }
    return 0;
}
}
