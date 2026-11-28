// -itlawdlframedump (diagnostic, off by default; on with itlres bit 0x2): the last few AWDL action frames we built
// (MI/PSF, from macOS's template plus our sync parameters) and the last few we heard, as raw bytes in the registry
// (ITLAWDLFrameDumpTx, ITLAWDLFrameDumpRx), for tools/compat/awdl_frame_dump.py. For the 2026-11-27 stalls: after
// the iPhone woke, it logged our frames as "SyncErrors(8), SameTree=No" and didn't adopt us as master although our
// metric was higher; IO80211's checks read fields of the sync parameters and election TLVs, so the two sides' frames
// are compared byte by byte. Measurement only: nothing here changes a frame.
// The frames hold MAC addresses and the device names in service TLVs: local diagnosis only (the decoder prints
// addresses shortened and skips service contents).
// All callers run in the driver's workloop gate (the frame builder checks inGate, the receive path and the publish
// timer run there), so no lock.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ItlAWDL {

constexpr size_t kFrameDumpBytes = 640;     // an MI with services is ~300-600 bytes; longer frames are cut
constexpr unsigned kFrameDumpEntries = 8;
constexpr uint64_t kFrameDumpGapUS = 100000;  // at most ten a second per direction
constexpr size_t kFrameDumpHeader = 8;      // per entry: u32 uptime ms, u16 stored length, u8 channel, u8 cut

struct FrameDumpEntry {
    uint32_t ms;
    uint16_t length;
    uint8_t channel;
    uint8_t cut;
    uint8_t bytes[kFrameDumpBytes];
};

struct FrameDump {
    FrameDumpEntry entry[kFrameDumpEntries];
    uint32_t next;       // entries written so far (the ring index is next % kFrameDumpEntries)
    uint64_t lastUS;     // when the last one was kept (0 = none)
    uint32_t skipped;    // frames not kept because of the gap
};

// Keeps a copy of one frame if the gap since the last kept one has passed. Returns whether it was kept.
inline bool frameDumpNote(FrameDump &d, uint64_t nowUS, const uint8_t *data, size_t length, uint8_t channel)
{
    if (!data || !length)
        return false;
    if (d.lastUS && nowUS >= d.lastUS && nowUS - d.lastUS < kFrameDumpGapUS) {
        ++d.skipped;
        return false;
    }
    FrameDumpEntry &e = d.entry[d.next % kFrameDumpEntries];
    const size_t keep = length < kFrameDumpBytes ? length : kFrameDumpBytes;
    memcpy(e.bytes, data, keep);
    e.ms = uint32_t(nowUS / 1000);
    e.length = uint16_t(keep);
    e.channel = channel;
    e.cut = keep < length ? 1 : 0;
    ++d.next;
    d.lastUS = nowUS ? nowUS : 1;
    return true;
}

// Oldest first: per entry the 8-byte header (little endian) then the stored bytes. Returns the bytes written;
// entries that don't fit whole are left out.
inline size_t frameDumpSerialize(const FrameDump &d, uint8_t *out, size_t capacity)
{
    const uint32_t count = d.next < kFrameDumpEntries ? d.next : kFrameDumpEntries;
    size_t used = 0;
    for (uint32_t i = d.next - count; i != d.next; ++i) {
        const FrameDumpEntry &e = d.entry[i % kFrameDumpEntries];
        if (used + kFrameDumpHeader + e.length > capacity)
            break;
        out[used + 0] = uint8_t(e.ms);
        out[used + 1] = uint8_t(e.ms >> 8);
        out[used + 2] = uint8_t(e.ms >> 16);
        out[used + 3] = uint8_t(e.ms >> 24);
        out[used + 4] = uint8_t(e.length);
        out[used + 5] = uint8_t(e.length >> 8);
        out[used + 6] = e.channel;
        out[used + 7] = e.cut;
        memcpy(out + used + kFrameDumpHeader, e.bytes, e.length);
        used += kFrameDumpHeader + e.length;
    }
    return used;
}

// An AWDL action frame (802.11 action to AWDL's BSSID, Apple's vendor category): PSF (subtype 0) or MI (3).
inline bool frameDumpIsSync(const uint8_t *f, size_t length)
{
    return length >= 32 && f[0] == 0xd0 && f[24] == 0x7f && f[25] == 0x00 && f[26] == 0x17 && f[27] == 0xf2 &&
        f[28] == 0x08 && (f[30] == 0 || f[30] == 3);
}

}  // namespace ItlAWDL
