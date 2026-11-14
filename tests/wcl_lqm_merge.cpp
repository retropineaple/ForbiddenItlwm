#include "../ForbiddenItlwm/WCL/WCL.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    int32_t reported = 100;
    unsigned calls = 0;
    auto off = [&](int32_t quality) { assert(quality == -2); ++calls; };
    AirportWCL::resetLinkQuality(reported, off);
    assert(reported == 0 && calls == 1);
    AirportWCL::resetLinkQuality(reported, off);
    assert(calls == 1);
    reported = 100;
    AirportWCL::resetLinkQuality(reported, off);
    assert(reported == 0 && calls == 2);

    AirportWCL::LqmBeaconUpdate update {};
    assert(AirportWCL::makeBeaconUpdate(3, update));
    AirportWCL::setBeaconRadio(update, -60, -95);
    assert(update.noiseValid && update.noise == -95);
    assert(update.snrValid && update.snr == 35);
    assert(update.receivedDelta == 3 && update.scheduledDelta == 0);
    const auto *bytes = reinterpret_cast<const uint8_t *>(&update);
    assert(bytes[8] == 0 && bytes[9] == 0 && bytes[10] == 0);
    assert(bytes[0x12] == 0 && bytes[0x13] == 0); // no CCA
    for (int noise : {0, 5}) {
        update = {};
        AirportWCL::setBeaconRadio(update, -60, noise);
        assert(!update.noiseValid && !update.snrValid);
    }
    update = {};
    AirportWCL::setBeaconRadio(update, 0, -95);
    assert(!update.noiseValid && !update.snrValid);
    puts("WCL stop reset and measured SNR/noise: passed");
}
