from pathlib import Path
import subprocess,sys,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1];sys.path.insert(0,str(R/'tests'))
from peer_slot_names import SLOT,rename,unrename
s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text();base=(T/'fixtures/same-channel-before.cpp').read_text()
def body(text,name,end):
 a=text.index(name);b=text.index(end,a);return text[a:b]
a=s.index('static bool awdlCoexSameChannelProbeWanted(');b=s.index('{',a)+1;depth=1
while depth:
 depth+=(s[b]=='{')-(s[b]=='}');b+=1
helper=s[a:b]+'\n'
method='int ItlIwx::\n'+body(s,'awdlCoexMacSend(struct iwx_softc','\n// The listener')
oldmethod='int ItlIwx::\n'+body(base,'awdlCoexMacSend(struct iwx_softc','\n// The listener').replace('awdlCoexMacSend(', 'baselineMacSend(',1)
a=s.index('    if (filterChange) {',s.index('awdlCoexBaHandle(struct'));b=a+len('    if (filterChange) {');depth=1
while depth:
 depth+=(s[b]=='{')-(s[b]=='}');b+=1
restoreBody='    // Restore expired windows even if opening failed with an uncertain firmware state.\n    const bool probeWanted = awdlCoexSameChannelProbeWanted(sc, awdlUptimeUS());\n    if ((awdlCoexSameChannelProbe.applied && !probeWanted) ||\n        (awdlCoexSameChannelProbe.restorePending && !probeWanted))\n        filterChange = true;\n'+s[a:b]
caller='struct { bool off=false; } awdlCoexBaFilter; static unsigned awdlCoexBaFilterChanges;\nvoid ItlIwx::runFilterChange(iwx_softc *sc,bool wanted,bool force){const bool filterWasOff=awdlCoexBaFilterOff;bool filterChange=force||wanted!=filterWasOff;awdlCoexBaFilter.off=wanted;const int gen=sc->sc_generation;auto stillOurs=[&](){return gen==sc->sc_generation;};\n'+restoreBody+'\n}\n'
p=r'''
#include <AWDLSameChannelProbe.hpp>
'''+SLOT+r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
static uint32_t htole32(uint32_t x){return x;}
#define IEEE80211_ADDR_COPY(a,b) memcpy(a,b,6)
#define IWX_FW_CMD_ID_AND_COLOR(a,b) ((a)|((b)<<8))
#define IEEE80211_IS_CHAN_2GHZ(p) ((p)->number<15)
struct ieee80211_channel { unsigned number=149; };
struct Phy { ieee80211_channel *channel=nullptr; };
struct ieee80211_node { Phy *in_phyctxt=nullptr; };
struct iwx_node { Phy *in_phyctxt=nullptr; };
struct ieee80211com { ieee80211_node *ic_bss=nullptr; uint8_t ic_myaddr[6]={0,1,2,3,4,5}; };
struct iwx_softc { ieee80211com sc_ic; int sc_generation=1; };
static unsigned ieee80211_chan2ieee(ieee80211com*,const ieee80211_channel *p){return p->number;}
struct iwx_mac_ctx_cmd { uint32_t id_and_color,action,mac_type,tsf_id; uint8_t node_addr[6],bssid_addr[6];uint32_t cck_rates,ofdm_rates,filter_flags; struct { uint32_t is_assoc,bi,bi_reciprocal,dtim_interval,dtim_reciprocal,listen_interval,assoc_id; } sta; };
enum { IWX_FW_CTXT_ACTION_ADD=1,IWX_FW_CTXT_ACTION_MODIFY=2,IWX_FW_CTXT_ACTION_REMOVE=3,IWX_FW_MAC_TYPE_LISTENER=4,IWX_FW_MAC_TYPE_P2P_DEVICE=6,IWX_FW_MAC_TYPE_P2P_STA=7,IWX_TSF_ID_B=1,IWX_MAC_CONTEXT_CMD=77,kAwdlCoexMacId=1,kCoexBaTxAsking=1,kCoexBaTxAgreed=2 };
enum { IWX_MAC_FILTER_IN_PROMISC=1,IWX_MAC_FILTER_IN_CONTROL_AND_MGMT=2,IWX_MAC_FILTER_ACCEPT_GRP=4,IWX_MAC_FILTER_IN_BEACON=64,IWX_MAC_FILTER_IN_PROBE_REQUEST=4096 };
static bool awdlCoexAddrKnown=true,awdlCoexBaFilterOff=false,awdlCoexBaTxEnabled=true,awdlCoexPromiscLive=false,awdlCoexPromiscUncertain=false;
// -itlslotdiag is off in this harness; the MAC command only notes filter changes when it is on.
static bool awdlSlotDiag=false; static struct { void noteEvent(uint64_t,char,uint8_t){} } awdlSlotDiagState;
static uint32_t awdlCoexToken=1;static unsigned awdlCoexFilter=3;
static uint8_t awdlCoexAddr[6]={2,1,2,3,4,6},etherbroadcastaddr[6]={255,255,255,255,255,255};
static ieee80211_channel own;static ieee80211_channel *awdlCoexOwnChan=&own;
static ItlAWDL::SameChannelProbe awdlCoexSameChannelProbe;
static uint64_t nowUS=1000000;
static uint64_t awdlUptimeUS(){return nowUS;}
static void iwx_ack_rates(iwx_softc*,iwx_node*,int *cck,int *ofdm){*cck=15;*ofdm=21;}
static int injected; static iwx_mac_ctx_cmd submitted;
static bool awdlSchedP2pDev=false,awdlSchedP2pDevFellBack=false; static int awdlSchedP2pDevStatus;
static uint32_t awdlSchedListenerType=IWX_FW_MAC_TYPE_P2P_DEVICE;
static inline uint32_t iwx_reciprocal(uint32_t v){return v?0xffffffffU/v:0;}
static bool controlWantedFlag=false,awdlCoexControlRxRestorePending=false;
static uint64_t controlDeadline=0;
static bool awdlControlReceiveProbeWanted(iwx_softc*,uint64_t now){return controlWantedFlag&&now<controlDeadline;}
class ItlIwx {public:int awdlCoexMacSend(iwx_softc*,uint32_t);int baselineMacSend(iwx_softc*,uint32_t);void runFilterChange(iwx_softc*,bool,bool);int iwx_send_cmd_pdu(iwx_softc *s,int id,int flags,unsigned size,void *cmd){assert(id==IWX_MAC_CONTEXT_CMD && !flags && size==sizeof(iwx_mac_ctx_cmd));submitted=*static_cast<iwx_mac_ctx_cmd*>(cmd);if(injected==2)++s->sc_generation;if(injected==3)++awdlCoexToken;return injected==1?5:0;}};
'''+helper+method+oldmethod+caller+r'''
int main(){
 awdlCoexBaPeerAdded=true;awdlCoexBaTids=1;awdlCoexBaTxState=kCoexBaTxAsking;awdlCoexBaProbeUntilUS=1250000;
 unsigned cases=0;iwx_softc sc;ItlIwx driver;Phy phy;ieee80211_node node;ieee80211_channel station;phy.channel=&station;node.in_phyctxt=&phy;sc.sc_ic.ic_bss=&node;
 // The actual baseline and candidate command builders must emit identical bytes
 // with the flag off, on every combination of original inputs.
 for(unsigned action:{1U,2U,3U})for(unsigned filter=0;filter<4;++filter)for(bool rxoff:{false,true})for(bool same:{false,true})for(bool ba:{false,true})for(bool session:{false,true})for(bool asking:{false,true})for(bool liveWindow:{false,true}){
  injected=0;awdlCoexFilter=filter;awdlCoexBaFilterOff=rxoff;station.number=same?149:48;awdlCoexBaTxEnabled=ba;awdlCoexBaPeerAdded=session;awdlCoexBaTids=session?1:0;awdlCoexBaTxState=asking?1:2;awdlCoexBaProbeUntilUS=liveWindow?nowUS+250000:nowUS;
  awdlCoexSameChannelProbe={};assert(!driver.baselineMacSend(&sc,action));auto old=submitted;assert(!driver.awdlCoexMacSend(&sc,action));assert(!memcmp(&old,&submitted,sizeof(old)));assert(!awdlCoexSameChannelProbe.applied);
  // Enabling the flag may add only the two receive bits, during an active
  // same-channel window, with an established receive session and asking TX.
  awdlCoexSameChannelProbe.enabled=true;assert(!driver.awdlCoexMacSend(&sc,action));bool eligible=action!=3&&filter==3&&!rxoff&&same&&ba&&session&&asking&&liveWindow;
  auto expected=old;if(eligible)expected.filter_flags|=3;assert(!memcmp(&expected,&submitted,sizeof(expected)));assert(awdlCoexSameChannelProbe.applied==eligible);++cases;
 }
 auto reset=[&](){injected=0;awdlCoexFilter=3;awdlCoexBaFilterOff=false;awdlCoexBaTxEnabled=true;awdlCoexBaPeerAdded=true;awdlCoexBaTids=1;awdlCoexBaTxState=1;station.number=149;awdlCoexOwnChan=&own;awdlCoexBaProbeUntilUS=nowUS+250000;awdlCoexSameChannelProbe={};awdlCoexSameChannelProbe.enabled=true;};
 // Country/channel independent: use the actual equal current channels, no149 exception.
 for(unsigned ch:{6U,36U,44U,48U,149U,165U}){reset();own.number=station.number=ch;assert(awdlCoexSameChannelProbeWanted(&sc,nowUS));assert(!driver.awdlCoexMacSend(&sc,2));assert(submitted.filter_flags==7&&awdlCoexSameChannelProbe.applied);++cases;}own.number=149;
 // Every independent closing condition must restore even with FilterOff=false.
 for(unsigned mode=0;mode<7;++mode){reset();assert(!driver.awdlCoexMacSend(&sc,2));assert(awdlCoexSameChannelProbe.applied&&awdlCoexSameChannelProbe.opens==1);
  if(mode==0)nowUS=awdlCoexBaProbeUntilUS;
  if(mode==1)awdlCoexBaTxState=2;
  if(mode==2)awdlCoexBaPeerAdded=false;
  if(mode==3)awdlCoexBaTids=0;
  if(mode==4)awdlCoexSameChannelProbe.enabled=false;
  if(mode==5)awdlCoexBaProbeUntilUS=0;
  if(mode==6)station.number=48;
  assert(!awdlCoexSameChannelProbeWanted(&sc,nowUS));assert(!driver.awdlCoexMacSend(&sc,2));assert(!awdlCoexSameChannelProbe.applied&&awdlCoexSameChannelProbe.closes==1);assert(submitted.filter_flags==(mode==6?7U:4U));++cases;
 }
 reset();injected=1;assert(driver.awdlCoexMacSend(&sc,2)==5);assert(awdlCoexSameChannelProbe.restorePending);nowUS=awdlCoexBaProbeUntilUS;injected=0;driver.runFilterChange(&sc,false,false);assert(!awdlCoexSameChannelProbe.restorePending);
 reset();assert(!driver.awdlCoexMacSend(&sc,2));nowUS+=2000;awdlCoexBaTxState=2;injected=1;assert(driver.awdlCoexMacSend(&sc,2)==5);assert(awdlCoexSameChannelProbe.applied&&awdlCoexSameChannelProbe.restoreFailures==1&&!awdlCoexSameChannelProbe.closes);assert(!awdlCoexSameChannelProbeWanted(&sc,nowUS));injected=0;assert(!driver.awdlCoexMacSend(&sc,2));assert(!awdlCoexSameChannelProbe.applied&&awdlCoexSameChannelProbe.lastUS==2000);
 for(int failure:{1,2,3}){reset();injected=failure;int err=driver.awdlCoexMacSend(&sc,2);assert(err==(failure==1?5:0));assert(!awdlCoexSameChannelProbe.applied&&!awdlCoexSameChannelProbe.opens);++cases;}
 reset();assert(!driver.awdlCoexMacSend(&sc,2));assert(!driver.awdlCoexMacSend(&sc,3));assert(!awdlCoexSameChannelProbe.applied&&!submitted.filter_flags);
 // Boundary checks on the production lease, including wrap/absent channels.
 ItlAWDL::SameChannelProbe lease;lease.enabled=true;
 assert(!lease.wants(0,0,true,true,false,1,0));assert(!lease.wants(149,149,true,true,false,250001,0));assert(lease.wants(149,149,true,true,false,250000,0));assert(!lease.wants(149,149,true,true,false,100,100));assert(lease.wants(149,149,true,true,false,UINT64_MAX,UINT64_MAX-1));assert(!lease.wants(149,149,true,true,false,0,UINT64_MAX-1));
 for(bool oldOff:{false,true})for(bool wanted:{false,true})for(bool force:{false,true})for(bool fail:{false,true}){reset();awdlCoexBaFilterOff=oldOff;injected=fail?1:0;driver.runFilterChange(&sc,wanted,force);assert(awdlCoexBaFilterOff==(fail?oldOff:wanted));assert(awdlCoexBaFilter.off==awdlCoexBaFilterOff);++cases;}
 printf("PASS %u actual baseline/candidate command comparisons, country-independent channels, bounded windows, all restore paths, real restore-caller rollback, restore retry and generation/token failures\n",cases);
}
'''
(T/'test-samech.cpp').write_text(rename(p))
with tempfile.TemporaryDirectory(prefix='forbidden-samech-') as d:
 exe=Path(d)/'test';subprocess.run(['xcrun','clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(T/'test-samech.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

# Normalize only #50's separately tested immediate-ADDBA dispatch before the
# unchanged RX guard pins below. Keep this adaptation outside those pins so
# upstream can update its own RX baselines without replacing our normalization.
rx_start=s.index('iwx_rx_frame(struct')
start=s.index('                        if (request.immediate) {',s.index('ItlAWDL::parseAddbaRequest(cf, coexLen',rx_start))
end=s.index('                    } else if (ItlAWDL::parseDelba',start)
fixture=(T/'fixtures/handoff-before.cpp').read_text()
oldstart=fixture.index('                        if (request.immediate) {')
oldend=fixture.index('                    } else if (ItlAWDL::parseDelba',oldstart)
s=s[:start]+fixture[oldstart:oldend]+s[end:]

# RX parsing/address/reorder guards must be byte-identical to beta.2. The per-peer state's move into
# awdlPeer (peer_slot_names.py) is a rename only, so it's undone before hashing.
s=unrename(s)
import hashlib
# iwx_rx_pkt: the security fixes add only length checks to fixed-size notifications (audit HL05). The AWDL scheduler
# adds a session-protection tap for the follower's own sessions (-itlschedsp): their starts and ends, a failed start
# (status 0) opening no window, each start timed against its plan run (ITLAWDLSchedStartVsRun); and restart counts
# at the init_task sites only with -itlfwcrashrec. The next release adds only the generation of the last successful
# listener session start (awdlSchedStartedGen), which the overlap exceptions wait for. Then a frame-release
# notification for a cleared or unused reorder entry is dropped instead of released (added lines only). -itlschedsecond's
# itlsecondvar 1 notes when the follower's last session ended (awdlSchedSpEndedUS, one added line).
# -itlslotdiag: the session-protection handler also records each plan run's planned and confirmed window and the
# receive filter/station state at its start and end (added lines only; the session bookkeeping is unchanged).
a=s.index('iwx_rx_pkt(struct');b=s.index("\nvoid ItlIwx::",a);assert hashlib.sha256(s[a:b].encode()).hexdigest()=='6413a7f4d3d4d89b364bdbf0b3f51bcb429c399311c0b2ffa00078a18c977589'
# iwx_rx_mpdu_mq: the next release adds only a read-only per-antenna signal tap (ITLAWDLChainRssi) after the RSSI,
# and a read-only count of A-MSDU subframes with AWDL's BSSID inside the existing A-MSDU branch (ITLAWDLAmsduRx).
a=s.index('iwx_rx_mpdu_mq(struct');b=s.index("\nvoid ItlIwx::",a);assert hashlib.sha256(s[a:b].encode()).hexdigest()=='7639048af6c0864698f341a5a8bce663ea86802dca8ac8dea490a3f12f7fa545'
# iwx_rx_frame: 0.6.0 adds only the phone's 5 GHz channel tracking from its TLV 18 sequence (PR #21). The AWDL
# scheduler adds, each behind its own flag: frame counts while the follower runs (-itlschedsp); each peer's election v2
# and sync TLV for the election (-itlschedclock), with the frame's RSSI, band and version (-itlschedelecrssi,
# -itlschedelecapple) at its on-air time (-itlschedonair); a count of AWDL frames heard on 6 and a drop of channel 6's
# promiscuous frames after the AWDL hand-off unless the station is on 6 (-itlschedch6); group AWDL data heard on 6
# passed to the hand-off (-itlawdlch6group), without the schedule-peer timing note; group data per slot
# (-itlschedch6peer); measurements only with -itlsvctiming, -itlschedskew and -itlrxloss. The next release adds only a
# count of received frames by reported channel while the listener is added (ITLAWDLListenerRxChan), read-only. Then
# a peer's DELBA and data are matched to that peer's slot (ItlAWDL::addedSlotFor) instead of the one BA peer: the same
# peer with one slot, the only change without -itlawdlmultipeer. Then an ADDBA answer to our request and a DELBA of our
# send session are matched to the sending peer's slot and its own request count (slot 0's as before). Then a further
# slot's peer's sync frames and data feed that slot's own schedule (added lines only). Then every device's 5 GHz
# AirDrop channel is noted for ITLAWDLPeerChannels (#45, diagnostics only; added lines only). Then a DELBA also finds
# a slot whose station is being added (ownedSlotFor), and only unicast QoS data to us with TID 0-7 keeps a receive
# session alive.
# Mixed-channel listen experiment: confirmed second-channel RX learns peers and delivers data; home-channel
# timing calibration and BA setup remain unchanged. Receive address and length checks are unchanged.
# Step 2b (-itlschedsecondtx only): sync frames heard during an executed second-channel visit also keep the peers'
# schedules (still home presence; timing checked against the second channel's entries, never calibrated there).
# Step 2d (-itlschedsecondsync only): Block Ack requests, teardowns and answers are also accepted when heard during
# an executed second-channel visit (the frame's own channel); the home-channel condition is unchanged.
# -itlawdlamsdutx (A-MSDU send, step A2): our send agreement keeps the answer's A-MSDU bit per slot and clears it on
# a DELBA of that session (added lines only; the answer's matching and every receive check are unchanged).
# Both merged for 0.7.0-alpha.4 (second-channel steps 2b-2d and the A-MSDU send agreement).
# After 0.7.0-alpha.4: while a scan runs, beacons and probe responses are no longer dropped by channel 6's handle
# or the listener's promiscuous receive, so networks on 6 and on the listener's channel reach macOS's scan
# (added condition only; AWDL's hand-off and every receive check are unchanged).
# -itlslotdiag: AWDL action frames and data to us or to a group are counted by plan entry, and unicast to us by the
# receive filter's state (added lines only; every receive check and the hand-off are unchanged).
# ITLAWDLPeerTraffic: a further count, channel and width per slot for bulk data from a known peer (added lines
# only; every receive check and the hand-off are unchanged).
# #119: the scan exception reads macOS's in-progress scan (ic_wcl_scan_requested) instead of the request's type
# (one condition swapped; every receive check and the hand-off are unchanged).
# -itlawdlframedump: a copy of each heard AWDL sync frame is kept for the registry (added lines only; every receive
# check and the hand-off are unchanged).
a=s.index('iwx_rx_frame(struct');b=s.index("\nvoid ItlIwx::",a);assert hashlib.sha256(s[a:b].encode()).hexdigest()=='1699ef85ea8a0ecc8280f6286baa12fa17517359112f1c0fda89392ce69bef47'
