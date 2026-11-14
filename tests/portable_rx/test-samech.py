from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text();base=(T/'fixtures/same-channel-before.cpp').read_text()
def body(text,name,end):
 a=text.index(name);b=text.index(end,a);return text[a:b]
helper=body(s,'static bool awdlCoexSameChannelProbeWanted(', '\nstatic bool awdlRxMacMaskValid;')
method='int ItlIwx::\n'+body(s,'awdlCoexMacSend(struct iwx_softc','\n// The listener')
oldmethod='int ItlIwx::\n'+body(base,'awdlCoexMacSend(struct iwx_softc','\n// The listener').replace('awdlCoexMacSend(', 'baselineMacSend(',1)
a=s.index('    if (filterChange) {',s.index('awdlCoexBaHandle(struct'));b=a+len('    if (filterChange) {');depth=1
while depth:
 depth+=(s[b]=='{')-(s[b]=='}');b+=1
restoreBody=s[a:b]
caller='struct { bool off=false; } awdlCoexBaFilter; static unsigned awdlCoexBaFilterChanges;\nvoid ItlIwx::runFilterChange(iwx_softc *sc,bool wanted,bool force){const bool filterWasOff=awdlCoexBaFilterOff;bool filterChange=force||wanted!=filterWasOff;awdlCoexBaFilter.off=wanted;const int gen=sc->sc_generation;auto stillOurs=[&](){return gen==sc->sc_generation;};\n'+restoreBody+'\n}\n'
p=r'''
#include <AWDLSameChannelProbe.hpp>
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
struct iwx_mac_ctx_cmd { uint32_t id_and_color,action,mac_type,tsf_id; uint8_t node_addr[6],bssid_addr[6];uint32_t cck_rates,ofdm_rates,filter_flags; };
enum { IWX_FW_CTXT_ACTION_ADD=1,IWX_FW_CTXT_ACTION_MODIFY=2,IWX_FW_CTXT_ACTION_REMOVE=3,IWX_FW_MAC_TYPE_LISTENER=4,IWX_TSF_ID_B=1,IWX_MAC_CONTEXT_CMD=77,kAwdlCoexMacId=1,kCoexBaTxAsking=1,kCoexBaTxAgreed=2 };
enum { IWX_MAC_FILTER_IN_PROMISC=1,IWX_MAC_FILTER_IN_CONTROL_AND_MGMT=2,IWX_MAC_FILTER_ACCEPT_GRP=4,IWX_MAC_FILTER_IN_BEACON=64,IWX_MAC_FILTER_IN_PROBE_REQUEST=4096 };
static bool awdlCoexAddrKnown=true,awdlCoexBaFilterOff=false,awdlCoexBaTxEnabled=true,awdlCoexPromiscLive=false,awdlCoexPromiscUncertain=false,awdlCoexBaPeerAdded=true;
static uint32_t awdlCoexToken=1;static unsigned awdlCoexFilter=3;static uint8_t awdlCoexBaTids=1;static int awdlCoexBaTxState=kCoexBaTxAsking;
static uint8_t awdlCoexAddr[6]={2,1,2,3,4,6},etherbroadcastaddr[6]={255,255,255,255,255,255};
static ieee80211_channel own;static ieee80211_channel *awdlCoexOwnChan=&own;
static ItlAWDL::SameChannelProbe awdlCoexSameChannelProbe;
static uint64_t nowUS=1000000,awdlCoexBaProbeUntilUS=1250000;
static uint64_t awdlUptimeUS(){return nowUS;}
static void iwx_ack_rates(iwx_softc*,iwx_node*,int *cck,int *ofdm){*cck=15;*ofdm=21;}
static int injected; static iwx_mac_ctx_cmd submitted;
static bool controlWantedFlag=false,awdlCoexControlRxRestorePending=false;
static uint64_t controlDeadline=0;
static bool awdlControlReceiveProbeWanted(iwx_softc*,uint64_t now){return controlWantedFlag&&now<controlDeadline;}
class ItlIwx {public:int awdlCoexMacSend(iwx_softc*,uint32_t);int baselineMacSend(iwx_softc*,uint32_t);void runFilterChange(iwx_softc*,bool,bool);int iwx_send_cmd_pdu(iwx_softc *s,int id,int flags,unsigned size,void *cmd){assert(id==IWX_MAC_CONTEXT_CMD && !flags && size==sizeof(iwx_mac_ctx_cmd));submitted=*static_cast<iwx_mac_ctx_cmd*>(cmd);if(injected==2)++s->sc_generation;if(injected==3)++awdlCoexToken;return injected==1?5:0;}};
'''+helper+method+oldmethod+caller+r'''
int main(){
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
(T/'test-samech.cpp').write_text(p)
with tempfile.TemporaryDirectory(prefix='forbidden-samech-') as d:
 exe=Path(d)/'test';subprocess.run(['xcrun','clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(T/'test-samech.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
# Compare RX processing after removing exactly the separately tested ADDBA note-only delta.
from rx_handoff_delta import without_handoff
s=without_handoff(s)
a=s.index('                // Which 5 GHz channel Apple uses here,')
b=s.index('                if (!(cf[10] & 1) && ourChannel && !fromOwnAddr)',a)
assert s[a:b].count('awdlCoexPeerSequence(cf, coexLen, seq)')==1
assert 'iwx_send' not in s[a:b]
s=s[:a]+s[b:]
for name in ['iwx_rx_pkt(struct','iwx_rx_mpdu_mq(struct','iwx_rx_frame(struct']:
 def methodpart(text):
  a=text.index(name);b=text.index('\nvoid ItlIwx::',a);return text[a:b]
 assert methodpart(base)==methodpart(s),name
assert 'if (awdlCoexBaFilterOff || awdlCoexSameChannelProbe.applied)' in s
assert 'const bool controlRxWanted = awdlControlReceiveProbeWanted' in s
print('PASS inherited RX guards identical excluding separately tested passive regional sequence observation/handoff; production timeout/session-loss and peer-removal restore guards present')
