// llw0, the low latency companion of awdl0. See AirportLLWInterface.hpp for what it is and where the sequence
// comes from.
#include "AirportItlwmV2.hpp"
#include "AirportLLWInterface.hpp"

#if defined(ITL_AWDL_LLW)
#include "AirportAWDLInterface.hpp"
#include <Airport/IOSkywalkPacketView.h>

OSDefineMetaClassAndStructors(ItlLLWInterface, IO80211InfraProtocol)

// Family functions the reconstructed headers don't declare. Each is an exported symbol of IO80211Family on
// 24H232 (checked with tools/abi/kc_carve.py and nm), and each is called the way Apple's driver calls it.
extern "C" void itlLLWSetLowLatencyEnabled(IO80211SkywalkInterface *, bool)
    __asm__("__ZN23IO80211SkywalkInterface20setLowLatencyEnabledEb");
extern "C" void itlLLWSetParentInterface(IO80211SkywalkInterface *, IO80211VirtualInterface *)
    __asm__("__ZN23IO80211SkywalkInterface18setParentInterfaceEP23IO80211VirtualInterface");
extern "C" void itlLLWSetCompanion(IO80211VirtualInterface *, IO80211SkywalkInterface *)
    __asm__("__ZN23IO80211VirtualInterface38setCompanionLowLatencySkywalkInterfaceEP23IO80211SkywalkInterface");
extern "C" void itlLLWSetCompanionInterfaceId(IO80211SkywalkInterface *, unsigned int)
    __asm__("__ZN23IO80211SkywalkInterface23setCompanionInterfaceIdEj");
extern "C" unsigned int itlLLWGetInterfaceId(IO80211SkywalkInterface *)
    __asm__("__ZN23IO80211SkywalkInterface14getInterfaceIdEv");
extern "C" void itlLLWSetInitMacAddress(IO80211SkywalkInterface *, ether_addr *)
    __asm__("__ZN23IO80211SkywalkInterface17setInitMacAddressER10ether_addr");
extern "C" const char *itlLLWGetBSDName(IO80211SkywalkInterface *)
    __asm__("__ZN23IO80211SkywalkInterface10getBSDNameEv");

// Apple's values for this interface (AppleBCMWLANProximityInterface::setCOMPANION_SKYWALK_IF_CREATE, 24H232).
static constexpr unsigned int kLowLatencyRole = 2;
static constexpr unsigned int kLowLatencyInterfaceId = 9;

bool ItlLLWInterface::init()
{
    if (!IO80211InfraProtocol::init())
        return false;
    itlLLWSetLowLatencyEnabled(this, true);
    return true;
}

void ItlLLWInterface::free()
{
    tearDown();
    IO80211InfraProtocol::free();
}

void ItlLLWInterface::stage(const char *name)
{
    setProperty("ItlwmLLWStage", name);
    if (controller)
        controller->setProperty("ItlwmLLWStage", name);
}

const char *ItlLLWInterface::bsdName()
{
    const char *name = itlLLWGetBSDName(this);
    return (name && name[0]) ? name : "llw0";
}

IOReturn ItlLLWInterface::bringUp(IO80211Controller *owner, IOWorkLoop *loop, ItlAWDLInterface *parentInterface,
                                  const ether_addr &address, int rule)
{
    if (!owner || !loop || !parentInterface || started || attached)
        return kIOReturnBadArgument;
    controller = owner;
    parent = parentInterface;
    receiveRule = rule;
    int requestedMtu = 0;
    if (PE_parse_boot_argn("itlawdlllwmtu", &requestedMtu, sizeof(requestedMtu)) && requestedMtu >= 1280 && requestedMtu <= 1500)
        mtu = uint32_t(requestedMtu);
    workLoop = loop;
    workLoop->retain();
    auto fail = [this](const char *where, IOReturn result) {
        stage(where);
        setProperty("ItlwmLLWFailure", result, 32);
        if (controller)
            controller->setProperty("ItlwmLLWFailure", result, 32);
        tearDown();
        return result;
    };

    stage("configuring");
    setInterfaceRole(kLowLatencyRole);
    setInterfaceId(kLowLatencyInterfaceId);
    itlLLWSetLowLatencyEnabled(this, true);
    // Before start: for role 2 the family's start takes the peer manager from the parent, and fails without one.
    itlLLWSetParentInterface(this, parent);
    itlLLWSetCompanion(parent, this);
    companionSet = true;
    itlLLWSetCompanionInterfaceId(this, itlLLWGetInterfaceId(parent));

    stage("attaching");
    if (!attach(controller))
        return fail("attach-failed", kIOReturnError);
    attached = true;
    if (!controller->attachInterface(this, controller))
        return fail("controller-attach-failed", kIOReturnError);

    ether_addr mac = address;
    itlLLWSetInitMacAddress(this, &mac);

    stage("starting");
    if (!IO80211InfraProtocol::start(controller))
        return fail("family-start-failed", kIOReturnError);
    started = true;

    stage("allocating");
    // A packet pool, as awdl0 uses on Sequoia (a quantum pool is refused for a network interface).
    const IOSkywalkPacketBufferPool::PoolOptions txOptions = {1024, 1024, 2048, 1, 4096, 0, 0};
    const IOSkywalkPacketBufferPool::PoolOptions rxOptions = {512, 512, 2048, 1, 4096, 0, 0};
    txPool = IOSkywalkPacketBufferPool::withName("itlwm-llw-tx", this, 1, &txOptions);
    rxPool = IOSkywalkPacketBufferPool::withName("itlwm-llw-rx", this, 1, &rxOptions);
    if (!txPool || !rxPool)
        return fail("pool-failed", kIOReturnNoMemory);
    packetQueues[0] = itlMakeTxSubmission(txPool, 64, 0, this, transmitAction, nullptr, 0);
    packetQueues[1] = itlMakeTxCompletion(txPool, 64, 0, this, nullptr, nullptr, 0);
    packetQueues[2] = itlMakeRxSubmission(rxPool, 64, 1, 0, this, receiveSupplyAction, nullptr, 0);
    packetQueues[3] = itlMakeRxCompletion(rxPool, 64, 0, this, nullptr, nullptr, 0);
    for (auto queue : packetQueues)
        if (!queue)
            return fail("queue-failed", kIOReturnNoMemory);
    for (auto queue : packetQueues) {
        const IOReturn result = workLoop->addEventSource(queue);
        if (result != kIOReturnSuccess)
            return fail("queue-add-failed", result);
        ++addedQueues;
    }

    IOSkywalkEthernetInterface::RegistrationInfo info = {};
    if (!initRegistrationInfo(&info, 1, sizeof(info)))
        return fail("registration-info-failed", kIOReturnBadArgument);
    // The fields Apple's createSkywalkInterface sets for the low latency role (24H232): the same as awdl0's
    // except for the name prefix.
    auto put32 = [&info](size_t offset, UInt32 value) { memcpy(info.bytes + offset, &value, sizeof(value)); };
    auto get32 = [&info](size_t offset) { UInt32 value; memcpy(&value, info.bytes + offset, sizeof(value)); return value; };
    put32(0x0c, 3);
    put32(0x14, 2);
    put32(0x24, 0x80);
    put32(0x28, 0x80);
    const char *prefix = "llw";
    memcpy(info.bytes + 0x30, &prefix, sizeof(prefix));
    put32(0x38, 0);
    memcpy(info.bytes + 0x3c, &headroom, sizeof(headroom));
    put32(0x40, 4);
    // Apple takes the headroom off the two frame size defaults rather than setting them.
    setProperty("ItlwmLLWDefaultFrameSizes", (UInt64(get32(0x48)) << 32) | get32(0x4c), 64);
    const size_t frameSizeOffsets[] = {0x48, 0x4c};
    for (size_t offset : frameSizeOffsets)
        if (get32(offset) > headroom)
            put32(offset, get32(offset) - headroom);
    memcpy(uniqueId, &mac, 6);
    uniqueId[6] = 'l';
    uniqueId[7] = 'l';
    memcpy(info.bytes + 0x108, &mac, sizeof(mac));
    IOSkywalkPacketQueue *queues[4];
    for (UInt32 i = 0; i < 4; ++i)
        queues[i] = reinterpret_cast<IOSkywalkPacketQueue *>(packetQueues[i]);

    stage("registering");
    const IOReturn result = registerEthernetInterface(&info, queues, 4, txPool, rxPool, 0);
    if (result != kIOReturnSuccess)
        return fail("registration-failed", result);
    registered = true;
    for (auto queue : packetQueues)
        queue->enable();
    registerService(kIOServiceAsynchronous);
    setRunningState(false);
    setProperty("ItlwmLLWReceiveRule", UInt64(receiveRule), 32);
    setProperty("ItlwmLLWBSDName", bsdName());
    stage("registered");
    publish(true);
    return kIOReturnSuccess;
}

void ItlLLWInterface::tearDown()
{
    if (registered) {
        registered = false;
        deregisterEthernetInterface(0);
    }
    for (UInt32 i = addedQueues; i; --i) {
        packetQueues[i - 1]->disable();
        if (workLoop)
            workLoop->removeEventSource(packetQueues[i - 1]);
    }
    addedQueues = 0;
    for (auto &queue : packetQueues)
        OSSafeReleaseNULL(queue);
    OSSafeReleaseNULL(txPool);
    OSSafeReleaseNULL(rxPool);
    if (companionSet) {
        companionSet = false;
        if (parent)
            itlLLWSetCompanion(parent, nullptr);
    }
    if (started) {
        started = false;
        IO80211InfraProtocol::stop(controller);
    }
    if (attached) {
        attached = false;
        controller->detachInterface(this, false);
        detach(controller);
    }
    OSSafeReleaseNULL(workLoop);
    parent = nullptr;
}

IOReturn ItlLLWInterface::prepareBSDInterface(struct __ifnet *ifp, unsigned int flags)
{
    const IOReturn result = IO80211InfraProtocol::prepareBSDInterface(ifp, flags);
    if (result == kIOReturnSuccess && ifp) {
        const errno_t error = ifnet_set_mtu(reinterpret_cast<ifnet_t>(ifp), mtu);
        setProperty("ItlwmLLWMtu", mtu, 32);
        setProperty("ItlwmLLWMtuResult", error, 32);
    }
    return result;
}

IOReturn ItlLLWInterface::processBSDCommand(struct __ifnet *ifp, unsigned int command, void *data)
{
    // _IOW('i', 200, ifreq), _IOW('i', 200, ifdrv), _IOWR('i', 201, ifreq), _IOWR('i', 201, ifdrv) and _IOWR('i', 200, ifreq):
    // the values Apple's low latency interface compares against (24H232).
    if (command == 0x802069c8 || command == 0x802869c8 || command == 0xc02069c8 || command == 0xc02069c9 ||
        command == 0xc02869c9)
        return EINVAL;
    return IO80211InfraProtocol::processBSDCommand(ifp, command, data);
}

IOReturn ItlLLWInterface::setPowerState(unsigned long state, IOService *device)
{
    // Skip IO80211InfraInterface's part: see the declaration.
    return IO80211SkywalkInterface::setPowerState(state, device);
}

SInt32 ItlLLWInterface::initBSDInterfaceParameters(ifnet_init_eparams *params, sockaddr_dl **address)
{
    const SInt32 result = IO80211InfraProtocol::initBSDInterfaceParameters(params, address);
    if (result == 0 && params) {
        // ifnet_init_eparams: the unique id pointer at +0x10 and its length at +0x18 (as Apple's override writes them).
        uint8_t *raw = reinterpret_cast<uint8_t *>(params);
        const uint8_t *id = uniqueId;
        const uint32_t length = sizeof(uniqueId);
        memcpy(raw + 0x10, &id, sizeof(id));
        memcpy(raw + 0x18, &length, sizeof(length));
    }
    return result;
}

// As Apple's low latency interface: the family's part, then report the link and bring it up when enabling.
SInt32 ItlLLWInterface::setInterfaceEnable(bool enable)
{
    IO80211InfraProtocol::setInterfaceEnable(enable);
    if (enable) {
        ++enableCalls;
        reportLinkStatus(3, 0x80);
        setLinkState(kIO80211NetworkLinkUp, 1, false, 0);
    } else {
        ++disableCalls;
    }
    publish(true);
    return 0;
}

// 0 link-local, 1 unique local, 2 multicast, 3 anything else, by IPv6 destination.
static unsigned destinationClass(const uint8_t *ipv6, size_t length)
{
    if (!ipv6 || length < 40)
        return 3;
    const uint8_t *destination = ipv6 + 24;
    if (destination[0] == 0xff)
        return 2;
    if (destination[0] == 0xfe && (destination[1] & 0xc0) == 0x80)
        return 0;
    if ((destination[0] & 0xfe) == 0xfc)
        return 1;
    return 3;
}

void ItlLLWInterface::noteDestination(const uint8_t *ipv6, size_t length, bool transmitted)
{
    ++(transmitted ? txClass : rxClass)[destinationClass(ipv6, length)];
}

void ItlLLWInterface::publish(bool force)
{
    const unsigned events = txCallbacks + rxDelivered + rxFailed + rxWanted;
    if (!force && events - lastPublished < 64)
        return;
    lastPublished = events;
    setProperty("ItlwmLLWEnableCalls", enableCalls, 32);
    setProperty("ItlwmLLWDisableCalls", disableCalls, 32);
    setProperty("ItlwmLLWTxOffered", txOffered, 32);
    setProperty("ItlwmLLWTxForwarded", txForwarded, 32);
    setProperty("ItlwmLLWTxDropped", txDropped, 32);
    setProperty("ItlwmLLWTxCompleteFailed", txCompleteFailed, 32);
    setProperty("ItlwmLLWRxWanted", rxWanted, 32);
    setProperty("ItlwmLLWRxDelivered", rxDelivered, 32);
    setProperty("ItlwmLLWRxFailed", rxFailed, 32);
    char classes[96];
    snprintf(classes, sizeof(classes), "linklocal %u, uniquelocal %u, multicast %u, other %u",
             txClass[0], txClass[1], txClass[2], txClass[3]);
    setProperty("ItlwmLLWTxDestinations", classes);
    snprintf(classes, sizeof(classes), "linklocal %u, uniquelocal %u, multicast %u, other %u",
             rxClass[0], rxClass[1], rxClass[2], rxClass[3]);
    setProperty("ItlwmLLWRxDestinationsSeen", classes);
}

// Every frame llw0 sends goes into awdl0's ring, to be sent to the same peers on the same schedule. The packet
// is given back to macOS at once: the ring holds a copy.
UInt32 ItlLLWInterface::transmit(IOSkywalkPacket *const *packets, UInt32 count)
{
    ++txCallbacks;
    UInt32 taken = 0;
    while (taken < count) {
        const UInt32 batch = (count - taken) < 32 ? (count - taken) : 32;
        for (UInt32 i = 0; i < batch; ++i) {
            auto *packet = reinterpret_cast<ItlSkywalkPacketView *>(packets[taken + i]);
            const UInt32 length = packet ? packet->getDataLength() : 0;
            const auto *base = packet && packet->getPacketBufferCount() ?
                reinterpret_cast<const uint8_t *>(packet->getDataVirtualAddress()) : nullptr;
            ++txOffered;
            if (!base || length < 14) {
                ++txDropped;
                continue;
            }
            const uint8_t *frame = base + itlPacketGetDataOffset(packet);
            noteDestination(frame + 14, length - 14, true);
            if (parent && parent->queueCompanionTransmit(frame, length))
                ++txForwarded;
            else
                ++txDropped;
        }
        // Count must stay below the completion queue's capacity (64).
        const IOReturn result = itlTxComplete(packetQueues[1], reinterpret_cast<void *const *>(packets + taken), batch, 0);
        if (result != kIOReturnSuccess) {
            ++txCompleteFailed;
            setProperty("ItlwmLLWTxCompleteLastError", result, 32);
            break;
        }
        taken += batch;
    }
    publish(false);
    return taken;
}

UInt32 ItlLLWInterface::transmitAction(OSObject *owner, IOSkywalkTxSubmissionQueue *, IOSkywalkPacket *const *packets,
                                       UInt32 count, void *)
{
    auto *interface = OSDynamicCast(ItlLLWInterface, owner);
    if (!interface || !packets || !count || !interface->registered)
        return 0;
    return interface->transmit(packets, count);
}

UInt32 ItlLLWInterface::receiveSupplyAction(OSObject *owner, IOSkywalkRxSubmissionQueue *, IOSkywalkPacket *const *,
                                            UInt32, void *)
{
    if (auto *interface = OSDynamicCast(ItlLLWInterface, owner))
        ++interface->rxSupplyCallbacks;
    return 0;
}

bool ItlLLWInterface::wants(const ItlAWDL::DataFrameView &view)
{
    if (!registered)
        return false;
    noteDestination(view.ip.bytes, view.ip.length, false);
    const bool uniqueLocal = destinationClass(view.ip.bytes, view.ip.length) == 1;
    // Plain RFC 1042 frames are llw0's by definition (what Broadcom's host does); other frames follow the rule.
    const bool wanted = view.ip.rfc1042 || (receiveRule == ReceiveUniqueLocal && uniqueLocal);
    if (uniqueLocal || view.ip.rfc1042) {
        ++rxWanted;
        publish(false);
    }
    return wanted;
}

// The same steps awdl0's receive path takes, with this interface's pool and queues.
bool ItlLLWInterface::deliver(const ItlAWDL::DataFrameView &view)
{
    if (!registered || !rxPool || !workLoop || !workLoop->inGate())
        return false;
    auto *pool = reinterpret_cast<ItlSkywalkPoolView *>(rxPool);
    ItlSkywalkPacketView *packet = nullptr;
    bool delivered = false;
    if (pool->allocatePacket(1, &packet, 1) == kIOReturnSuccess && packet) {
        const size_t wanted = 14 + view.ip.length;
        if (packet->prepareWithQueue(packetQueues[2], 0, 0) == kIOReturnSuccess && packet->getPacketBufferCount() == 1 &&
            packet->setDataLength(unsigned(wanted)) == kIOReturnSuccess && packet->getDataLength() == wanted) {
            auto *bytes = reinterpret_cast<uint8_t *>(packet->getDataVirtualAddress());
            size_t written = 0;
            if (bytes && ItlAWDL::copyDataEthernet(view, bytes, wanted, &written) == ItlAWDL::BuildResult::Ready &&
                written == wanted && itlPacketSetHeadroom(packet, 0) == kIOReturnSuccess &&
                itlPacketSetLinkHeaderLength(packet, 14) == kIOReturnSuccess &&
                itlRxEnqueue(packetQueues[3], &packet, 1, 0) == kIOReturnSuccess) {
                // The queue owns the packet now.
                delivered = true;
            }
        }
        if (!delivered) {
            packet->setDataLength(0);
            packet->completeWithQueue(packetQueues[2], 0, 0);
            pool->deallocatePacket(packet);
        }
    }
    ++(delivered ? rxDelivered : rxFailed);
    publish(false);
    return delivered;
}
#endif
