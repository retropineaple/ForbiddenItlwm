// awdl0's MTU. IO80211VirtualInterface::peerToPeerConfigureIfnet (IO80211Family 24H232, 0xffffff80022a6cfc) sets it with
//   safeMetaCast(controller, "AppleBCMWLANCore") ? ifnet_set_mtu(1500) : ifnet_set_mtu(1484)
// (mov esi,0x5dc / mov esi,0x5cc at 0xffffff80022a6dc5 / ...6dcc). On Apple's own Broadcom hardware awdl0 is therefore 1500,
// the same as a wired interface; every other controller is given 1484, and AirDrop's QUIC, sized for a 1500-byte wired
// path, is then refused on awdl0 with EMSGSIZE ("Message too long", unexpected-40). The driver sets 1500 itself after the
// family has run, as Apple's own does. itlawdlmtu=N (1280 to 1500) chooses another value, 1484 being the family's.
#pragma once
#include <stdint.h>
namespace ItlAWDL {
constexpr uint32_t kAwdlMtuApple = 1500;
constexpr uint32_t kAwdlMtuFamilyDefault = 1484;
constexpr uint32_t kAwdlMtuMin = 1280;
// The MTU to use: the boot-arg's value when it is in range, else Apple's.
inline uint32_t awdlMtu(int requested) {
    return requested >= int(kAwdlMtuMin) && requested <= int(kAwdlMtuApple) ? uint32_t(requested) : kAwdlMtuApple;
}
}
