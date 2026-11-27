// Host checks for the scheduler's restart rule (AWDLSchedRestart.hpp). No firmware or radio.
#include <AWDLSchedRestart.hpp>
#include <cassert>
#include <cstdio>
using namespace ItlAWDL;

static constexpr uint32_t kMax = 3;   // kAwdlSchedMaxRestarts

int main() {
    unsigned cases = 0;
    // Only 2, 6, 10 and 12 are not faults.
    for (unsigned site = 0; site < 16; ++site, ++cases)
        assert(isFaultRestartSite(site) == !(site == 2 || site == 6 || site == 10 || site == 12));
    {
        // 2026-11-27: two joins, then a crash at the third join, then a join. The old rule stopped at the crash
        // (third generation change); now the joins are free and the crash is the first fault.
        uint32_t restarts = 0;
        assert(schedOnNewGeneration(false, true, restarts, kMax) == SchedGenAction::Resume);
        assert(schedOnNewGeneration(false, true, restarts, kMax) == SchedGenAction::Resume);
        assert(schedOnNewGeneration(true, true, restarts, kMax) == SchedGenAction::ResumeAfterFault);
        assert(schedOnNewGeneration(false, true, restarts, kMax) == SchedGenAction::Resume);
        assert(restarts == 1);
        cases += 5;
    }
    {
        // Any number of joins, toggles and wakes never stop it.
        uint32_t restarts = 0;
        for (int i = 0; i < 1000; ++i, ++cases)
            assert(schedOnNewGeneration(false, true, restarts, kMax) == SchedGenAction::Resume);
        assert(restarts == 0);
    }
    {
        // A crash loop still stops it at the third fault, with joins in between.
        uint32_t restarts = 0;
        assert(schedOnNewGeneration(true, true, restarts, kMax) == SchedGenAction::ResumeAfterFault);
        assert(schedOnNewGeneration(false, true, restarts, kMax) == SchedGenAction::Resume);
        assert(schedOnNewGeneration(true, true, restarts, kMax) == SchedGenAction::ResumeAfterFault);
        assert(schedOnNewGeneration(true, true, restarts, kMax) == SchedGenAction::Stop);
        cases += 4;
    }
    {
        // Without -itlschedsafe the first fault stops it; a join still doesn't.
        uint32_t restarts = 0;
        assert(schedOnNewGeneration(false, false, restarts, kMax) == SchedGenAction::Resume);
        assert(schedOnNewGeneration(true, false, restarts, kMax) == SchedGenAction::Stop);
        assert(restarts == 0);
        cases += 3;
    }
    printf("awdl_sched_restart: %u cases passed\n", cases);
    return 0;
}
