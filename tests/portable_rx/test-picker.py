"""Compile exact production picker against controlled regulatory and BSS fixtures."""
from pathlib import Path
import subprocess
T=Path(__file__).resolve().parent;R=T.parents[1]
s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text()
def function(name):
 a=s.index(name+'(');a=s.rfind('\n',0,a)+1;b=s.index('{',a);n=1;e=b+1
 while n:
  if s[e]=='{':n+=1
  if s[e]=='}':n-=1
  e+=1
 return s[a:e]
h=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#define IEEE80211_CHAN_MAX 255
#define IEEE80211_CHAN_PASSIVE 0x0200
#define IEEE80211_CHAN_DFS 0x00080000
#define IEEE80211_CHAN_NOADHOC 0x00200000
#define IEEE80211_CHAN_VHT80 0x08000000
#define IEEE80211_IS_CHAN_2GHZ(c) ((c)->number <= 14)
#define IEEE80211_S_RUN 4
#define IEEE80211_CHAN_WIDTH_160 4
#define IEEE80211_CHAN_WIDTH_80P80 5
#define IWX_PHY_VHT_CHANNEL_MODE80 2
#define IWX_PHY_VHT_CHANNEL_MODE20 0
#define isset(a,n) ((a)[n])
struct ieee80211_channel {int ic_freq=0;unsigned ic_flags=0;unsigned number=0;unsigned ic_center_freq2=0;};
struct Node {ieee80211_channel*ni_chan=nullptr;int ni_chw=0;unsigned ni_vht_chan1=0,ni_vht_chan2=0;};
struct ieee80211com {ieee80211_channel ic_channels[256];bool ic_chan_active[256]{},ic_beacon_hint[256]{};int ic_state=0;Node*ic_bss=nullptr;};
struct iwx_softc {ieee80211com sc_ic;int sc_generation=3;};
static uint32_t awdlCoexBeaconHints;
static int awdlCoexWide80FailedGen=-1;
static bool awdlCoexSameBand;
static uint8_t awdlCoexPeer5G,awdlCoexPeerSeq[16];
static uint64_t awdlCoexPeer5GUS,clockUS=1000000000;
static constexpr uint64_t kAwdlCoexPeer5GLifeUS=600000000;
static uint64_t awdlUptimeUS(){return clockUS;}
static unsigned ieee80211_chan2ieee(ieee80211com*,ieee80211_channel*c){return c->number;}
static void awdlCoexPublish(){}
static unsigned ieee80211_mhz2ieee(unsigned f,unsigned){return f>=5000?(f-5000)/5:0;}
'''
h+=function('awdlCoex80BlockFirst')+'\n'
h+=function('awdlCoexNoIr')+'\n'
h+='static int\n'+function('awdlCoexPickOwn')+'\n'
h+=r'''
static iwx_softc sc;static Node bss;
static void reset(){sc={};awdlCoexSameBand=false;sc.sc_generation=3;bss={};awdlCoexWide80FailedGen=-1;awdlCoexPeer5G=44;awdlCoexPeer5GUS=clockUS;memset(awdlCoexPeerSeq,44,16);
 for(unsigned c=0;c<256;++c)sc.sc_ic.ic_channels[c].number=c;
 for(unsigned c: {6U,36U,40U,44U,48U,149U,153U,157U,161U}){sc.sc_ic.ic_channels[c].ic_freq=1;sc.sc_ic.ic_channels[c].ic_flags=IEEE80211_CHAN_VHT80;sc.sc_ic.ic_chan_active[c]=true;}}
static int pick(int width=-1){uint8_t w=99;int c=awdlCoexPickOwn(&sc,&w);if(width>=0)assert(w==width);return c;}
static void denyUpper(){for(unsigned c=149;c<=161;c+=4)sc.sc_ic.ic_chan_active[c]=false;}
int main(){unsigned cases=0;
 reset();assert(pick(2)==149);++cases;
 reset();sc.sc_ic.ic_bss=&bss;bss.ni_chan=&sc.sc_ic.ic_channels[6];awdlCoexSameBand=true;assert(pick(0)==6);++cases;
 awdlCoexSameBand=false;assert(pick(2)==149);++cases;
 reset();denyUpper();assert(pick(2)==44);++cases;
 // Every covered subchannel must pass all independent regulatory permissions.
 for(unsigned block: {36U,149U})for(unsigned c=block;c<=block+12;c+=4)for(unsigned why=0;why<5;++why){reset();if(block==36)denyUpper();auto&ic=sc.sc_ic;
  if(why==0)ic.ic_chan_active[c]=false;
  if(why==1)ic.ic_channels[c].ic_freq=0;
  if(why==2)ic.ic_channels[c].ic_flags|=IEEE80211_CHAN_DFS;
  if(why==3)ic.ic_channels[c].ic_flags|=IEEE80211_CHAN_NOADHOC;
  if(why==4)ic.ic_channels[c].ic_flags|=IEEE80211_CHAN_PASSIVE;
  int result=pick();assert(result!=(int)block || block==36);uint8_t w;int p=awdlCoexPickOwn(&sc,&w);assert(!(p==int(block==36?44:149)&&w==2));++cases;
 }
 reset();denyUpper();sc.sc_ic.ic_channels[36].ic_flags|=IEEE80211_CHAN_PASSIVE;assert(pick(0)==44);++cases;
 sc.sc_ic.ic_beacon_hint[36]=true;assert(pick(2)==44);++cases;
 reset();denyUpper();awdlCoexPeer5GUS=clockUS-kAwdlCoexPeer5GLifeUS;assert(pick(0)==6);++cases;
 reset();denyUpper();awdlCoexPeer5GUS=clockUS+1;assert(pick(0)==6);++cases;
 reset();denyUpper();memset(awdlCoexPeerSeq,6,16);assert(pick(0)==6);++cases;
 reset();awdlCoexWide80FailedGen=3;assert(pick(0)==44);++cases;
 reset();sc.sc_ic.ic_state=IEEE80211_S_RUN;sc.sc_ic.ic_bss=&bss;bss.ni_chan=&sc.sc_ic.ic_channels[149];bss.ni_chw=IEEE80211_CHAN_WIDTH_160;assert(pick(2)==44);++cases;
 // 80+80 can span BOTH candidate blocks; the secondary segment is not the primary group.
 reset();sc.sc_ic.ic_state=IEEE80211_S_RUN;sc.sc_ic.ic_bss=&bss;bss.ni_chan=&sc.sc_ic.ic_channels[149];bss.ni_chw=IEEE80211_CHAN_WIDTH_80P80;bss.ni_vht_chan1=155;bss.ni_vht_chan2=42;bss.ni_chan->ic_center_freq2=5210;
 int c=pick();if(c!=6){fprintf(stderr,"FAIL: 80+80 primary149 secondary42 selected overlapping channel %d\n",c);return 2;}++cases;
 // Effective centre must also cover Extended NSS layouts whose raw VHT seg2 is zero.
 bss.ni_vht_chan2=0;assert(pick(0)==6);++cases;
 // Opposite primary/secondary ordering and non-overlapping secondary.
 reset();sc.sc_ic.ic_state=IEEE80211_S_RUN;sc.sc_ic.ic_bss=&bss;bss.ni_chan=&sc.sc_ic.ic_channels[44];bss.ni_chw=IEEE80211_CHAN_WIDTH_80P80;bss.ni_chan->ic_center_freq2=5775;assert(pick(0)==6);++cases;
 bss.ni_chan->ic_center_freq2=5530;assert(pick(2)==149);++cases;
 printf("%u production picker cases passed\n",cases);
}
'''
h=h.replace('#include <cstring>','#include <cstring>\n#include <initializer_list>')
p=T/'test-picker.cpp';p.write_text(h)
subprocess.run(['/usr/bin/clang++','-std=c++17','-g','-fsanitize=address,undefined',str(p),'-o',str(T/'test-picker')],check=True)
subprocess.run([str(T/'test-picker')],check=True)
