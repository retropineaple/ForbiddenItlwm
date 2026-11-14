#pragma once
#include "AWDLObservation.hpp"
namespace ItlAWDL {
inline bool shortTcpControlRequest(const uint8_t *frame, size_t length) {
    if (!frame || length > 512 || length < 24 || frame[0] != 0x08 || (frame[4] & 1)) return false;
    DataFrameView view{};
    if (parseDataFrame(frame, length, false, &view) != ParseResult::Valid ||
        view.ip.length < 60 || view.ip.bytes[6] != 6) return false;
    const size_t header = size_t(view.ip.bytes[52] >> 4) * 4;
    return header >= 20 && 40 + header <= view.ip.length &&
        ((view.ip.bytes[53] & 2) || view.ip.length > 40 + header);
}
struct ControlReceiveTag {
    uint8_t peer[6]{};
    int generation = -1;
    uint32_t listener = 0;
    unsigned channel = 0;
    bool valid = false;
    void record(bool candidate, const uint8_t *address, int gen, uint32_t token, unsigned sentChannel) {
        valid = candidate && address && knownAddress(address) && !(address[0] & 1) && sentChannel && sentChannel <= 255;
        if (!valid) return;
        for (unsigned k = 0; k < 6; ++k) peer[k] = address[k];
        generation = gen; listener = token; channel = sentChannel;
    }
    bool take(bool ownSuccess, int gen, uint32_t token, unsigned currentChannel) {
        const bool result = valid && ownSuccess && generation == gen && listener == token && channel == currentChannel;
        valid = false;
        return result;
    }
};
// Starts after an attributed successful TX completion, never from host enqueue
// time. Repeated SYNs cannot extend a window; at most one window per second.
struct ControlReceiveProbe {
    static constexpr uint64_t windowUS = 250000, cooldownUS = 1000000;
    uint8_t peer[6]{};
    uint64_t sinceUS = 0;
    int generation = -1;
    uint32_t listener = 0, arms = 0;
    unsigned channel = 0;
    bool armed = false;
    void clear() { armed = false; }
    bool arm(const uint8_t *address, uint64_t now, int gen, uint32_t token, unsigned sentChannel) {
        if (!address || !knownAddress(address) || (address[0] & 1) || !sentChannel || sentChannel > 255) return false;
        if (generation == gen && listener == token && channel == sentChannel && arms &&
            (now < sinceUS || now - sinceUS < cooldownUS)) return false;
        for (unsigned k = 0; k < 6; ++k) peer[k] = address[k];
        sinceUS = now; generation = gen; listener = token; channel = sentChannel; armed = true; ++arms;
        return true;
    }
    bool wants(bool enabled, bool known, bool quiet, unsigned own, unsigned station,
               uint64_t now, int gen, uint32_t token) const {
        return armed && enabled && known && quiet && own && own == channel && own == station &&
            generation == gen && listener == token && now >= sinceUS && now - sinceUS < windowUS;
    }
};
}
