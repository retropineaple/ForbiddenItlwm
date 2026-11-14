// Host-only dependencies for extracted, unmodified driver methods.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <libkern/OSByteOrder.h>
#define htole32 OSSwapHostToLittleInt32
#define htole16 OSSwapHostToLittleInt16
#define IEEE80211_ADDR_COPY(dst, src) memcpy(dst, src, 6)
static uint8_t etheranyaddr[6] = {};

enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
    IEEE80211_S_ASSOC, IEEE80211_S_RUN };
enum { IWX_FLAG_SHUTDOWN = 1, IWX_FLAG_SCANNING = 2, IWX_FW_CTXT_ACTION_ADD = 1,
    IWX_FW_CTXT_ACTION_MODIFY, IWX_FW_CTXT_ACTION_REMOVE, IWX_FW_CTXT_INVALID = 255,
    IWX_MAX_MACS_IN_BINDING = 4, IWX_LMAC_24G_INDEX = 0, IWX_LMAC_5G_INDEX = 1,
    IWX_PHY_VHT_CHANNEL_MODE20 = 0, IWX_PHY_VHT_CHANNEL_MODE80 = 2,
    IWX_UCODE_TLV_CAPA_DYNAMIC_QUOTA = 0, IWX_UCODE_TLV_CAPA_CDB_SUPPORT = 1,
    IWX_BINDING_CONTEXT_CMD = 10, IWX_REMOVE_STA = 11, IWX_MONITOR_STA_ID = 12,
    IWX_MAC_CONF_GROUP = 1, IWX_FW_CMD_VER_UNKNOWN = 255, kIwxLowLatencyCmd = 3,
    kAwdlCoexPhyId = 1, kAwdlCoexMacId = 2, kCoexBaTxOff = 0,
    THREAD_UNINT = 0, THREAD_AWAKENED = 0, kSecondScale = 1, kIOReturnSuccess = 0,
    IWX_STA_GENERAL_PURPOSE = 1, IWX_STA_FLG_FAT_EN_20MHZ = 0, IWX_STA_FLG_MIMO_EN_SISO = 0,
    IWX_STA_FLG_FAT_EN_MSK = 1, IWX_STA_FLG_MIMO_EN_MSK = 2, IWX_ADD_STA_SUCCESS = 1,
    IWX_ADD_STA_STATUS_MASK = 255, IWX_ADD_STA = 13, IWX_MGMT_TID = 15, IWX_DEFAULT_QUEUE_SIZE = 256 };
#define IWX_FW_CMD_ID_AND_COLOR(id, color) ((id) | ((color) << 8))
#define IEEE80211_IS_CHAN_2GHZ(c) (true)
#define XYLog(...) ((void)0)
#define DEVNAME(sc) "host"
#define nitems(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(p, type, member) (reinterpret_cast<type *>(p))
using IOReturn = int;
using OSObject = void;
using AbsoluteTime = uint64_t;
namespace ItlAWDL { struct BaFilterState {}; }
struct Pending { void clear() {} };
struct ieee80211_channel {};
struct iwx_phy_ctxt { int id = 0, color = 0; ieee80211_channel *channel = nullptr; };
struct ieee80211_node {};
struct iwx_node : ieee80211_node { iwx_phy_ctxt *in_phyctxt = nullptr; int in_id = 0, in_color = 0; };
struct ieee80211com { ieee80211_state ic_state = IEEE80211_S_RUN;
    ieee80211_node *ic_bss = nullptr; ieee80211_channel ic_channels[256]; };
struct Ring {};
struct iwx_softc { ieee80211com sc_ic; int sc_generation = 7, sc_flags = 0;
    ieee80211_state ns_nstate = IEEE80211_S_AUTH; int ns_arg = 0, init_task = 0;
    int sc_enabled_capa[2] = {}; iwx_phy_ctxt sc_phyctxt[2]; Ring txq[4];
    std::function<void(ieee80211com *, ieee80211_state, int)> sc_newstate; };
struct iwx_binding_cmd { uint32_t id_and_color, action, phy, macs[4], lmac_id; };
struct iwx_mac_low_latency_cmd { uint32_t mac_id, low_latency_rx, low_latency_tx; };
struct iwx_rm_sta_cmd { int sta_id; };
struct iwx_add_sta_cmd { int sta_id, station_type; uint32_t mac_id_n_color;
    uint8_t addr[6]; uint16_t tid_disable_tx; uint32_t station_flags, station_flags_msk; };
static int awdlCoexSetupAttempts, awdlCoexSetupAborts, awdlCoexQueueAssigned, awdlCoexSetupGen,
    awdlCoexStuckStreak;
static uint32_t awdlCoexQueueWritePointer, awdlCoexStaResult, awdlCoexQueueResult, awdlCoexSetupToken;
static int awdlCoexTxSettingUpGen = -1;
static uint32_t awdlCoexTxSettingUpOwner, awdlCoexTxSettingUpNext;
static int awdlCoexStationTransitionGen = -1, awdlCoexStationReadyGen = 7;
static uint32_t awdlCoexStationTransitionOwner, awdlCoexStationTransitionNext;
static bool awdlCoexProbe = true, awdlCoexWant = true;
static uint64_t awdlCoexLastCompletionUS, awdlCoexTxPauseUntilUS;
static const char *awdlCoexQueueStage;
static bool awdlCoexAdded, awdlCoexTxStaAdded, awdlCoexInFlight, awdlCoexAddrApplied,
    awdlCoexMulti, awdlCoexMultiActive, awdlCoexMovePending, awdlCoexPromiscLive,
    awdlCoexPromiscUncertain, awdlCoexBaPeerAdded, awdlCoexBaPeerAccepted,
    awdlCoexBaFilterOff, awdlCoexRemoveRequested, awdlStopping, awdlCoexLowLatency,
    awdlCoexAddrKnown, awdlCoexTx, awdlCoexTxHalted;
static int awdlCoexGeneration = 7, awdlCoexAddingGen = -1, awdlCoexRemovingGen = -1,
    awdlCoexBrokenGeneration = -1, awdlCoexPhyUncertainGen = -1,
    awdlCoexWide80FailedGen = -1, awdlCoexPendingSlot = -1, awdlCoexBaGen,
    awdlCoexBaTids, awdlCoexBaAnswerLength, awdlCoexBaTxState, awdlCoexBaTxQueue,
    awdlCoexBaTidTeardown, awdlCoexBaRxIdleTids, awdlCoexQueue = -1,
    awdlCoexTxSetupTried, awdlCoexPhyWidth, awdlCoexOwnWidth,
    awdlCoexPhyResult, awdlCoexMultiBindResult, awdlCoexMacResult, awdlCoexBindStatus,
    awdlCoexBindResult, awdlCoexLowLatencyResult, awdlCoexTeardownErrors,
    awdlCoexAdds, awdlCoexRemoves, awdlCoexChannel, awdlFixedChannel,
    awdlCoexOrphanQueue = -1;
static uint32_t awdlCoexToken, awdlCoexAddingNext, awdlCoexAddingOwner;
static uint32_t awdlCoexAddsAfterRemove, awdlCoexAddsAfterRestart;
static bool awdlCoexLostToRestart;
static uint64_t awdlCoexLastMoveUS;
static ieee80211_channel *awdlCoexOwnChan;
static const char *awdlCoexMultiState;
static Pending awdlCoexBaRequestsPending;
static ItlAWDL::BaFilterState awdlCoexBaFilter;
static uint8_t awdlCoexAddr[6], awdlCoexAddrInFirmware[6];
static std::vector<std::string> events;
static std::mutex mutex;
static std::condition_variable cv;
static thread_local std::unique_lock<std::mutex> *held;
static bool firmwareWaiting, releaseFirmware;
static std::string scenario;
static bool setupCase() { return scenario.find("setup-") == 0; }
static bool transitionCase() { return scenario.find("transition-") == 0; }
static bool transitionPauseUsed;
static bool suffix(const char *s) { return scenario.find(s) != std::string::npos; }
static void pauseFirmware() {
    firmwareWaiting = true; cv.notify_all();
    cv.wait(*held, [] { return releaseFirmware; });
}
static int systq, restarts;
static void task_add(int, int *) { ++restarts; events.push_back("restart"); }
static int splnet() { return 0; }
static void splx(int) {}
static bool isset(int *a, int n) { return a[n]; }
static int ieee80211_chan2ieee(ieee80211com *, ieee80211_channel *) { return 6; }
static uint64_t awdlUptimeUS() { return 1; }
static int iwx_cmd_id(int, int, int) { return 0; }
static void clock_interval_to_deadline(int, int, uint64_t *d) { *d = 1; }
struct Loop { bool inGate() { return true; } };
struct Gate {
    int runAction(IOReturn (*action)(OSObject *, void *, void *, void *, void *), void *p) {
        return action(nullptr, p, nullptr, nullptr, nullptr);
    }
    int commandSleep(void *, AbsoluteTime, int) {
        events.push_back("wait");
        if (scenario == "restart" || scenario == "add-restart" || (setupCase() && suffix("restart"))) {
            // Restart and a newer listener may appear while the old waiter sleeps.
            // The old task must not operate on that station.
            return restartWhileWaiting();
        }
        if (scenario == "timeout" || scenario == "add-timeout" || (setupCase() && suffix("timeout"))) return 1;
        if (setupCase() && suffix("shutdown")) return shutdownWhileWaiting();
        releaseFirmware = true;
        cv.notify_all();
        return cv.wait_for(*held, std::chrono::seconds(1)) == std::cv_status::timeout ? 1 : THREAD_AWAKENED;
    }
    int restartWhileWaiting();
    int shutdownWhileWaiting();
    void commandWakeup(void *) { events.push_back("wake"); cv.notify_all(); }
};
class ItlIwx {
public:
    iwx_softc com; Loop loop; Gate gate;
    Loop *getMainWorkLoop() { return &loop; }
    Gate *getMainCommandGate() { return &gate; }
    int awdlCoexListener(iwx_softc *, bool);
    int awdlCoexTxTeardown(iwx_softc *);
    int awdlCoexTxSetup(iwx_softc *);
    static void iwx_newstate_task(void *);
    void awdlCoexPublish() {}
    int awdlCoexPickOwn(iwx_softc *, uint8_t *) { return 0; }
    int iwx_phy_ctxt_cmd(iwx_softc *, iwx_phy_ctxt *, int, int, int, int) { return 0; }
    uint8_t iwx_lookup_cmd_ver(iwx_softc *, int, int) { return 1; }
    int awdlCoexMacSend(iwx_softc *, int action) {
        events.push_back(action == IWX_FW_CTXT_ACTION_ADD ? "mac-add" : "mac-remove");
        if (action == IWX_FW_CTXT_ACTION_ADD &&
            (scenario == "add-wait" || scenario == "add-rollback-failure" ||
             scenario == "add-timeout" || scenario == "add-restart")) {
            firmwareWaiting = true; cv.notify_all();
            cv.wait(*held, [] { return releaseFirmware; });
        }
        return action == IWX_FW_CTXT_ACTION_REMOVE &&
            (scenario == "rollback-failure" || scenario == "add-rollback-failure") ? EIO : 0;
    }
    int iwx_send_cmd_pdu_status(iwx_softc *, int cmd, size_t, void *, uint32_t *status) {
        if (cmd == IWX_ADD_STA) {
            events.push_back("sta-add");
            if (scenario.find("setup-sta") == 0) pauseFirmware();
            *status = IWX_ADD_STA_SUCCESS; return 0;
        }
        events.push_back("binding"); *status = 0; return 0;
    }
    int iwx_tvqm_enable_txq(iwx_softc *, int tid, int, uint32_t size, int sta) {
        assert(tid == IWX_MGMT_TID && size == IWX_DEFAULT_QUEUE_SIZE && sta == IWX_MONITOR_STA_ID);
        events.push_back("queue");
        if (scenario.find("setup-queue") == 0) pauseFirmware();
        return suffix("negative") ? -ECANCELED : 1;
    }
    int awdlCoexBaRemovePeer(iwx_softc *) { events.push_back("peer-remove"); return 0; }
    int iwx_flush_sta_tids(iwx_softc *sc, int sta, int tids) {
        assert(sta == IWX_MONITOR_STA_ID && tids == 0xffff);
        events.push_back("flush");
        if (scenario == "remove-wait") {
            firmwareWaiting = true; cv.notify_all();
            cv.wait(*held, [] { return releaseFirmware; });
        }
        if (scenario == "remove-restart") { ++sc->sc_generation; awdlCoexTxStaAdded = true; }
        return scenario == "tx-failure" ? EIO : 0;
    }
    int iwx_send_cmd_pdu(iwx_softc *, int cmd, int, size_t, void *data) {
        assert(cmd == IWX_REMOVE_STA);
        assert(static_cast<iwx_rm_sta_cmd *>(data)->sta_id == IWX_MONITOR_STA_ID);
        events.push_back("sta-remove"); return setupCase() && suffix("failure") ? EIO : 0;
    }
    void iwx_reset_tx_ring(iwx_softc *, Ring *) { events.push_back("ring-reset"); }
    int stationStep(const char *name) {
        events.push_back(name);
        const bool selected = transitionCase() && (scenario == std::string("transition-") + name ||
            scenario.find(std::string("transition-") + name + "-") == 0);
        if (selected && !transitionPauseUsed) {
            transitionPauseUsed = true;
            pauseFirmware();
        }
        return selected && suffix("failure") ? EIO : 0;
    }
    int iwx_run_stop(iwx_softc *) { return stationStep("run-stop"); }
    int iwx_deauth(iwx_softc *) { return stationStep("deauth"); }
    int iwx_scan(iwx_softc *) { events.push_back("scan"); return 0; }
    int iwx_auth(iwx_softc *) { return stationStep("auth"); }
    int iwx_rs_init(iwx_softc *, iwx_node *, bool) { return 0; }
    int iwx_run(iwx_softc *) { return stationStep("run"); }
};
static ItlIwx *driver;
int Gate::shutdownWhileWaiting() {
    driver->com.sc_flags |= IWX_FLAG_SHUTDOWN;
    awdlStopping = true;
    return THREAD_AWAKENED;
}
int Gate::restartWhileWaiting() {
    ++driver->com.sc_generation;
    awdlCoexGeneration = driver->com.sc_generation;
    awdlCoexAddingGen = driver->com.sc_generation;
    awdlCoexAdded = awdlCoexTxStaAdded = true;
    ++awdlCoexAddingOwner;
    if (setupCase()) {
        awdlCoexTxSettingUpGen = driver->com.sc_generation;
        awdlCoexTxSettingUpOwner = ++awdlCoexTxSettingUpNext;
        awdlCoexQueue = 2;
    }
    return THREAD_AWAKENED;
}

// DRIVER_IMPLEMENTATIONS

static bool saw(const char *event) {
    return std::find(events.begin(), events.end(), event) != events.end();
}
static void before(const char *first, const char *second) {
    assert(saw(first) && saw(second));
    assert(std::find(events.begin(), events.end(), first) < std::find(events.begin(), events.end(), second));
}
int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    ItlIwx instance; driver = &instance;
    auto &sc = driver->com;
    iwx_node node; node.in_phyctxt = &sc.sc_phyctxt[0]; sc.sc_ic.ic_bss = &node;
    sc.sc_newstate = [](ieee80211com *ic, ieee80211_state state, int) {
        driver->stationStep("commit"); ic->ic_state = state;
    };
    std::unique_lock<std::mutex> lock(mutex); held = &lock;
    awdlCoexAdded = scenario != "tx-only" && scenario != "no-owner";
    awdlCoexTxStaAdded = scenario == "tx-only" || scenario == "tx-failure" || scenario == "normal" || scenario == "remove-restart";
    awdlCoexQueue = awdlCoexTxStaAdded ? 1 : -1;
    std::thread add;
    if (transitionCase()) {
        if (scenario == "transition-run") sc.ns_nstate = IEEE80211_S_RUN;
        add = std::thread([&] {
            std::unique_lock<std::mutex> stateLock(mutex); held = &stateLock;
            ItlIwx::iwx_newstate_task(&sc);
        });
        cv.wait(lock, [] { return firmwareWaiting; });
        const auto commandsBefore = events.size();
        const int addResult = driver->awdlCoexListener(&sc, true);
        const int setupResult = driver->awdlCoexTxSetup(&sc);
        const bool quiet = events.size() == commandsBefore;
        if (suffix("restart")) {
            ++sc.sc_generation;
            sc.sc_ic.ic_state = IEEE80211_S_AUTH;
            sc.ns_nstate = IEEE80211_S_RUN;
            ItlIwx::iwx_newstate_task(&sc);
        } else if (suffix("owner")) {
            ++sc.sc_generation;
            awdlCoexStationTransitionGen = sc.sc_generation;
            awdlCoexStationTransitionOwner = ++awdlCoexStationTransitionNext;
        } else if (suffix("shutdown")) {
            sc.sc_flags |= IWX_FLAG_SHUTDOWN;
        }
        releaseFirmware = true; cv.notify_all(); lock.unlock(); add.join(); lock.lock();
        for (const auto &event : events) std::printf("%s ", event.c_str());
        std::puts(""); std::fflush(stdout);
        assert(addResult != 0 && setupResult != 0 && quiet);
        if (suffix("failure")) {
            assert(restarts == 1 && !saw("commit"));
            assert(driver->awdlCoexListener(&sc, true) != 0);
            assert(driver->awdlCoexTxSetup(&sc) != 0);
            assert(!saw("mac-add") && !saw("sta-add"));
        } else if (suffix("restart")) {
            assert(restarts == 0 && awdlCoexAdded && sc.sc_ic.ic_state == IEEE80211_S_RUN);
            assert(std::count(events.begin(), events.end(), "commit") == 1);
            if (scenario == "transition-run-stop-restart") assert(!saw("deauth"));
            assert(awdlCoexStationReadyGen == sc.sc_generation && awdlCoexStationTransitionGen == -1);
        } else if (suffix("owner")) {
            assert(!saw("deauth") && !saw("commit") && restarts == 0);
            assert(awdlCoexStationTransitionGen == sc.sc_generation && awdlCoexStationTransitionOwner != 0);
            assert(driver->awdlCoexListener(&sc, true) == EBUSY);
            assert(driver->awdlCoexTxSetup(&sc) == EBUSY);
        } else if (suffix("shutdown")) {
            assert(restarts == 0 && !saw("commit") && !saw("mac-add"));
            if (scenario == "transition-run-stop-shutdown") assert(!saw("deauth"));
        } else {
            assert(restarts == 0 && awdlCoexStationTransitionGen == -1);
            if (scenario != "transition-run") {
                assert(!awdlCoexAdded && sc.sc_ic.ic_state == IEEE80211_S_AUTH);
                assert(driver->awdlCoexListener(&sc, true) != 0);
                sc.ns_nstate = IEEE80211_S_RUN;
                ItlIwx::iwx_newstate_task(&sc);
            }
            before("run", "mac-add"); before("commit", "mac-add");
            assert(awdlCoexAdded && sc.sc_ic.ic_state == IEEE80211_S_RUN && restarts == 0);
            assert(driver->awdlCoexTxSetup(&sc) == 0);
        }
        return 0;
    }
    if (setupCase()) {
        add = std::thread([&] {
            std::unique_lock<std::mutex> setupLock(mutex); held = &setupLock;
            assert(driver->awdlCoexTxSetup(&sc) == ECANCELED);
            events.push_back("setup-finished");
        });
        cv.wait(lock, [] { return firmwareWaiting; });
    }
    if (scenario == "add-wait" || scenario == "add-rollback-failure" ||
        scenario == "add-timeout" || scenario == "add-restart") {
        awdlCoexAdded = false;
        add = std::thread([&] {
            std::unique_lock<std::mutex> addLock(mutex); held = &addLock;
            assert(driver->awdlCoexListener(&sc, true) == ECANCELED);
            events.push_back("add-finished");
        });
        cv.wait(lock, [] { return firmwareWaiting; });
    }
    if (scenario == "remove-wait") {
        awdlCoexAdded = awdlCoexTxStaAdded = true; awdlCoexQueue = 1;
        add = std::thread([&] {
            std::unique_lock<std::mutex> removeLock(mutex); held = &removeLock;
            assert(driver->awdlCoexListener(&sc, false) == 0);
        });
        cv.wait(lock, [] { return firmwareWaiting; });
    }
    if (scenario == "timeout" || scenario == "restart") {
        awdlCoexAdded = false; awdlCoexAddingGen = sc.sc_generation;
    }
    if (scenario == "equal-run") sc.ns_nstate = IEEE80211_S_RUN;
    ItlIwx::iwx_newstate_task(&sc);
    if (add.joinable()) {
        releaseFirmware = true; cv.notify_all(); lock.unlock(); add.join(); lock.lock();
    }
    for (const auto &event : events) std::printf("%s ", event.c_str());
    std::puts(""); std::fflush(stdout);
    if (setupCase()) {
        if (suffix("restart")) {
            assert(!saw("sta-remove") && !saw("mac-remove") && !saw("run-stop") && !saw("commit"));
            assert(awdlCoexAdded && awdlCoexTxStaAdded && restarts == 0);
            assert(awdlCoexTxSettingUpGen == sc.sc_generation && awdlCoexTxSettingUpOwner != 0);
            assert(awdlCoexQueue == 2 && !saw("ring-reset"));
        } else if (suffix("shutdown")) {
            assert(!saw("mac-remove") && !saw("run-stop") && !saw("commit") && restarts == 0);
        } else if (suffix("failure") || suffix("timeout")) {
            assert(!saw("mac-remove") && !saw("run-stop") && !saw("commit") && restarts == 1);
            if (scenario == "setup-queue-failure") assert(awdlCoexOrphanQueue == 1 && !saw("ring-reset"));
        } else {
            before("sta-remove", "binding"); before("binding", "mac-remove"); before("mac-remove", "run-stop");
            assert(saw("wait") && saw("wake") && !awdlCoexTxStaAdded && !awdlCoexAdded && restarts == 0);
            if (scenario == "setup-queue") before("ring-reset", "mac-remove");
        }
    } else if (scenario == "tx-only") {
        before("sta-remove", "run-stop"); assert(!awdlCoexTxStaAdded && awdlCoexQueue == -1);
        assert(!saw("binding") && !saw("mac-remove"));
    } else if (scenario == "add-wait") {
        before("mac-remove", "run-stop"); assert(saw("wait") && saw("wake"));
        assert(!awdlCoexAdded && awdlCoexAddingGen == -1 && restarts == 0);
    } else if (scenario == "timeout" || scenario == "rollback-failure" || scenario == "tx-failure" ||
               scenario == "add-rollback-failure" || scenario == "add-timeout") {
        assert(!saw("run-stop") && !saw("commit") && restarts == 1);
        if (scenario == "add-timeout" || scenario == "add-rollback-failure") {
            assert(driver->awdlCoexListener(&sc, true) == EIO);
            assert(std::count(events.begin(), events.end(), "mac-add") == 1);
        }
        if (scenario == "tx-failure") {
            assert(awdlCoexOrphanQueue == 1 && awdlCoexQueue == -1 && !awdlCoexTxStaAdded);
            assert(!saw("ring-reset"));
        }
    } else if (scenario == "restart" || scenario == "remove-restart" || scenario == "add-restart") {
        assert(!saw("run-stop") && !saw("deauth") && !saw("commit") && restarts == 0);
        assert(awdlCoexTxStaAdded);
        if (scenario == "restart" || scenario == "add-restart") {
            assert(awdlCoexAdded && !saw("mac-remove"));
            assert(awdlCoexAddingGen == sc.sc_generation && awdlCoexAddingOwner != 0);
        }
        else assert(!saw("sta-remove"));
    } else if (scenario == "normal" || scenario == "remove-wait") {
        before("sta-remove", "binding"); before("binding", "mac-remove"); before("mac-remove", "run-stop");
        assert(!awdlCoexAdded && !awdlCoexTxStaAdded && restarts == 0);
    } else if (scenario == "equal-run") {
        before("mac-remove", "run-stop"); before("run-stop", "run"); assert(saw("commit"));
    } else if (scenario == "no-owner") {
        assert(!saw("mac-remove") && !saw("flush") && !saw("binding") && restarts == 0);
        before("run-stop", "auth");
    }
}
