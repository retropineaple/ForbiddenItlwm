#pragma once
#include "AWDLObservation.hpp"
namespace ItlAWDL {
// One successful short-unicast TX hint. Admission still requires the live known-
// peer table in the task; this hint grants no receive or authentication rights.
struct ControlPeerHint {
    static constexpr uint64_t lifetimeUS = 2000000;
    uint8_t peer[6]{};
    uint64_t seenUS = 0;
    int generation = -1;
    uint32_t listener = 0, epoch = 0;
    bool valid = false;
    void clear() { valid = false; }
    bool current(uint64_t now, int gen, uint32_t token, uint32_t owner) const {
        return valid && now >= seenUS && now - seenUS <= lifetimeUS &&
            generation == gen && listener == token && epoch == owner;
    }
    bool note(const uint8_t *address, uint64_t now, int gen, uint32_t token, uint32_t owner) {
        if (!address || !knownAddress(address) || (address[0] & 1)) return false;
        if (current(now, gen, token, owner) && !sameAddress(peer, address)) return false;
        memcpy(peer, address, 6);
        seenUS = now; generation = gen; listener = token; epoch = owner; valid = true;
        return true;
    }
};
}
