// The parts of the Continuity peer cache and UMI status handling that are plain data, so a host test can check them.
// Layouts were read on macOS Sequoia 15.8.2 (24H232) with tools/diag/trace_peer_cache.d and by disassembling
// IO80211Family; see docs/HOW-IT-WORKS.md, section 9.
#pragma once
#include <stdint.h>
#include <string.h>

namespace itlawdl {

// setPEER_CACHE_CONTROL: a 464-byte record. Version at +0, operation at +4, the peer's address at +8.
constexpr unsigned peerCacheRecordBytes = 464;
constexpr unsigned peerCacheOperationOffset = 4;
constexpr unsigned peerCacheAddressOffset = 8;
constexpr uint32_t peerCacheOperationAdd = 0;
constexpr uint32_t peerCacheOperationAddFirmware = 6;   // Broadcom's firmware also takes 6 as an add
constexpr uint32_t peerCacheOperationDelete = 2;
// What the completion event carries at +4.
constexpr uint32_t peerCacheAdded = 1;
constexpr uint32_t peerCacheDeleted = 3;

// The completion for an operation, or 0 when macOS expects none.
inline uint32_t peerCacheCompletion(uint32_t operation)
{
    if (operation == peerCacheOperationAdd || operation == peerCacheOperationAddFirmware)
        return peerCacheAdded;
    if (operation == peerCacheOperationDelete)
        return peerCacheDeleted;
    return 0;
}

inline const uint8_t *peerCacheAddress(const uint8_t *record)
{
    return record + peerCacheAddressOffset;
}

// The completion event (message 51) repeats the request with the result at +4.
inline void buildPeerCacheCompletion(const uint8_t *request, uint32_t result, uint8_t *out)
{
    memcpy(out, request, peerCacheRecordBytes);
    memcpy(out + peerCacheOperationOffset, &result, sizeof(result));
}

// The unicast action frame status (message 69): the peer's address, then a status byte at +6 (0 = acknowledged).
// macOS reads only those seven bytes; the rest of the record is kept zero.
constexpr unsigned umiStatusRecordBytes = 32;
constexpr unsigned umiStatusByteOffset = 6;

inline void buildUmiAckStatus(const uint8_t *peer, uint8_t *out)
{
    memset(out, 0, umiStatusRecordBytes);
    memcpy(out, peer, 6);
}

// A bounded list of peers still owed a status. A peer already in the list isn't added twice, because several
// requests of one registration would otherwise post the same status again. Returns false only when the list is full.
inline bool umiAckQueuePush(uint8_t (*peers)[6], unsigned &count, unsigned depth, const uint8_t *peer)
{
    for (unsigned i = 0; i < count; i++)
        if (!memcmp(peers[i], peer, 6))
            return true;
    if (count >= depth)
        return false;
    memcpy(peers[count++], peer, 6);
    return true;
}

} // namespace itlawdl
