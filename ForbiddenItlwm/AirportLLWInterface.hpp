// llw0, the low latency companion of awdl0 (macOS Sequoia only, experimental, behind -itlawdlllw).
//
// Continuity Camera and Sidecar open their media connections with a "low latency" requirement, and macOS serves
// that from a second interface next to awdl0. wifip2pd asks for it with request 293
// (APPLE80211_IOC_COMPANION_SKYWALK_IF_CREATE) on awdl0 right after awdl0 comes up.
//
// Apple builds it on the Wi-Fi interface's class chain (IO80211InfraProtocol, as AppleBCMWLANLowLatencyInterface),
// not on the virtual interface chain awdl0 uses: for role 2 the family's start takes the peer manager from the
// parent interface, and createPeerManager returns nothing for that role. The sequence here follows Apple's
// (read from 24H232's AppleBCMWLANCoreMac and IO80211Family):
//   role 2, interface id 9, low latency enabled, parent = awdl0, awdl0's companion = this, the companion
//   interface id, awdl0's address, the family's start, then an Ethernet registration with prefix "llw" and flag 4.
//
// The radio has no low latency mode. Frames llw0 sends go into awdl0's transmit ring, to the same peers.
// What arrives for llw0 is decided by a rule on the received frame (itlawdlllwrx), because on Apple's hardware
// that choice is made inside the firmware.
#pragma once
#include <ItlAWDLTargets.hpp>
#if defined(ITL_AWDL_LLW)
#include <Airport/Apple80211.h>
#include <Airport/IOSkywalkQueueFactories.h>
#include <IOKit/IOWorkLoop.h>
#include <AWDLDataFrame.hpp>

class ItlAWDLInterface;

class ItlLLWInterface : public IO80211InfraProtocol {
    OSDeclareDefaultStructors(ItlLLWInterface)
public:
    // llw0 carries data only: every request handler of the chain answers "unsupported".
#include "AirportLLWHandlers.inc"

    bool init() override;
    void free() override;
    IOReturn setMacAddress(ether_addr &) override { return kIOReturnUnsupported; }
    const char *getBSDNamePrefix() override { return "llw"; }
    SInt32 setInterfaceEnable(bool) override;
    // Two traps found bringing this up (24H232, confirmed by disassembly):
    // - IO80211InfraInterface::setPowerState runs a block on the interface work loop when the caller isn't on it, and
    //   the request that creates this interface is already holding that loop, so power management waits for the
    //   whole request and the BSD name never appears. The Skywalk part is all this interface needs; the rest is
    //   the Wi-Fi interface's system state report to the firmware.
    // - The ifnet's unique id is the 6-byte MAC, and this interface has awdl0's MAC, so dlil refuses it (EBUSY)
    //   and the BSD client gives up after 50 tries. Apple's own overrides this with an 8-byte id.
    IOReturn setPowerState(unsigned long state, IOService *device) override;
    SInt32 initBSDInterfaceParameters(ifnet_init_eparams *params, sockaddr_dl **address) override;
    // Apple's override refuses the driver-specific ioctls (SIOCSDRVSPEC and SIOCGDRVSPEC, the Apple80211 request path)
    // with EINVAL and leaves the rest to the family.
    IOReturn processBSDCommand(struct __ifnet *ifp, unsigned int command, void *data) override;
    // MTU 1500, as on Apple's own Broadcom hardware: IO80211VirtualInterface::peerToPeerConfigureIfnet (24H232,
    // 0xffffff80022a6cfc, reached from configureIfnet at 0xffffff80022a6df8 for every interface role but 6, 8, 9 and 10)
    // sets 1500 when the controller is AppleBCMWLANCore and 1484 for any other; see include/AWDLMtu.hpp. awdl0 carries
    // 1497-byte frames without a refusal at 1500 (14294 of them traced), and llw0's frames go through the same ring.
    // itlawdlllwmtu=1280..1500 chooses another value.
    IOReturn prepareBSDInterface(struct __ifnet *ifp, unsigned int flags) override;
    // Apple's driver answers these from its own queues and pools; the family's defaults return nothing.
    void *getRxCompQueue() override { return packetQueues[3]; }
    void *getTxCompQueue() override { return packetQueues[1]; }
    void *getTxPacketPool() override { return txPool; }
    void *getRxPacketPool() override { return rxPool; }
    int getNumTxQueues() override { return 1; }
    int getTxHeadroom() override { return headroom; }

    // Under the shared controller gate. Configures, starts and registers the interface. On failure everything it
    // did is undone and the object only needs releasing.
    IOReturn bringUp(IO80211Controller *controller, IOWorkLoop *workLoop, ItlAWDLInterface *parent,
                     const ether_addr &address, int receiveRule);
    // Under the shared gate, or after the work loop has stopped. Safe to call twice.
    void tearDown();
    // The name macOS gave the interface, "llw0" unless something else took unit 0.
    const char *bsdName();
    bool isUp() const { return registered; }

    // Under the shared gate. Whether a received AWDL data frame belongs on llw0 rather than awdl0.
    bool wants(const ItlAWDL::DataFrameView &view);
    // Under the shared gate. Hands the frame to macOS on llw0. False when it could not be delivered.
    bool deliver(const ItlAWDL::DataFrameView &view);

    // itlawdlllwrx: 0 delivers nothing to llw0 (counts only), 1 delivers frames whose IPv6 destination is a
    // unique local address (fd00::/8).
    enum { ReceiveNone = 0, ReceiveUniqueLocal = 1 };

private:
    static constexpr UInt16 headroom = 0x40;
    static UInt32 transmitAction(OSObject *, IOSkywalkTxSubmissionQueue *, IOSkywalkPacket *const *, UInt32, void *);
    static UInt32 receiveSupplyAction(OSObject *, IOSkywalkRxSubmissionQueue *, IOSkywalkPacket *const *, UInt32, void *);
    UInt32 transmit(IOSkywalkPacket *const *, UInt32);
    void noteDestination(const uint8_t *ipv6, size_t length, bool transmitted);
    void publish(bool force);
    void stage(const char *name);

    IO80211Controller *controller = nullptr;   // not retained: it outlives every interface it owns
    ItlAWDLInterface *parent = nullptr;        // not retained: the parent owns this object
    IOWorkLoop *workLoop = nullptr;
    IOSkywalkPacketBufferPool *txPool = nullptr;
    IOSkywalkPacketBufferPool *rxPool = nullptr;
    IOEventSource *packetQueues[4] = {};
    UInt32 addedQueues = 0;
    bool attached = false, started = false, registered = false, companionSet = false;
    int receiveRule = ReceiveNone;
    // Counters. Destination classes are of the IPv6 destination: link-local, unique local, multicast, other.
    unsigned enableCalls = 0, disableCalls = 0;
    unsigned txCallbacks = 0, txOffered = 0, txForwarded = 0, txDropped = 0, txCompleteFailed = 0;
    unsigned txClass[4] = {}, rxClass[4] = {};
    unsigned rxWanted = 0, rxDelivered = 0, rxFailed = 0, rxSupplyCallbacks = 0;
    unsigned lastPublished = 0;
    uint32_t mtu = 1500;
    uint8_t uniqueId[8] = {};   // MAC and "ll": the ifnet's unique id, which has to outlive the ifnet
};
#endif
