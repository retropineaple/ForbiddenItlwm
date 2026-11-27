// What the AirDrop scheduler (itlawdlsched) does when the firmware comes back in a new generation. Every firmware
// start gives a new generation: a join, a Wi-Fi toggle and a wake restart the firmware just as a crash recovery does.
// Only a restart after a fault counts; the others resume and use up nothing.
//
// Faults: up to kSchedFaultsInWindow in kSchedFaultWindowUS, the scheduler resumes at once. A fault that isn't the
// scheduler's (one now and then) never stops it. At the limit (a crash loop, which the scheduler may be causing) it
// pauses, AirDrop runs without it, and it tries again after kSchedPauseUS[0], then kSchedPauseUS[1]; the fault after
// that keeps it off until the next boot. The faults stay in the window across a pause, so after the short pause a
// single fault pauses again; after the long one the old faults have aged out. A steady crash loop costs at most
// 3 + 1 + 3 = 7 crashes in a boot before the scheduler stays off.
#pragma once
#include <stdint.h>

namespace ItlAWDL {

// Restart sites in ItlIwx.cpp (itlFwNoteRestart). Not faults: 2, a planned listener channel move; 6 and 10, the radio
// switch toggled; 12, the interface coming up again (every join, Wi-Fi toggle and wake). Everything else follows a
// fault: 1 a TX stall, 3 a failed state change, 4 a command timeout, 5 critical temperature, 7 and 9 a fatal firmware
// error, 8 and 11 a hardware error.
constexpr bool isFaultRestartSite(unsigned site) {
    return site != 2 && site != 6 && site != 10 && site != 12;
}

constexpr unsigned kSchedFaultsInWindow = 3;
constexpr uint64_t kSchedFaultWindowUS = 30ULL * 60 * 1000000;
constexpr unsigned kSchedPauses = 2;
constexpr uint64_t kSchedPauseUS[kSchedPauses] = {10ULL * 60 * 1000000, 30ULL * 60 * 1000000};

enum class SchedGenAction { Resume, ResumeAfterFault, Pause, Stop };

struct SchedFaultPolicy {
    uint64_t faultUS[kSchedFaultsInWindow] = {};   // the last faults' times (0 = none)
    unsigned next = 0;
    unsigned pauses = 0;     // pauses so far this boot
    uint32_t faults = 0;     // faults this boot

    unsigned inWindow(uint64_t now) const {
        unsigned n = 0;
        for (uint64_t t : faultUS)
            n += t && now >= t && now - t < kSchedFaultWindowUS;
        return n;
    }

    // fault: a fault restart happened since the scheduler took its previous generation. safe: -itlschedsafe (on with
    // the scheduler). Without it, the first fault stops it, as in the first research builds. On Pause, *resumeAtUS
    // gets when to try again.
    SchedGenAction onNewGeneration(bool fault, bool safe, uint64_t now, uint64_t *resumeAtUS) {
        if (!fault)
            return SchedGenAction::Resume;
        ++faults;
        if (!safe)
            return SchedGenAction::Stop;
        faultUS[next] = now ? now : 1;
        next = (next + 1) % kSchedFaultsInWindow;
        if (inWindow(now) < kSchedFaultsInWindow)
            return SchedGenAction::ResumeAfterFault;
        if (pauses >= kSchedPauses)
            return SchedGenAction::Stop;
        if (resumeAtUS)
            *resumeAtUS = now + kSchedPauseUS[pauses];
        ++pauses;
        return SchedGenAction::Pause;
    }
};

}  // namespace ItlAWDL
