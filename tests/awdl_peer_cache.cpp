// Host checks of the Continuity peer cache completion record and the UMI status queue.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../ForbiddenItlwm/AWDLPeerCache.hpp"

using namespace itlawdl;

int main()
{
    // Which operations get a completion, and what it says (Broadcom's firmware also takes 6 as an add).
    assert(peerCacheCompletion(0) == 1);
    assert(peerCacheCompletion(6) == 1);
    assert(peerCacheCompletion(2) == 3);
    assert(peerCacheCompletion(1) == 0);
    assert(peerCacheCompletion(3) == 0);
    assert(peerCacheCompletion(0xffffffffu) == 0);

    // The completion repeats the 464-byte request and changes only the word at +4.
    uint8_t request[peerCacheRecordBytes];
    for (unsigned i = 0; i < sizeof(request); i++)
        request[i] = uint8_t(i * 7 + 3);
    const uint32_t version = 1, operation = 0;
    memcpy(request, &version, 4);
    memcpy(request + 4, &operation, 4);
    const uint8_t peer[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(request + 8, peer, 6);
    assert(memcmp(peerCacheAddress(request), peer, 6) == 0);

    uint8_t done[peerCacheRecordBytes];
    memset(done, 0xee, sizeof(done));
    buildPeerCacheCompletion(request, peerCacheCompletion(operation), done);
    uint32_t result;
    memcpy(&result, done + 4, 4);
    assert(result == 1);
    for (unsigned i = 0; i < sizeof(done); i++)
        if (i < 4 || i >= 8)
            assert(done[i] == request[i]);
    uint32_t requested;
    memcpy(&requested, request + 4, 4);
    assert(requested == 0);   // the request itself is left as it was

    // A delete completes as 3.
    const uint32_t del = 2;
    memcpy(request + 4, &del, 4);
    buildPeerCacheCompletion(request, peerCacheCompletion(del), done);
    memcpy(&result, done + 4, 4);
    assert(result == 3);

    // The unicast action frame status: the address, then a zero status byte (acknowledged), and nothing else set.
    uint8_t status[umiStatusRecordBytes];
    memset(status, 0xff, sizeof(status));
    buildUmiAckStatus(peer, status);
    assert(memcmp(status, peer, 6) == 0);
    assert(status[umiStatusByteOffset] == 0);
    for (unsigned i = 6; i < sizeof(status); i++)
        assert(status[i] == 0);

    // The queue: a peer is queued once, a second peer is kept, and a full list refuses more without losing any.
    uint8_t peers[3][6];
    unsigned count = 0;
    const uint8_t other[6] = {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    const uint8_t third[6] = {0x02, 0x01, 0x02, 0x03, 0x04, 0x05};
    const uint8_t fourth[6] = {0x02, 0x09, 0x08, 0x07, 0x06, 0x05};
    assert(umiAckQueuePush(peers, count, 3, peer) && count == 1);
    assert(umiAckQueuePush(peers, count, 3, peer) && count == 1);      // repeated request, same status
    assert(umiAckQueuePush(peers, count, 3, other) && count == 2);
    assert(umiAckQueuePush(peers, count, 3, third) && count == 3);
    assert(umiAckQueuePush(peers, count, 3, other) && count == 3);     // already queued, even when full
    assert(!umiAckQueuePush(peers, count, 3, fourth) && count == 3);   // full
    assert(memcmp(peers[0], peer, 6) == 0 && memcmp(peers[1], other, 6) == 0 && memcmp(peers[2], third, 6) == 0);

    puts("peer cache completion and UMI status queue: passed (host only)");
    return 0;
}
