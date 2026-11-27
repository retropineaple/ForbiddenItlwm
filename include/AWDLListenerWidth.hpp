// The listener's channel moves wait for a stable choice and a quiet link (#68): a choice that held for 60 s and no
// AWDL data for 30 s, since a move drops every peer's Block Ack. Widening on the same channel (20 -> 80 MHz, for
// example 44/20 once the scheduler runs beside a router on 36/80) isn't a roam between channels: nearby devices'
// changing channel sequences don't flip it, so it needs only a short settle and a short gap in the data. With the
// long waits, steady AirDrop traffic kept a listener at 20 MHz for good (PR #88: 44/20 on alpha.4, 44/80 on the
// fork). The drain, peer removal and command checks after the wait are the same for both.
#pragma once
#include <stdint.h>

namespace ItlAWDL {

constexpr uint64_t kWidenSettleUS = 3000000;   // the 80 MHz choice held this long (overlap readiness can flicker)
constexpr uint64_t kWidenQuietUS = 2000000;    // no AWDL data for this long: between sends, not during one

enum class MoveWait : uint8_t { None, Settle, DataQuiet, WidenSettle, WidenQuiet };

inline const char *moveWaitName(MoveWait w)
{
    switch (w) {
    case MoveWait::Settle: return "choice settling";
    case MoveWait::DataQuiet: return "waiting for 30 s without data";
    case MoveWait::WidenSettle: return "widening: choice settling";
    case MoveWait::WidenQuiet: return "widening: waiting for a gap in the data";
    default: return "none";
    }
}

// Whether a move to a new choice waits now. widen: same channel, from 20 to 80 MHz. sinceUS: when this choice
// was first made; lastDataUS: the last AWDL data sent (0 = none).
inline MoveWait moveWait(bool widen, uint64_t now, uint64_t sinceUS, uint64_t lastDataUS, uint64_t settleUS,
                         uint64_t quietUS)
{
    const uint64_t needQuiet = widen ? kWidenQuietUS : quietUS;
    const uint64_t needStable = widen ? kWidenSettleUS : settleUS;
    if (lastDataUS && now - lastDataUS < needQuiet)
        return widen ? MoveWait::WidenQuiet : MoveWait::DataQuiet;
    if (now - sinceUS < needStable)
        return widen ? MoveWait::WidenSettle : MoveWait::Settle;
    return MoveWait::None;
}

}  // namespace ItlAWDL
