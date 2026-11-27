// What the AirDrop scheduler (itlawdlsched) does when the firmware comes back in a new generation. Every firmware
// start gives a new generation: a join, a Wi-Fi toggle and a wake restart the firmware just as a crash recovery does.
// Only a restart after a fault counts toward the limit; the others resume without using it up. Before this, a boot
// with two joins and one crash had its scheduler stopped until the next reboot.
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

enum class SchedGenAction { Resume, ResumeAfterFault, Stop };

// fault: a fault restart happened since the follower took its previous generation. safe: -itlschedsafe (on with
// itlawdlsched=1/3). Without it, the first fault stops the follower, as in the first research builds.
inline SchedGenAction schedOnNewGeneration(bool fault, bool safe, uint32_t &restarts, uint32_t maxRestarts) {
    if (!fault)
        return SchedGenAction::Resume;
    if (safe && ++restarts < maxRestarts)
        return SchedGenAction::ResumeAfterFault;
    return SchedGenAction::Stop;
}

}  // namespace ItlAWDL
