// Host checks for the per-entry session diagnostic (AWDLSlotDiag.hpp). No firmware or radio.
#include <AWDLSlotDiag.hpp>
#include <cassert>
#include <cstdio>
using namespace ItlAWDL;

int main() {
    unsigned cases = 0;
    const uint64_t E = 65536;   // one extended AW, as kClockEawUS
    const Clock c{E, 0, E};     // entry n covers [n*E, (n+1)*E): clockEawAt(c, n*E) == n
    assert(clockEawAt(c, 2 * E, nullptr) == 2 && clockEawAt(c, 2 * E - 1, nullptr) == 1);
    ++cases;
    assert(SlotDiag::bankFor(2) == 0 && SlotDiag::bankFor(4) == 0 && SlotDiag::bankFor(5) == 1);
    ++cases;
    {
        // The 2026-11-27 idle plan: single listener entries at 2 and 10. A session asked for 12 ms ahead, started
        // 3 ms late and ended 14 ms early: entry 2 is planned in full and covered for 49 ms.
        static SlotDiag d{};
        const uint64_t seq = 16 * E * 10;   // some sequence later, still entry 2
        const uint64_t ps = seq + 2 * E, pe = seq + 3 * E;
        d.noteRun(c, 0, 149, ps - 12000, ps, pe, ps + 3000, pe - 14000, 5, 0);
        assert(d.bank[0][2].runs == 1);
        assert(d.bank[0][2].plannedUS == E);
        assert(d.bank[0][2].coveredUS == E - 17000);
        assert(d.bank[0][3].plannedUS == 0 && d.bank[0][3].coveredUS == 0);
        const SlotRun &r = d.ring[0];
        assert(r.slot == 2 && r.entries == 1 && r.bank == 0 && r.channel == 149);
        assert(r.requestLeadUS == 12000 && r.startOffUS == 3000 && r.endOffUS == -14000);
        assert(r.action == 5 && r.ucastToUs == 0);
        cases += 9;
    }
    {
        // A run over entries 15, 0, 1 (wrapping) splits by entry; a late end spills into entry 2.
        static SlotDiag d{};
        const uint64_t ps = 16 * E * 3 + 15 * E, pe = ps + 3 * E;
        d.noteRun(c, 1, 149, ps, ps, pe, ps, pe + 1000, 0, 7);
        assert(d.bank[1][15].runs == 1 && d.bank[1][0].runs == 0);
        assert(d.bank[1][15].plannedUS == E && d.bank[1][0].plannedUS == E && d.bank[1][1].plannedUS == E);
        assert(d.bank[1][2].plannedUS == 0 && d.bank[1][2].coveredUS == 1000);
        assert(d.ring[0].entries == 3 && d.ring[0].endOffUS == 1000 && d.ring[0].ucastToUs == 7);
        cases += 4;
    }
    {
        // Frames by entry, with and without a session; out-of-range input is ignored.
        static SlotDiag d{};
        d.noteRx(c, 0, 10 * E + 5, kSlotRxUcastToUs, true);
        d.noteRx(c, 0, 10 * E + 6, kSlotRxAction, false);
        d.noteRx(c, 2, 10 * E, kSlotRxAction, true);
        d.noteRx(Clock{0, 0, 0}, 0, 10 * E, kSlotRxAction, true);
        assert(d.bank[0][10].rx[kSlotRxUcastToUs] == 1 && d.bank[0][10].rxNoSession[kSlotRxUcastToUs] == 0);
        assert(d.bank[0][10].rx[kSlotRxAction] == 1 && d.bank[0][10].rxNoSession[kSlotRxAction] == 1);
        cases += 2;
    }
    {
        // A run without a plan (duty sessions) is only counted; the ring wraps.
        static SlotDiag d{};
        d.noteRun(c, 0, 149, 0, 0, 0, 100, 200, 0, 0);
        assert(d.runsNoPlan == 1 && d.runsRecorded == 0);
        for (unsigned i = 0; i < kSlotRunRing + 3; ++i) {
            const uint64_t ps = 16 * E * (i + 1) + 2 * E;
            d.noteRun(c, 0, 149, ps, ps, ps + E, ps, ps + E, uint16_t(i), 0);
        }
        assert(d.runsRecorded == kSlotRunRing + 3 && d.ringNext == kSlotRunRing + 3);
        assert(d.ring[0].action == kSlotRunRing && d.ring[2].action == kSlotRunRing + 2);
        assert(d.bank[0][2].runs == kSlotRunRing + 3);
        cases += 4;
    }
    {
        // Flags are kept per run; filter/station events wrap in their own ring with millisecond times.
        static SlotDiag d{};
        const uint64_t ps = 16 * E * 2 + 10 * E;
        d.noteRun(c, 0, 149, ps, ps, ps + E, ps, ps + E, 1, 0, kSlotFlagPromiscStart | kSlotFlagPromiscEnd);
        assert(d.ring[0].flags == (kSlotFlagPromiscStart | kSlotFlagPromiscEnd));
        d.noteEvent(121710123, 'X', 0);
        d.noteEvent(121711000, 'P', 0);
        assert(d.events[0].ms == 121710 && d.events[0].kind == 'X' && d.events[1].kind == 'P');
        for (unsigned i = 0; i < kSlotEventRing; ++i)
            d.noteEvent(1000ULL * i, 'S', uint8_t(i));
        assert(d.eventNext == kSlotEventRing + 2 && d.events[2].kind == 'S' && d.events[2].arg == 0);
        cases += 3;
    }
    printf("awdl_slot_diag: %u cases passed\n", cases);
    return 0;
}
