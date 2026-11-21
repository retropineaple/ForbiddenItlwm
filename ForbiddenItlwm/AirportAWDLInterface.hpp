// Experimental native AWDL interface. No AWDL radio data path is implemented.
#pragma once
#include <Airport/IO80211AWDLProtocol.h>
#include <Airport/IOSkywalkQueueFactories.h>
#include "AWDLRequestedValue.hpp"
#include "AWDLInactiveProtocol.hpp"
#include "AWDLPeerCache.hpp"
#include <IOKit/IOLocks.h>

class ItlAWDLInterface : public IO80211AWDLProtocol {
    OSDeclareDefaultStructors(ItlAWDLInterface)
public:
    struct FrameConfiguration {
        apple80211_awdl_sync_params parameters;
        bool hasRequestedTiming;
        uint8_t address[6];
        size_t templateLength;
        uint8_t staticTLVs[ItlAWDLFrameTemplate::capacity];
    };
    // Caller retains this interface and holds the shared controller gate.
    // The copy owns every byte and does not activate the native transport.
    IOReturn copyFrameConfiguration(FrameConfiguration*);
    void observeDataFrame(const uint8_t*, size_t);
    void acceptNativeRequests();
    // Under the shared gate. Completes accepted packets and reports whether work remains.
    bool serviceTransmit();
    // Under the shared gate. Prepared TLVs for a master indication frame, if any.
    bool copyMasterIndication(uint8_t *, size_t, size_t *);
    bool hasMasterIndication() const { return masterIndicationLength != 0; }
    // Under the shared gate. Converts the oldest queued frame into a caller-owned buffer.
    bool popTransmit(uint8_t *, size_t, size_t *, const uint8_t *holdPayloadFor, bool holdEverySize = false);
    // Frames macOS has queued that the driver hasn't taken yet.
    unsigned queuedTransmit() const { return transmitQueued; }
    // Under the shared gate. Puts one Ethernet frame from the companion interface (llw0) into the same ring
    // awdl0's own frames wait in, so it goes to the same peers on the same schedule. False when it didn't fit.
    bool queueCompanionTransmit(const uint8_t *ethernet, size_t length);
    unsigned heldShortTransmit() const { return transmitHeldShort; }
    IOReturn initializeNetwork(IO80211Controller*, IOWorkLoop*, const ether_addr&);
    IOReturn receiveManagementFrame(IO80211Controller*, const uint8_t*, size_t, uint32_t, int32_t);
    void stop(IOService*) override;
    void free() override;
    // #13 diagnostics: who opens and closes this interface (the BSD client that owns awdl0,
    // the Wi-Fi daemons' user clients), and when the BSD interface is prepared and finalised.
    bool handleOpen(IOService*, IOOptionBits, void*) override;
    void handleClose(IOService*, IOOptionBits) override;
#if __IO80211_TARGET == __MAC_15_2
    IOReturn prepareBSDInterface(struct __ifnet*, unsigned int) override;
    void finalizeBSDInterface(struct __ifnet*, unsigned int) override;
#endif
    // Pure in IO80211VirtualInterface on Tahoe, and in IO80211SkywalkInterface on Sequoia.
#if __IO80211_TARGET != __MAC_14_4   // not a virtual on Sonoma
    IOReturn setMacAddress(ether_addr&) override { return unsupported("setMacAddress"); }
#endif
    IOReturn getSSID(apple80211_ssid_data*) override { return unsupported("getSSID"); }
    IOReturn getCHANNEL(apple80211_channel_data*) override { return unsupported("getCHANNEL"); }
    IOReturn getBSSID(apple80211_bssid_data*) override { return unsupported("getBSSID"); }
    IOReturn getSTATE(apple80211_state_data*) override { return unsupported("getSTATE"); }
    IOReturn getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *data) override { return readRequested(requested_AWDL_SYNC_PARAMS, data); }
    IOReturn getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *data) override;
    IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter *data) override { return readRequested(requested_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS, data); }
    IOReturn getAWDL_ELECTION_ID(apple80211_awdl_election_id *data) override { return readRequested(requested_AWDL_ELECTION_ID, data); }
    IOReturn getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *data) override { return readRequested(requested_AWDL_ELECTION_METRIC, data); }
    IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence *data) override { return readRequested(requested_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE, data); }
    IOReturn getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size*) override;
    IOReturn getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *data) override { return readRequested(requested_AWDL_MASTER_CHANNEL, data); }
    IOReturn getAWDL_SYNC_STATE(apple80211_awdl_sync_state *data) override { return nativeState.getState(data); }
    IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds *data) override { return readRequested(requested_AWDL_ELECTION_RSSI_THRESHOLDS, data); }
    IOReturn getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *data) override { return readRequested(requested_AWDL_PRESENCE_MODE, data); }
    IOReturn getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*) override { return unsupported("getAWDL_ELECTION_PARAMS"); }
    IOReturn getAWDL_DEVICE_CAPABILITIES(apple80211_awdl_device_capabilities*) override { return unsupported("getAWDL_DEVICE_CAPABILITIES"); }
    IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel *data) override { return readRequested(requested_AWDL_SECONDARY_MASTER_CHANNEL, data); }
    IOReturn getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) override;
    IOReturn getAWDL_MIN_RATE(apple80211_awdl_min_rate*) override { return unsupported("getAWDL_MIN_RATE"); }
    IOReturn getAWDL_ENCRYPTION_TYPE(apple80211_encrypt*) override { return unsupported("getAWDL_ENCRYPTION_TYPE"); }
    IOReturn getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*) override { return unsupported("getAWDL_TXCAL_PERIOD"); }
    IOReturn getAWDL_DEV_STATS(apple80211_awdl_dev_stats*) override { return unsupported("getAWDL_DEV_STATS"); }
    IOReturn getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*) override { return unsupported("getAWDL_DFSP_CONFIG"); }
    IOReturn getAWDL_CCA(apple80211_awdl_cca*) override { return unsupported("getAWDL_CCA"); }
    IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap *) override;
    IOReturn getLLW_PARAMS(apple80211_llw_params*) override { return unsupported("getLLW_PARAMS"); }
    IOReturn setSSID(apple80211_ssid_data*) override { return unsupported("setSSID"); }
    IOReturn setCHANNEL(apple80211_channel_data*) override { return unsupported("setCHANNEL"); }
    IOReturn setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *data) override { return storeRequested(requested_AWDL_SYNC_PARAMS, data, "AWDL_SYNC_PARAMS"); }
    IOReturn setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *data) override;
    IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter *data) override { return storeRequested(requested_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS, data, "AWDL_EXTENSION_STATE_MACHINE_PARAMETERS"); }
    IOReturn setAWDL_ELECTION_ID(apple80211_awdl_election_id *data) override { return storeRequested(requested_AWDL_ELECTION_ID, data, "AWDL_ELECTION_ID"); }
    IOReturn setAWDL_BSSID(apple80211_awdl_bssid *data) override { return storeRequested(requestedBSSID, data, "AWDL_BSSID"); }
    IOReturn setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *data) override { return storeRequested(requested_AWDL_ELECTION_METRIC, data, "AWDL_ELECTION_METRIC"); }
    IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence *data) override;
    IOReturn setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *data) override;
    IOReturn setAWDL_SYNC_STATE(apple80211_awdl_sync_state *data) override;
    IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds *data) override { return storeRequested(requested_AWDL_ELECTION_RSSI_THRESHOLDS, data, "AWDL_ELECTION_RSSI_THRESHOLDS"); }
    IOReturn setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *data) override { return storeRequested(requested_AWDL_PRESENCE_MODE, data, "AWDL_PRESENCE_MODE"); }
    IOReturn setPEER_CACHE_CONTROL(apple80211_peer_cache_control*) override;
    IOReturn setAWDL_OOB_REQUEST(apple80211_awdl_oob_request *) override;
    IOReturn setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template *data) override;
    IOReturn setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params *) override;
    IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(apple80211_awdl_peer_traffic_registration *) override;
    IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel *data) override;
    IOReturn setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode *) override;
    IOReturn setAWDL_MIN_RATE(apple80211_awdl_min_rate*) override { return unsupported("setAWDL_MIN_RATE"); }
    IOReturn setAWDL_ENCRYPTION_KEYS(apple80211_key*) override { return unsupported("setAWDL_ENCRYPTION_KEYS"); }
    IOReturn setAWDL_ENCRYPTION_TYPE(apple80211_encrypt*) override { return unsupported("setAWDL_ENCRYPTION_TYPE"); }
    IOReturn setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request *) override;
    IOReturn setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*) override { return unsupported("setAWDL_TXCAL_PERIOD"); }
    IOReturn setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add*) override { return unsupported("setAWDL_AWDL_ADVERTISERS"); }
    IOReturn setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*) override { return unsupported("setAWDL_DFSP_CONFIG"); }
    IOReturn setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config*) override { return unsupported("setAWDL_DFSP_UCSA_CONFIG"); }
    IOReturn setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create*) override;
    IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(apple80211_awdl_af_sec_payload_request*) override { return unsupported("setAWDL_OOB_SEC_PAYLOAD_REQUEST"); }
    IOReturn setLLW_PARAMS(apple80211_llw_params*) override;
    IOReturn setTX_RATE(apple80211_tx_rate*) override { return unsupported("setTX_RATE"); }
    IOReturn setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection*) override { return unsupported("setP2P_COEX_PROTECTION"); }
    IOReturn setPSF_DWELL(apple80211_awdl_psf_dwell_params*) override { return unsupported("setPSF_DWELL"); }
#if __IO80211_TARGET != __MAC_14_4   // Sonoma's AWDL protocol has no setAWDL_CONFIG (62 methods, not 63)
    IOReturn setAWDL_CONFIG(apple80211_awdl_config_request*) override { return unsupported("setAWDL_CONFIG"); }
#endif
    IOReturn setAWDL_RTG_PEER_STATS_CONFIG(apple80211_rtg_peer_stats_cfg*) override { return unsupported("setAWDL_RTG_PEER_STATS_CONFIG"); }
private:
    void noteClientEvent(const char *what, IOService *client, long result, bool trace);
    static constexpr unsigned clientEventCapacity = 12;
    char clientEvents[clientEventCapacity][200] = {};
    unsigned clientEventCount = 0;
    // These are desired host settings, not an active radio schedule. Native
    // synchronization/state/frame operations remain unsupported.
    IOLock *configurationLock = nullptr;
    // Allocated on first use, race-free, so every path that needs the lock has it whatever runs first
    // (audit D01, P-R05). Freed in free().
    IOLock *ensureConfigurationLock() {
        if (!configurationLock) {
            IOLock *fresh = IOLockAlloc();
            if (fresh && !OSCompareAndSwapPtr(nullptr, fresh, reinterpret_cast<void * volatile *>(&configurationLock)))
                IOLockFree(fresh);
        }
        return configurationLock;
    }
    ItlAWDLFrameTemplate frameTemplate;
    uint8_t localFrameAddress[6] = {};
    bool dataCopyProbeEnabled = false;
    unsigned dataCopyAttempts = 0, dataCopySuccess = 0;
    bool dataDeliverEnabled = false;
    unsigned dataDelivered = 0, dataDeliverFailed = 0;
    unsigned transmitCallbacks = 0, transmitPackets = 0, receiveSupplyCallbacks = 0;
    bool transmitSampled = false;
    void noteTransmit(IOSkywalkPacket *const *, UInt32);
    void noteQueueState(const char *);
    UInt32 acceptTransmit(IOSkywalkPacket *const *, UInt32);
    // Only under the shared gate. Each frame is a copy, so no packet waits on the radio.
    struct TransmitFrame { uint16_t length; bool llw; uint8_t bytes[1514]; };   // llw: sent as plain RFC 1042, not AWDL
    // itlawdltxqueue=N, 128 to 4096: frames held while the radio can't send, such as while the
    // peer is on another channel. A full queue drops, and the sender's transport reads that as loss.
    // Set once, before the frames are allocated (about 1.5 KB each).
    static constexpr unsigned transmitFrameCapacity = 4096, transmitFrameDefault = 128;
    unsigned transmitFrameCount = transmitFrameDefault;
    TransmitFrame *transmitFrames = nullptr;
    // Size of the allocation above, kept apart from transmitFrameCount, which a second initialisation
    // can change (audit D03).
    size_t transmitFramesBytes = 0;
    unsigned transmitHead = 0, transmitQueued = 0, transmitDropped = 0, transmitAccepted = 0;
    unsigned transmitHeldShort = 0;  // Short frames kept in order behind a paused or full aggregate queue.
    unsigned transmitPopped = 0, transmitHeldMark = 0;
    IOSkywalkPacket *transmitDone[transmitFrameCapacity] = {};
    unsigned transmitDoneCount = 0, transmitCompleted = 0, transmitLastPublished = 0;
    void drainTransmitDone();
    uint32_t transmitDepthBins[9] = {};
    uint16_t transmitSequence = 0;
    bool dataTransmitEnabled = false;
    ItlAWDLNativeState nativeState;
    unsigned oobRequests = 0;
    unsigned peerCacheRequests = 0, llwParamRequests = 0;
#if defined(ITL_AWDL_LLW)
    // -itlawdlllw: the companion low latency interface (llw0), created when macOS asks for it with request 293.
    // Owned here and torn down with this interface. itlawdlllwrx chooses which received frames it gets.
    class ItlLLWInterface *companion = nullptr;
    IO80211Controller *owner = nullptr;   // not retained: it owns this interface
    bool companionWanted = false;
    int companionReceiveRule = 0;
    unsigned companionRequests = 0, companionTxQueued = 0, companionTxDropped = 0;
#endif
    // Peer cache control (Continuity Camera, Sidecar traffic registration). The family sleeps after the call until
    // the driver posts event 51 with the same 464-byte record (+4 = 1 added, 3 deleted). A wakeup sent from inside
    // the handler is lost, because the caller has not gone to sleep yet, so the completion is queued here and
    // posted by a timer on the interface work loop after the handler has returned.
    static constexpr unsigned peerCacheBytes = itlawdl::peerCacheRecordBytes, peerCacheQueueDepth = 8;
    struct PeerCacheDone { uint8_t bytes[peerCacheBytes]; };
    PeerCacheDone peerCacheDone[peerCacheQueueDepth];
    unsigned peerCacheDoneCount = 0, peerCacheCompleted = 0, peerCacheDropped = 0;
    IOTimerEventSource *peerCacheTimer = nullptr;
    static void peerCacheTick(OSObject *, IOTimerEventSource *);
    void postPeerCacheCompletions();
    // -itlawdlumiack: Continuity Camera registers a real-time group by sending the peer a "UMI" action frame and
    // waiting for its transmit status (message 69: peer address, then a status byte at +6, 0 = acknowledged).
    // The family sends that frame from a timer that only runs inside an availability window, and this driver
    // reports no windows, so the status never comes and the registration fails after 3 s. With the flag set the
    // driver reports the status itself, shortly after it has answered the peer cache add that precedes it.
    bool umiAckWanted = false;
    static constexpr unsigned umiAckQueueDepth = 8;
    uint8_t umiAckPeers[umiAckQueueDepth][6] = {};
    unsigned umiAckCount = 0, umiAckPosted = 0;
    // The peer of the last cache add. Once a peer is cached the family stops asking for it, so later
    // registrations (each begins with two LLW_PARAMS requests) are answered for this peer.
    uint8_t umiLastPeer[6] = {};
    bool umiLastPeerValid = false;
    void queueUmiAck(const uint8_t *peer);
    IOTimerEventSource *umiAckTimer = nullptr;
    static void umiAckTick(OSObject *, IOTimerEventSource *);
    void postUmiAcks();
    // -itlawdlpeeralive: the family drops the iPhone from its peer table when it has heard no action frame for 2 s and has no
    // unicast activity from it for 6 s (IO80211AWDLPeer::shouldRemovePeerFromNDP, 24H232 0xffffff8002149bf5). That clears the
    // camera's traffic registration and ends the real-time group every 6-11 s. The camera's video arrives on llw0, which does
    // not update the family's per-peer unicast time, so this sets it (IO80211Peer::setLastRxUnicastLinkActivity, a plain store
    // of mach_absolute_time) at most every 250 ms per peer, from the RX path (inside the work loop gate).
    // -itlawdlndpkeep: raises the family's own limit (ndpRemovalTimeoutmSec) to 100 s through IO80211AWDLPeerManager::setAwdlCtl.
    // Both call IO80211Family symbols that exist in 24H232 (Sequoia 15.x) only; other targets build them as no-ops.
    bool peerAliveWanted = false, ndpKeepWanted = false, ullInfraWanted = false;
    struct PeerAliveSlot { uint8_t mac[6]; uint64_t last; };
    PeerAliveSlot peerAlive[4] = {};
    uint64_t peerAliveInterval = 0;
    uint32_t peerAliveCalls = 0, peerAliveNoManager = 0, peerAliveNoPeer = 0, ndpKeepApplied = 0, ullInfraApplied = 0;
    // -itlawdlrtgkeep: the iPhone never sends the real-time group TLV (type 31) in the action frames we hear (ITLAWDLRtgPeerSeen 0 of
    // 752), and the family removes an active RTG peer after 5 s without one (IO80211AWDLPeer::actionFrameReport, 24H232
    // 0xffffff8002151549: "Active RTG Peer has no RTG TLV for 5 Seconds", then removePeerFromRealTimeGroup). The time it compares
    // is a mach_absolute_time stamp at peer ivars +0x6a0 that parseAWDLRealTimeGroupTLV writes. This refreshes it while we hear the peer.
    bool rtgKeepWanted = false;
    uint64_t rtgKeepSanity = 0;
    uint32_t rtgKeepRefreshed = 0, rtgKeepSkipped = 0;
    void keepPeerAlive(const uint8_t *peer);
    IOTimerEventSource *ndpKeepTimer = nullptr;
    // -itlawdlawevents: availability-window events 41 (start, 36 B) and 43 (end, 40 B) for the family. The firmware's AWDL scheduler
    // posts them on Broadcom; without them the family believes it is never inside a window, so handleUmiTimer / sendUnicastMI
    // ("Asked to send an UMI, but we are not in an AW") never send the real-time UMI that makes the iPhone join the RTG and move to
    // our channel plan (a real Mac: UMI 0xb000000, the iPhone's RTG TLV 40 ms later, its channel sequence converges on the Mac's).
    // One start every 64 TU slot (AwNumber +4), the end 16 TU later, timed from the host clock. Layouts: AWDL_PEERMANAGER_DRIVER_ABI.md
    // 2.2/2.5, handler of 24H232 read with REA. Also records the UMI requests the family then makes (ioctl 149 = AWDL_OOB_REQUEST).
    bool awEventsWanted = false, awSyncEnabled = false, awInWindow = false;
    IOTimerEventSource *awEventTimer = nullptr;
    static void awEventTick(OSObject *, IOTimerEventSource *);
    void postAwEvent();
    void stopAwEvents();
    uint16_t awNumber = 0;
    uint32_t awStarts = 0, awEnds = 0, umiRequests = 0, awAlignedStarts = 0;
    uint64_t awSlotStartUS = 0;
    void noteUmiRequest(const apple80211_awdl_oob_request *data);
    // -itlawdlumitx: the UMI is transmitted for real. noteUmiRequest keeps the family's template (copied inside the ioctl, the
    // buffer is only valid during it), the HAL builds a unicast master indication from it and sends it in the iPhone's listening
    // time, and the frame's own transmit result is reported as message 69 (status byte at +6: 0 acknowledged) instead of the
    // fabricated acknowledgement of -itlawdlumiack, which is not posted while this is on.
public:
    struct UmiRequest { uint8_t peer[6]; uint16_t option; uint16_t length; uint8_t tlvs[512]; };
private:
    static constexpr unsigned umiQueueDepth = 2, umiResultDepth = 4;
    bool umiTxWanted = false;
    UmiRequest umiQueue[umiQueueDepth] = {};
    unsigned umiQueueCount = 0, umiResultCount = 0;
    uint8_t umiResultPeers[umiResultDepth][6] = {};
    uint8_t umiResultOk[umiResultDepth] = {};
    uint32_t umiTxQueued = 0, umiTxDropped = 0, umiResultsPosted = 0;
    // What arrives in the 3 s after a UMI went out (ItlwmAWDLUmiRxRing, 20-byte entries: ms since the UMI u16, flags u8 (1 = to us,
    // 2 = from the UMI's peer, 4 = unicast), subtype u8, length u16, rssi s8, channel u8, TLV count u8, then up to 11 TLV types).
    struct UmiWatchEntry { uint16_t ms; uint8_t flags, subtype; uint16_t length; int8_t rssi; uint8_t channel, tlvCount; uint8_t types[11]; };
    static constexpr unsigned umiWatchDepth = 40;
    UmiWatchEntry umiWatch[umiWatchDepth] = {};
    unsigned umiWatchCount = 0;
    uint64_t umiWatchStartUS = 0, umiWatchEndUS = 0;
    uint8_t umiWatchPeer[6] = {};
    void noteUmiWatch(const uint8_t *frame, size_t length, uint32_t channel, int32_t rssi);
public:
    bool umiPending();
    bool takeUmi(UmiRequest *out);
    void noteUmiTxResult(const uint8_t *peer, bool acknowledged);
private:
    void postUmiResults();
    static void ndpKeepTick(OSObject *, IOTimerEventSource *);
    void applyNdpKeep();
    bool oobSampled = false, debugFlagsSet = false, debugFlagsWanted = false;
    IOReturn noteOobRequest(const apple80211_awdl_oob_request *, const char *);
    // Native 25G241 layout read from both the sender and Apple's Broadcom handler:
    // length at +0x28 and a pointer to peer manager storage at +0x30, 0x4b0 bytes at most.
    void storeMasterIndication(const apple80211_awdl_oob_request *);
    uint8_t masterIndication[0x4b0] = {};
    size_t masterIndicationLength = 0;
    bool masterIndicationEnabled = false;
    uint16_t masterIndicationSampledLength = 0;
    unsigned masterIndicationUpdates = 0, masterIndicationRejected = 0;
    uint32_t afTxModeValue = 0;
    ItlAWDLRequestedValue<apple80211_awdl_bssid> requestedBSSID;
    ItlAWDLRequestedValue<apple80211_awdl_sync_params> requested_AWDL_SYNC_PARAMS;
    ItlAWDLRequestedValue<apple80211_awdl_extension_state_machine_parameter> requested_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS;
    ItlAWDLRequestedValue<apple80211_awdl_election_id> requested_AWDL_ELECTION_ID;
    ItlAWDLRequestedValue<apple80211_awdl_election_metric> requested_AWDL_ELECTION_METRIC;
    ItlAWDLRequestedValue<apple80211_awdl_sync_channel_sequence> requested_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE;
    ItlAWDLRequestedValue<apple80211_awdl_master_channel> requested_AWDL_MASTER_CHANNEL;
    ItlAWDLRequestedValue<apple80211_awdl_secondary_master_channel> requested_AWDL_SECONDARY_MASTER_CHANNEL;
    ItlAWDLRequestedValue<apple80211_awdl_election_rssi_thresholds> requested_AWDL_ELECTION_RSSI_THRESHOLDS;
    ItlAWDLRequestedValue<apple80211_awdl_presence_mode> requested_AWDL_PRESENCE_MODE;
    template <typename T> IOReturn storeRequested(ItlAWDLRequestedValue<T> &slot,
                                                 const T *data, const char *name) {
        if (!configurationLock)
            return kIOReturnNotReady;
        IOLockLock(configurationLock);
        const IOReturn result = slot.set(data);
        IOLockUnlock(configurationLock);
        if (result == kIOReturnSuccess) {
            setProperty("ItlwmAWDLLastStoredRequest", name);
            // The request exactly as macOS sent it (for example the channel sequence and its width flags), under ItlwmAWDLRaw_<name>.
            char key[80];
            snprintf(key, sizeof(key), "ItlwmAWDLRaw_%s", name);
            if (auto *raw = OSData::withBytes(data, sizeof(T))) {
                setProperty(key, raw);
                raw->release();
            }
        }
        noteCall(name, result);
        return result;
    }
    template <typename T> IOReturn readRequested(const ItlAWDLRequestedValue<T> &slot,
                                                T *data) {
        if (!configurationLock)
            return kIOReturnNotReady;
        IOLockLock(configurationLock);
        const IOReturn result = slot.get(data);
        IOLockUnlock(configurationLock);
        return result;
    }
    // What macOS asks for as this Mac's AWDL channels (published only; the listener doesn't follow it yet).
    uint32_t requestedSequenceSets = 0;
    void publishRequestedSequence();
    void releaseNetworkResources();
    static UInt32 rejectTransmit(OSObject*, IOSkywalkTxSubmissionQueue*, IOSkywalkPacket *const*, UInt32, void*);
    static UInt32 deferReceive(OSObject*, IOSkywalkRxSubmissionQueue*, IOSkywalkPacket *const*, UInt32, void*);
    IOSkywalkPacketBufferPool *txPool = nullptr;
    IOSkywalkPacketBufferPool *rxPool = nullptr;
    IOEventSource *packetQueues[4] = {};
    IOWorkLoop *queueWorkLoop = nullptr;
    UInt32 addedQueues = 0;
    bool nativeStarted = false;
    bool registrationAttempted = false;
    uint32_t managementSubmissions = 0;
    IOReturn unsupported(const char *operation) {
        setProperty("ItlwmAWDLLastUnsupported", operation);
        noteCall(operation, kIOReturnUnsupported);
        return kIOReturnUnsupported;
    }
    // Every apple80211 request the daemons make of this interface that goes through unsupported() or storeRequested():
    // name, how many, and the last answer, published as ItlwmAWDLCalls ("name=count/result;..."). It shows what a
    // Continuity Camera session asks for, not only the last refused call.
    struct CallStat { const char *name; uint32_t count; uint32_t last; };
    static constexpr unsigned callStatCapacity = 64;
    CallStat callStats[callStatCapacity] = {};
    unsigned callStatCount = 0;
    uint32_t callTotal = 0;
    void noteCall(const char *name, IOReturn result);
    // Each distinct channel sequence macOS has asked for (up to four), published as ItlwmAWDLRawSeq0..3: the plan changes between
    // the idle one and the real-time group's, and ItlwmAWDLRaw_<name> only keeps the latest.
    uint8_t seqHistory[4][sizeof(apple80211_awdl_sync_channel_sequence)] = {};
    unsigned seqHistoryCount = 0;
    void noteSequenceHistory(const apple80211_awdl_sync_channel_sequence *data) {
        if (!data)
            return;
        for (unsigned i = 0; i < seqHistoryCount; ++i)
            if (!memcmp(seqHistory[i], data, sizeof(*data)))
                return;
        if (seqHistoryCount >= 4)
            return;
        memcpy(seqHistory[seqHistoryCount], data, sizeof(*data));
        char key[32];
        snprintf(key, sizeof(key), "ItlwmAWDLRawSeq%u", seqHistoryCount);
        ++seqHistoryCount;
        if (auto *raw = OSData::withBytes(data, sizeof(*data))) {
            setProperty(key, raw);
            raw->release();
        }
    }
    // The raw traffic registration records (ioctl 164, 184 bytes: version, peer, name length and name, active, and 12 bytes
    // the struct above stops short of), the distinct ones, for decoding the preferred channels and the bandwidth and latency
    // a service asks for.
    static constexpr unsigned trafficRegBytes = 184, trafficRegCapacity = 6;
    uint8_t trafficRegs[trafficRegCapacity][trafficRegBytes] = {};
    unsigned trafficRegCount = 0, trafficRegSeen = 0;
    void noteTrafficRegistration(const apple80211_awdl_peer_traffic_registration *data);
};
