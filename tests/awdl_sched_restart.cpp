// Host checks for the scheduler's restart rule (AWDLSchedRestart.hpp). No firmware or radio.
#include <AWDLSchedRestart.hpp>
#include <cassert>
#include <cstdio>
using namespace ItlAWDL;

static constexpr uint64_t MIN = 60ULL * 1000000;

int main() {
    unsigned cases = 0;
    uint64_t at = 0;
    // Only 2, 6, 10 and 12 are not faults.
    for (unsigned site = 0; site < 16; ++site, ++cases)
        assert(isFaultRestartSite(site) == !(site == 2 || site == 6 || site == 10 || site == 12));
    {
        // 2026-11-27: two joins, then a crash at the third join, then a join: the joins are free.
        SchedFaultPolicy p;
        assert(p.onNewGeneration(false, true, 1 * MIN, &at) == SchedGenAction::Resume);
        assert(p.onNewGeneration(false, true, 2 * MIN, &at) == SchedGenAction::Resume);
        assert(p.onNewGeneration(true, true, 3 * MIN, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.onNewGeneration(false, true, 4 * MIN, &at) == SchedGenAction::Resume);
        assert(p.faults == 1 && p.pauses == 0);
        cases += 5;
    }
    {
        // Any number of joins, toggles and wakes never stop or pause it.
        SchedFaultPolicy p;
        for (int i = 0; i < 1000; ++i, ++cases)
            assert(p.onNewGeneration(false, true, uint64_t(i) * MIN, &at) == SchedGenAction::Resume);
        assert(p.faults == 0);
    }
    {
        // A crash now and then (not the scheduler's) never stops it: one an hour for a day.
        SchedFaultPolicy p;
        for (int h = 1; h <= 24; ++h, ++cases)
            assert(p.onNewGeneration(true, true, uint64_t(h) * 60 * MIN, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.pauses == 0 && p.faults == 24);
    }
    {
        // A crash loop: three in a few minutes pause it 10 min; one more right after pauses it 30 min; after that the
        // old faults have aged out, and three more stop it for the boot. 7 crashes in all.
        SchedFaultPolicy p;
        uint64_t t = 5 * MIN;
        assert(p.onNewGeneration(true, true, t, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.onNewGeneration(false, true, t + MIN / 2, &at) == SchedGenAction::Resume);   // a join between
        assert(p.onNewGeneration(true, true, t + MIN, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.onNewGeneration(true, true, t + 2 * MIN, &at) == SchedGenAction::Pause && at == t + 12 * MIN);
        t = at;
        assert(p.onNewGeneration(true, true, t + MIN, &at) == SchedGenAction::Pause && at == t + 31 * MIN);
        t = at;
        assert(p.onNewGeneration(true, true, t + MIN, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.onNewGeneration(true, true, t + 2 * MIN, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.onNewGeneration(true, true, t + 3 * MIN, &at) == SchedGenAction::Stop);
        assert(p.faults == 7 && p.pauses == 2);
        cases += 9;
    }
    {
        // Faults spread wider than the window: three in 40 min resume each time.
        SchedFaultPolicy p;
        assert(p.onNewGeneration(true, true, 0 * MIN + 1, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.onNewGeneration(true, true, 20 * MIN, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.onNewGeneration(true, true, 40 * MIN, &at) == SchedGenAction::ResumeAfterFault);
        assert(p.inWindow(40 * MIN) == 2);
        cases += 4;
    }
    {
        // Without -itlschedsafe the first fault stops it; a join still doesn't.
        SchedFaultPolicy p;
        assert(p.onNewGeneration(false, false, MIN, &at) == SchedGenAction::Resume);
        assert(p.onNewGeneration(true, false, 2 * MIN, &at) == SchedGenAction::Stop);
        cases += 2;
    }
    printf("awdl_sched_restart: %u cases passed\n", cases);
    return 0;
}
