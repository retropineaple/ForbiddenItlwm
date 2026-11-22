// Offline construction of what a real AWDL peer sends that our native template
// lacks, plus unicast IPv6 frames meant to make a peer address us. Wire layouts
// cross-checked against seemoo-lab/owl src/frame.h and src/tx.c and against one
// captured peer frame. No code was copied and no peer frame is replayed: all
// values are our own address, metric and schedule, or fixed constants.
#pragma once
#include <AWDLDataPayload.hpp>
#include <AWDLChannels.hpp>
namespace ItlAWDL {
constexpr size_t peerTLVPrefixBytes = 24 + 44;  // Election (5), channel sequence (18).
constexpr size_t peerTLVSuffixBytes = 12;       // Data rate capabilities (7).
// Adds TLVs 5 and 18 before the native template's TLVs and 7 after them. Our
// metric comes from the template's election v2 TLV, so both TLVs agree and this
// claims nothing the template doesn't. Nothing is written unless all checks pass.
// `channelClass` 0 picks the 20 MHz class for `channel`. 128 is the 80 MHz class, as a phone sends.
// `twoStreams` advertises MCS 8 to 15 as well, matching the HT capabilities the driver reports to macOS with
// -itlawdlcaps (dedicated mode's VHT runs had both).
inline bool extendWithPeerTLVs(uint8_t *tlvs, size_t *length, size_t capacity, const uint8_t *address, uint8_t channel = 6,
                               uint8_t channelClass = 0, bool twoStreams = false) {
    if (!tlvs || !length || !address) return false;
    // A channel outside the fixed-schedule list can't be described, so nothing is added. An explicit
    // class must be the channel's 20 MHz class, or 128 (80 MHz) for a 5 GHz channel.
    if (!fixedScheduleChannel(channel)) return false;
    if (channelClass && channelClass != globalOperatingClass20(channel) && !(channelClass == 128 && channel > 14))
        return false;
    const size_t old = *length;
    if (old > capacity) return false;
    const uint8_t *election = nullptr;
    // A template that already carries macOS's own data rate TLV (7), as it does once the driver reports its
    // HT and VHT capabilities, keeps it: ours isn't added on top.
    bool nativeRates = false;
    for (size_t offset = 0; offset < old;) {
        if (old - offset < 3) return false;
        const size_t count = read16(tlvs + offset + 1);
        if (count > old - offset - 3) return false;
        const uint8_t type = tlvs[offset];
        if (type == 5 || type == 18) return false;
        if (type == 7) nativeRates = true;
        if (type == 24 && count >= 28) election = tlvs + offset + 3;
        offset += 3 + count;
    }
    const size_t suffix = nativeRates ? 0 : peerTLVSuffixBytes;
    if (!election || capacity - old < peerTLVPrefixBytes + suffix) return false;
    const uint32_t metric = read32(election + 24);
    for (size_t i = old; i > 0; --i) tlvs[i - 1 + peerTLVPrefixBytes] = tlvs[i - 1];
    uint8_t *p = tlvs;
    for (size_t i = 0; i < peerTLVPrefixBytes; ++i) p[i] = 0;
    p[0] = 5; write16(p + 1, 21);
    for (unsigned i = 0; i < 6; ++i) p[3 + 5 + i] = address[i];
    write32(p + 3 + 11, metric);
    write32(p + 3 + 15, metric);
    p += 24;
    // Sixteen slots of our channel, matching the schedule in our sync TLV. Byte two
    // is a global operating class: 0x51 (81) is how a captured peer encodes channel
    // 6, and 124 is the 20 MHz class containing channel 149.
    p[0] = 18; write16(p + 1, 41);
    const uint8_t header[6] = {15, 3, 0, 3, 0xff, 0xff};
    for (unsigned i = 0; i < 6; ++i) p[3 + i] = header[i];
    const uint8_t opClass = channelClass ? channelClass : globalOperatingClass20(channel);
    for (unsigned i = 0; i < 16; ++i) { p[9 + 2*i] = channel; p[10 + 2*i] = opClass; }
    if (nativeRates) {
        *length = old + peerTLVPrefixBytes;
        return true;
    }
    uint8_t *q = tlvs + peerTLVPrefixBytes + old;
    for (size_t i = 0; i < peerTLVSuffixBytes; ++i) q[i] = 0;
    // Conservative HT: one stream, no A-MSDU bit, so replies stay decodable.
    q[0] = 7; write16(q + 1, 9);
    q[3 + 2] = 0x6f; q[3 + 4] = 0x1b; q[3 + 5] = 0xff;
    if (twoStreams) q[3 + 6] = 0xff;
    *length = old + peerTLVPrefixBytes + peerTLVSuffixBytes;
    return true;
}
// -itlawdlslothop: rewrites the sixteen entries of the channel sequence TLV (18) in a run of TLVs to `channels` (0 keeps the entry as it is)
// with `classes`, so the sequence we announce is the plan the radio really follows. A TLV 18 of any other shape is left alone.
// Returns true when entries were written.
inline bool applySlotChannels(uint8_t *tlvs, size_t length, const uint8_t *channels, const uint8_t *classes) {
    if (!tlvs || !channels || !classes) return false;
    for (size_t offset = 0; offset + 3 <= length;) {
        const size_t count = read16(tlvs + offset + 1);
        if (count > length - offset - 3) return false;
        uint8_t *v = tlvs + offset + 3;
        if (tlvs[offset] == 18 && count == 41 && v[0] == 15 && v[1] == 3 && v[2] == 0 && v[3] == 3) {
            for (unsigned i = 0; i < 16; ++i) {
                if (!channels[i]) continue;
                v[6 + 2 * i] = channels[i];
                v[7 + 2 * i] = classes[i];
            }
            return true;
        }
        offset += 3 + count;
    }
    return false;
}
// Prepares the peer manager's master indication TLVs for our frame builder. Native
// sync (4) goes, since the builder supplies it. Native channel sequence (18) goes
// because it advertises hopping this radio doesn't do; extendWithPeerTLVs adds a
// channel-6 one. Election (5) and data rate (7) go so they aren't duplicated.
// Everything else, including services and ARPA, is kept byte for byte.
// True when bytes from macOS's master indication request are a complete run of TLVs that ends exactly
// at length and holds an election v2 TLV (type 24, at least 28 bytes), which the frame builder needs
// anyway. Checked before the bytes are kept, so stray memory behind a wrong pointer is far less likely
// to be stored and broadcast. It doesn't make a wrong pointer safe to read.
inline bool masterIndicationWellFormed(const uint8_t *source, size_t length) {
    if (!source || !length) return false;
    bool election = false;
    for (size_t offset = 0; offset < length;) {
        if (length - offset < 3) return false;
        const size_t count = read16(source + offset + 1);
        if (count > length - offset - 3) return false;
        election |= source[offset] == 24 && count >= 28;
        offset += 3 + count;
    }
    return election;
}
inline bool buildMasterIndicationTLVs(const uint8_t *source, size_t length, const uint8_t *address,
                                      uint8_t *output, size_t capacity, size_t *written, uint8_t channel = 6,
                                      uint8_t channelClass = 0, bool twoStreams = false) {
    if (!source || !address || !output || !written || !length) return false;
    size_t used = 0;
    for (size_t offset = 0; offset < length;) {
        if (length - offset < 3) return false;
        const size_t count = read16(source + offset + 1);
        if (count > length - offset - 3) return false;
        const uint8_t type = source[offset];
        if (type != 4 && type != 18 && type != 5 && type != 7) {
            if (capacity - used < 3 + count) return false;
            for (size_t i = 0; i < 3 + count; ++i) output[used + i] = source[offset + i];
            used += 3 + count;
        }
        offset += 3 + count;
    }
    if (!extendWithPeerTLVs(output, &used, capacity, address, channel, channelClass, twoStreams)) return false;
    *written = used;
    return true;
}
// Converts one Ethernet frame from macOS into an FCS-free AWDL data frame. Only
// IPv6 from our own address is carried. All checks precede writes. Input and
// output must be disjoint and remain caller-owned.
// plain: the low latency (llw0) form, a bare RFC 1042 header 'aa aa 03 00 00 00 86 dd' and no AWDL header or
// sequence, which is what Broadcom's firmware sends for llw0 (its host code adds the Apple SNAP for awdl0 only).
// qosTid7 (with plain): QoS data with TID 7 (voice) in a 26-byte header, as an iPad's llw0 sends it (35 samples in
// Bitboss2/ForbiddenItlwm c351dc2); the default is the non-QoS header above.
inline BuildResult buildDataFrameFromEthernet(const uint8_t *eth, size_t length, const uint8_t *own,
                                              uint16_t sequence, uint8_t *output, size_t available, size_t *written,
                                              bool plain = false, bool qosTid7 = false) {
    if (!eth || !own || !output || !written || length < 14 + 40 || eth[12] != 0x86 || eth[13] != 0xdd)
        return BuildResult::Invalid;
    bool destination = false, sameSource = true;
    for (unsigned i = 0; i < 6; ++i) { destination |= eth[i] != 0; sameSource &= eth[6 + i] == own[i]; }
    if (!destination || !sameSource || (*own & 1) || !validIPv6Payload(eth + 14, length - 14))
        return BuildResult::Invalid;
    const unsigned header = plain && qosTid7 ? 26 : 24;
    if (available < header + (plain ? 8 : 16) + (length - 14)) return BuildResult::NoSpace;
    for (unsigned i = 0; i < header; ++i) output[i] = 0;
    output[0] = header == 26 ? 0x88 : 0x08;
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    for (unsigned i = 0; i < 6; ++i) {
        output[4 + i] = eth[i];
        output[10 + i] = own[i];
        output[16 + i] = bssid[i];
    }
    write16(output + 22, uint16_t((sequence & 4095) << 4));
    if (plain) {
        const uint8_t snap[8] = {0xaa, 0xaa, 3, 0, 0, 0, 0x86, 0xdd};
        if (header == 26) output[24] = 7;   // QoS control: TID 7, normal ack, not an A-MSDU
        for (unsigned i = 0; i < 8; ++i) output[header + i] = snap[i];
        for (size_t i = 0; i < length - 14; ++i) output[header + 8 + i] = eth[14 + i];
        *written = header + 8 + (length - 14);
        return BuildResult::Ready;
    }
    size_t payload = 0;
    const auto result = buildIPv6DataPayload(eth + 14, length - 14, sequence, output + 24, available - 24, &payload);
    if (result != BuildResult::Ready) return result;
    *written = 24 + payload;
    return BuildResult::Ready;
}
// Modified EUI-64, which is what awdl0 uses for its own link-local address.
inline void linkLocalFromAddress(const uint8_t *m, uint8_t *ip) {
    for (unsigned i = 0; i < 16; ++i) ip[i] = 0;
    ip[0] = 0xfe; ip[1] = 0x80;
    ip[8] = uint8_t(m[0] ^ 2); ip[9] = m[1]; ip[10] = m[2];
    ip[11] = 0xff; ip[12] = 0xfe;
    ip[13] = m[3]; ip[14] = m[4]; ip[15] = m[5];
}
inline uint16_t icmpv6Checksum(const uint8_t *packet, size_t length) {
    uint32_t sum = 0;
    auto add = [&sum](const uint8_t *b, size_t n) {
        for (size_t i = 0; i < n; i += 2)
            sum += (uint32_t(b[i]) << 8) | (i + 1 < n ? b[i + 1] : 0);
    };
    add(packet + 8, 32);                 // Source and destination addresses.
    sum += uint32_t(length - 40);        // Upper-layer length.
    sum += 58;                           // Next header.
    add(packet + 40, length - 40);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return uint16_t(~sum);
}
enum class PeerRequest { Neighbor, Echo };
constexpr size_t peerRequestICMPBytes(PeerRequest r) { return r == PeerRequest::Neighbor ? 32 : 24; }
// Builds one complete, FCS-free unicast AWDL data frame. A Neighbor Solicitation
// carries our link-layer address, so the receiver can answer directly without
// first resolving us (RFC 4861 section 7.2.3). All checks precede writes. Output
// must be disjoint from the addresses and remains caller-owned.
inline BuildResult buildPeerRequest(PeerRequest request, const uint8_t *own, const uint8_t *peer,
                                    const uint8_t *peerIP, uint16_t sequence,
                                    uint8_t *output, size_t available, size_t *written) {
    if (!own || !peer || !peerIP || !output || !written || (*own & 1) || (*peer & 1)) return BuildResult::Invalid;
    bool ownSet = false, peerSet = false;
    for (unsigned i = 0; i < 6; ++i) { ownSet |= own[i] != 0; peerSet |= peer[i] != 0; }
    if (!ownSet || !peerSet) return BuildResult::Invalid;
    const size_t icmp = peerRequestICMPBytes(request);
    uint8_t ip[40 + 32] = {};
    ip[0] = 0x60;
    ip[5] = uint8_t(icmp);
    ip[6] = 58;
    ip[7] = 255;                         // Required for neighbor discovery.
    linkLocalFromAddress(own, ip + 8);
    for (unsigned i = 0; i < 16; ++i) ip[24 + i] = peerIP[i];
    if (request == PeerRequest::Neighbor) {
        ip[40] = 135;
        for (unsigned i = 0; i < 16; ++i) ip[48 + i] = peerIP[i];
        ip[64] = 1; ip[65] = 1;          // Source link-layer address option.
        for (unsigned i = 0; i < 6; ++i) ip[66 + i] = own[i];
    } else {
        ip[40] = 128;
        ip[44] = 0x49; ip[45] = 0x54;    // Identifier.
        ip[46] = uint8_t(sequence >> 8); ip[47] = uint8_t(sequence);
        for (unsigned i = 0; i < 16; ++i) ip[48 + i] = uint8_t(0xa0 + i);
    }
    const uint16_t checksum = icmpv6Checksum(ip, 40 + icmp);
    ip[42] = uint8_t(checksum >> 8); ip[43] = uint8_t(checksum);
    if (available < 24 + 16 + 40 + icmp) return BuildResult::NoSpace;
    for (unsigned i = 0; i < 24; ++i) output[i] = 0;
    output[0] = 0x08;
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    for (unsigned i = 0; i < 6; ++i) {
        output[4 + i] = peer[i];
        output[10 + i] = own[i];
        output[16 + i] = bssid[i];
    }
    write16(output + 22, uint16_t((sequence & 4095) << 4));
    size_t payload = 0;
    const auto result = buildIPv6DataPayload(ip, 40 + icmp, sequence, output + 24, available - 24, &payload);
    if (result != BuildResult::Ready) return result;
    *written = 24 + payload;
    return BuildResult::Ready;
}
}
