"""Extract actual channel/known-peer/bulk eligibility helper, no firmware calls."""
from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text()
def function(name):
 a=s.index('static bool '+name+'(');b=s.index('{',a);n=1;e=b+1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
body=function('awdlControlRxQuiet')+'\n'+function('awdlControlReceiveProbeWanted');assert 'iwx_send' not in body
h=r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <cstring>
#include <AWDLControlReceiveProbe.hpp>
struct ieee80211_channel{unsigned number;};struct Phy{ieee80211_channel*channel;};struct iwx_node{Phy*in_phyctxt;};struct IC{void*ic_bss;};struct iwx_softc{IC sc_ic;int sc_generation=7;};
static unsigned ieee80211_chan2ieee(IC*,const ieee80211_channel*c){return c->number;}
static bool enabled,known,awdlStopping,awdlCoexAdded,awdlCoexMultiActive;
static unsigned awdlCoexFilter,awdlCoexToken;static int awdlCoexGeneration;
static uint64_t awdlCoexBaTxLastAggUS,awdlCoexBaLastRxPayloadUS;
static struct{bool bulk;}awdlCoexBaTxBulk;
static ItlAWDL::ControlReceiveProbe awdlCoexControlRxProbe;
static ieee80211_channel*awdlCoexOwnChan;
static bool awdlControlRxEnabled(){return enabled;}
static bool awdlCoexPeerKnown(const uint8_t*,uint64_t){return known;}
'''+body+r'''
int main(){unsigned cases=0;iwx_softc sc;ieee80211_channel own{149},station{149};Phy phy{&station};iwx_node node{&phy};const uint8_t peer[6]={2,3,4,5,6,7};
 auto reset=[&](unsigned leaseChannel=149){sc={{&node},7};enabled=known=awdlCoexAdded=awdlCoexMultiActive=true;awdlStopping=false;awdlCoexFilter=3;awdlCoexToken=9;awdlCoexGeneration=7;awdlCoexBaTxBulk.bulk=false;awdlCoexBaTxLastAggUS=awdlCoexBaLastRxPayloadUS=0;awdlCoexOwnChan=&own;awdlCoexControlRxProbe={};assert(awdlCoexControlRxProbe.arm(peer,10000000,7,9,leaseChannel));};
 for(unsigned flags=0;flags<32;++flags)for(unsigned a:{0U,6U,36U,44U,149U,165U})for(unsigned b:{0U,6U,36U,44U,149U,165U}){reset(a?a:149);own.number=a;station.number=b;enabled=flags&1;known=flags&2;awdlCoexBaTxBulk.bulk=flags&4;awdlCoexBaTxLastAggUS=flags&8?9999999:0;awdlCoexBaLastRxPayloadUS=flags&16?9999999:0;bool expected=flags==3&&a&&a==b;assert(awdlControlReceiveProbeWanted(&sc,10000001)==expected);++cases;}
 own.number=station.number=149;
 for(unsigned guard=0;guard<11;++guard){reset();switch(guard){case 0:awdlStopping=true;break;case 1:awdlCoexAdded=false;break;case 2:awdlCoexMultiActive=false;break;case 3:awdlCoexFilter=1;break;case 4:awdlCoexGeneration=6;break;case 5:sc.sc_generation=8;break;case 6:++awdlCoexToken;break;case 7:sc.sc_ic.ic_bss=nullptr;break;case 8:awdlCoexOwnChan=nullptr;break;case 9:awdlCoexBaLastRxPayloadUS=10000002;break;case 10:awdlCoexBaTxLastAggUS=10000002;break;}assert(!awdlControlReceiveProbeWanted(&sc,10000001));++cases;}
 reset();assert(!awdlControlReceiveProbeWanted(&sc,10250000));assert(!awdlControlReceiveProbeWanted(&sc,9999999));++cases;
 printf("%u production receive-window eligibility cases passed\n",cases);
}
'''
p=T/'test-control-rx-wanted.cpp';p.write_text(h)
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'test';subprocess.run(['/usr/bin/clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
