// Host checks for the listener move waits (AWDLListenerWidth.hpp): same-channel widening gets a short settle and
// a short gap in the data; moves between channels keep the 60 s / 30 s waits (#68).
#include <AWDLListenerWidth.hpp>
#include <cassert>
#include <cstdio>
using namespace ItlAWDL;

int main() {
    unsigned cases = 0;
    const uint64_t S = 60000000, Q = 30000000, now = 1000000000;
    // A move between channels: 60 s stable and 30 s without data, as before.
    assert(moveWait(false, now, now - 10000000, 0, S, Q) == MoveWait::Settle);
    assert(moveWait(false, now, now - S, now - 5000000, S, Q) == MoveWait::DataQuiet);
    assert(moveWait(false, now, now - S, now - Q, S, Q) == MoveWait::None);
    cases += 3;
    // Widening 44/20 -> 44/80 with AirDrop data every few seconds: the old rule never let it through.
    assert(moveWait(false, now, now - 600000000, now - 3000000, S, Q) == MoveWait::DataQuiet);
    // The widening rule: 3 s stable, then the first 2 s gap.
    assert(moveWait(true, now, now - 1000000, 0, S, Q) == MoveWait::WidenSettle);
    assert(moveWait(true, now, now - kWidenSettleUS, now - 500000, S, Q) == MoveWait::WidenQuiet);
    assert(moveWait(true, now, now - kWidenSettleUS, now - kWidenQuietUS, S, Q) == MoveWait::None);
    assert(moveWait(true, now, now - kWidenSettleUS, 0, S, Q) == MoveWait::None);
    cases += 5;
    assert(moveWaitName(MoveWait::None)[0] == 'n' && moveWaitName(MoveWait::WidenQuiet)[0] == 'w');
    ++cases;
    printf("awdl_listener_width: %u cases passed\n", cases);
    return 0;
}
