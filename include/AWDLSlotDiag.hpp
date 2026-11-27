// -itlslotdiag (on with itlawdlsched=3): where in macOS's 16-entry AWDL sequence the listener's sessions really ran,
// and which entries it heard the peer in. For the idle stall of 2026-11-27: with macOS in Infra Priority the plan gives
// the listener one or two isolated entries, and the iPhone's unicast frames in exactly those entries got no ACK. The
// counters say whether the radio was there (session coverage, action frames heard) and whether unicast to us arrived.
// Measurement only; nothing here changes a session or a frame.
#pragma once
#include <stdint.h>
#include <AWDLElection.hpp>

namespace ItlAWDL {

// Bank 0: the plan gives the listener at most kSlotSparseEntries entries (idle, Infra Priority); bank 1: more.
constexpr unsigned kSlotSparseEntries = 4;
constexpr unsigned kSlotRunRing = 24;
constexpr unsigned kSlotEventRing = 16;

// SlotRun::flags: the listener's receive filter and the first peer slot's station, at the session's start and end.
constexpr uint8_t kSlotFlagPromiscStart = 0x1, kSlotFlagPromiscEnd = 0x2, kSlotFlagStationStart = 0x4,
                  kSlotFlagStationEnd = 0x8;

// A receive-filter or station change: 'P' promiscuous receive on, 'p' off, 'S' a peer station added, 's' removed
// (arg = peer slot), 'X' a station removed because the peer was no longer heard (arg = slot).
struct SlotEvent {
    uint32_t ms;            // uptime
    char kind;
    uint8_t arg;
};

enum SlotRx : uint8_t { kSlotRxAction = 0, kSlotRxUcastToUs = 1, kSlotRxGroup = 2 };

struct SlotStats {
    uint32_t runs;          // sessions that began in this entry (a run's first entry)
    uint64_t plannedUS;     // planned run time that fell in this entry
    uint64_t coveredUS;     // time a confirmed session (firmware start to end) covered this entry
    uint32_t rx[3];         // frames heard on the listener's channel in this entry, by SlotRx
    uint32_t rxNoSession[3];// of those, heard while no session was running
};

struct SlotRun {
    uint8_t slot;           // the entry the planned run starts in (eaw % 16)
    uint8_t entries;        // planned length in entries, rounded
    uint8_t bank;
    uint8_t channel;
    int32_t requestLeadUS;  // planned start minus the time the session was asked for
    int32_t startOffUS;     // firmware start minus planned start (late is positive)
    int32_t endOffUS;       // firmware end minus planned end (late is positive)
    uint16_t action;        // action frames heard during the session
    uint16_t ucastToUs;     // unicast data to our AWDL address heard during the session
    uint8_t flags;          // kSlotFlag*
};

struct SlotDiag {
    SlotStats bank[2][16];
    SlotRun ring[kSlotRunRing];
    uint32_t ringNext;
    uint32_t runsRecorded, runsNoPlan;
    // Unicast data to our address heard while promiscuous receive was live / while it was off (a peer station set).
    uint32_t ucastPromisc, ucastFiltered;
    SlotEvent events[kSlotEventRing];
    uint32_t eventNext;

    void noteEvent(uint64_t nowUS, char kind, uint8_t arg) {
        SlotEvent &e = events[eventNext % kSlotEventRing];
        ++eventNext;
        e.ms = uint32_t(nowUS / 1000);
        e.kind = kind;
        e.arg = arg;
    }

    static unsigned bankFor(unsigned listenerEntries) { return listenerEntries <= kSlotSparseEntries ? 0 : 1; }

    // Calls f(entry % 16, overlapUS) for each extended AW that [a, b) overlaps, at most 64 of them.
    template <typename F> static void forEachEntry(const Clock &c, uint64_t a, uint64_t b, F f) {
        if (!c.eawUS || b <= a)
            return;
        uint64_t t = a;
        for (unsigned guard = 0; t < b && guard < 64; ++guard) {
            uint64_t into = 0;
            const uint32_t eaw = clockEawAt(c, t, &into);
            const uint64_t entryEnd = t - into + c.eawUS;
            const uint64_t stop = entryEnd < b ? entryEnd : b;
            f(eaw % 16, stop - t);
            t = stop;
        }
    }

    void noteRx(const Clock &c, unsigned bankIndex, uint64_t t, SlotRx kind, bool inSession) {
        if (!c.eawUS || bankIndex > 1 || kind > kSlotRxGroup)
            return;
        SlotStats &s = bank[bankIndex][clockEawAt(c, t, nullptr) % 16];
        ++s.rx[kind];
        if (!inSession)
            ++s.rxNoSession[kind];
    }

    // One finished session: its planned run [plannedStart, plannedEnd) and what the firmware reported [start, end).
    void noteRun(const Clock &c, unsigned bankIndex, uint8_t channel, uint64_t requestedUS, uint64_t plannedStart,
                 uint64_t plannedEnd, uint64_t start, uint64_t end, uint16_t action, uint16_t ucastToUs,
                 uint8_t flags = 0) {
        if (bankIndex > 1 || !c.eawUS)
            return;
        if (!plannedStart || plannedEnd <= plannedStart || end < start) {
            ++runsNoPlan;
            return;
        }
        SlotStats *b = bank[bankIndex];
        ++b[clockEawAt(c, plannedStart, nullptr) % 16].runs;
        forEachEntry(c, plannedStart, plannedEnd, [&](unsigned e, uint64_t us) { b[e].plannedUS += us; });
        forEachEntry(c, start, end, [&](unsigned e, uint64_t us) { b[e].coveredUS += us; });
        SlotRun &r = ring[ringNext % kSlotRunRing];
        ++ringNext;
        ++runsRecorded;
        r.slot = uint8_t(clockEawAt(c, plannedStart, nullptr) % 16);
        const uint64_t len = plannedEnd - plannedStart;
        const uint64_t entries = (len + c.eawUS / 2) / c.eawUS;
        r.entries = uint8_t(entries > 255 ? 255 : entries);
        r.bank = uint8_t(bankIndex);
        r.channel = channel;
        r.requestLeadUS = clamp32(int64_t(plannedStart) - int64_t(requestedUS));
        r.startOffUS = clamp32(int64_t(start) - int64_t(plannedStart));
        r.endOffUS = clamp32(int64_t(end) - int64_t(plannedEnd));
        r.action = action;
        r.ucastToUs = ucastToUs;
        r.flags = flags;
    }

    static int32_t clamp32(int64_t v) {
        return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : int32_t(v);
    }
};

}  // namespace ItlAWDL
