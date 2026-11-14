"""Actual production retarget task with firmware waits injected at each boundary."""
from pathlib import Path
import subprocess
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text();a=s.index('void ItlIwx::\nawdlCoexRetarget(');b=s.index('// Adds or removes the probe',a);method=s[a:b]
h=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#define nitems(x) (sizeof(x)/sizeof((x)[0]))
#define IWX_PHY_VHT_CHANNEL_MODE20 0
#define IWX_PHY_VHT_CHANNEL_MODE80 2
#define IWX_FW_CTXT_ACTION_MODIFY 3
struct ieee80211_channel{unsigned number=0;};struct ieee80211_node{};
struct ieee80211com{ieee80211_channel ic_channels[256];ieee80211_node*ic_bss=nullptr;};
struct iwx_phy_ctxt{ieee80211_channel*channel=nullptr;};
struct iwx_softc{ieee80211com sc_ic;iwx_phy_ctxt sc_phyctxt[2];struct{int queued=0;}txq[4];int sc_generation=7;};
static constexpr int kAwdlCoexPhyId=1,kAwdlCoexTxSetupTries=3;
static bool awdlCoexMultiActive,awdlCoexAdded,awdlStopping,awdlCoexMovePending,awdlCoexInFlight,awdlCoexTxHalted,awdlCoexTx;
static int awdlCoexGeneration,awdlCoexAddingGen,awdlCoexRemovingGen,awdlCoexPhyUncertainGen,awdlCoexBrokenGeneration,awdlCoexWide80FailedGen;
static int awdlCoexOwnWidth,awdlCoexQueue,awdlCoexTxSetupTried,awdlCoexPhyWidth,awdlCoexChannel,awdlFixedChannel,awdlCoexRateStep,awdlCoexRateWindowOK,awdlCoexRateWindowFailed;
static unsigned awdlCoexToken,awdlCoexMoveDrainTimeouts,awdlCoexMoveFailures,awdlCoexMoves;
static uint64_t awdlCoexLastMoveUS,awdlCoexMovePendingUS,nowUS;
static const char*awdlCoexMultiState;
static ieee80211_channel*awdlCoexOwnChan;
static struct{unsigned resets=0;void reset(){++resets;}}awdlCoexPeerSchedule;
static int want,width,injectStage,injectAction,teardowns,removes,phys,macs,publishes;
static iwx_softc sc;
static unsigned ieee80211_chan2ieee(ieee80211com*,ieee80211_channel*c){return c->number;}
static uint64_t awdlUptimeUS(){return nowUS;}
static void awdlCoexPublish(){++publishes;}
static int awdlCoexPickOwn(iwx_softc*,uint8_t*w){*w=width;return want;}
static int inject(iwx_softc*s,int stage){if(injectStage!=stage)return 0;switch(injectAction){case 1:return 5;case 2:++s->sc_generation;break;case 3:++awdlCoexToken;break;case 4:awdlCoexAdded=false;++awdlCoexToken;break;case 5:want=6;break;case 6:width=0;break;}return 0;}
static int awdlCoexTxTeardown(iwx_softc*s){++teardowns;return inject(s,1);}
static int awdlCoexBaRemovePeer(iwx_softc*s){++removes;return inject(s,2);}
static int iwx_phy_ctxt_cmd(iwx_softc*s,iwx_phy_ctxt*c,int,int,int action,int){assert(c==&s->sc_phyctxt[1]&&action==3);++phys;return inject(s,3);}
static int awdlCoexMacSend(iwx_softc*s,int action){assert(action==3);++macs;return inject(s,4);}
struct ItlIwx{void awdlCoexRetarget(iwx_softc*);};
'''+method+r'''
static void reset(){sc={};for(unsigned c=0;c<256;++c)sc.sc_ic.ic_channels[c].number=c;sc.sc_generation=7;sc.sc_phyctxt[1].channel=&sc.sc_ic.ic_channels[6];
 awdlCoexMultiActive=awdlCoexAdded=awdlCoexTx=true;awdlStopping=awdlCoexMovePending=awdlCoexInFlight=awdlCoexTxHalted=false;
 awdlCoexGeneration=7;awdlCoexAddingGen=awdlCoexRemovingGen=awdlCoexPhyUncertainGen=awdlCoexBrokenGeneration=awdlCoexWide80FailedGen=-1;
 awdlCoexOwnWidth=20;awdlCoexQueue=1;awdlCoexTxSetupTried=0;awdlCoexPhyWidth=-1;awdlCoexChannel=awdlFixedChannel=6;
 awdlCoexRateStep=awdlCoexRateWindowOK=awdlCoexRateWindowFailed=5;awdlCoexToken=9;awdlCoexMoveDrainTimeouts=awdlCoexMoveFailures=awdlCoexMoves=0;
 awdlCoexLastMoveUS=awdlCoexMovePendingUS=0;nowUS=30000000;awdlCoexMultiState="initial";awdlCoexOwnChan=sc.sc_phyctxt[1].channel;awdlCoexPeerSchedule.resets=0;
 want=44;width=2;injectStage=injectAction=teardowns=removes=phys=macs=publishes=0;
}
int main(){unsigned cases=0;ItlIwx d;
 for(int target:{6,44,149})for(int w:{0,2}){reset();want=target;width=w;sc.sc_phyctxt[1].channel=&sc.sc_ic.ic_channels[target==6?149:6];d.awdlCoexRetarget(&sc);assert(phys==1&&macs==1&&awdlCoexMoves==1&&!awdlCoexMovePending);assert(awdlCoexChannel==target&&awdlFixedChannel==target&&awdlCoexOwnWidth==(w==2?80:20));assert(awdlCoexPhyWidth==-1);++cases;}
 for(int guard=0;guard<9;++guard){reset();switch(guard){case 0:awdlCoexMultiActive=false;break;case 1:awdlCoexAdded=false;break;case 2:awdlCoexGeneration=6;break;case 3:awdlCoexAddingGen=7;break;case 4:awdlCoexRemovingGen=7;break;case 5:awdlCoexPhyUncertainGen=7;break;case 6:awdlCoexBrokenGeneration=7;break;case 7:awdlStopping=true;break;case 8:want=0;break;}d.awdlCoexRetarget(&sc);assert(!removes&&!phys&&!macs);++cases;}
 for(int target:{44,149}){reset();want=target;awdlCoexLastMoveUS=nowUS-9999999;d.awdlCoexRetarget(&sc);assert(!phys&&!removes);nowUS++;d.awdlCoexRetarget(&sc);assert(phys==1&&macs==1);++cases;}
 reset();want=6;sc.sc_phyctxt[1].channel=&sc.sc_ic.ic_channels[149];awdlCoexLastMoveUS=nowUS-1;d.awdlCoexRetarget(&sc);assert(phys==1);++cases;
 reset();awdlCoexInFlight=true;d.awdlCoexRetarget(&sc);assert(!phys&&!teardowns&&awdlCoexMovePending);nowUS+=1999999;d.awdlCoexRetarget(&sc);assert(!teardowns);nowUS++;d.awdlCoexRetarget(&sc);assert(teardowns==1&&phys==1&&awdlCoexTxHalted&&awdlCoexTxSetupTried==3);++cases;
 for(int stage=1;stage<=4;++stage)for(int action=1;action<=4;++action){reset();injectStage=stage;injectAction=action;if(stage==1){awdlCoexInFlight=true;awdlCoexMovePending=true;awdlCoexMovePendingUS=nowUS-2000000;}
  d.awdlCoexRetarget(&sc);
  if(stage<3)assert(!phys&&!macs);
  if(stage==3)assert(phys==1&&!macs);
  if(stage==4)assert(phys==1&&macs==1);
  if(action==1&&stage==1)assert(awdlCoexBrokenGeneration==7&&awdlCoexTxHalted);
  if(action==1&&stage==3)assert(awdlCoexPhyUncertainGen==7&&awdlCoexTxHalted);
  if(action==1&&stage==4)assert(awdlCoexTxHalted);
  ++cases;
 }
 // A firmware wait which changes map/width must cancel the decision before PHY submission.
 for(int action:{5,6}){reset();injectStage=2;injectAction=action;d.awdlCoexRetarget(&sc);assert(removes==1&&!phys&&!awdlCoexMovePending);++cases;}
 printf("%u production retarget task cases passed\n",cases);
}
'''
p=T/'test-retarget.cpp';p.write_text(h)
subprocess.run(['/usr/bin/clang++','-std=c++17','-fsanitize=address,undefined',str(p),'-o',str(T/'test-retarget')],check=True)
subprocess.run([str(T/'test-retarget')],check=True)
