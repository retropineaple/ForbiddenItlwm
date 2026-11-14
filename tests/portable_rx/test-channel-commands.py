"""Exercise actual width/control-position/class and final block eligibility helpers."""
from pathlib import Path
import subprocess,re
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text()
def function(name,prefix=''):
 a=s.index(name+'(');a=s.rfind('\n',0,a)+1;b=s.index('{',a);n=1;e=b+1
 while n:
  n+=(s[e]=='{')-(s[e]=='}');e+=1
 return prefix+s[a:e]+'\n'
h=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#define IEEE80211_CHAN_MAX 255
#define IEEE80211_CHAN_PASSIVE 0x0200
#define IEEE80211_CHAN_DFS 0x00080000
#define IEEE80211_CHAN_NOADHOC 0x00200000
#define IEEE80211_CHAN_VHT80 0x08000000
#define IEEE80211_S_ASSOC 3
#define XYLog(...) ((void)0)
#define isset(a,n) ((a)[n])
enum {IEEE80211_CHAN_WIDTH_20_NOHT,IEEE80211_CHAN_WIDTH_20,IEEE80211_CHAN_WIDTH_40,IEEE80211_CHAN_WIDTH_80,IEEE80211_CHAN_WIDTH_160};
struct ieee80211_channel {int ic_freq=0,ic_center_freq1=0;unsigned ic_flags=0,number=0;};
struct Node {ieee80211_channel*ni_chan=nullptr;unsigned ni_chw=0;};
struct ieee80211com {ieee80211_channel ic_channels[256];bool ic_chan_active[256]{},ic_beacon_hint[256]{};int ic_state=0;Node*ic_bss=nullptr;};
static unsigned ieee80211_chan2ieee(ieee80211com*,ieee80211_channel*c){return c->number;}
static bool awdlCoexMultiActive,awdlCoexMovePending,awdlCoexMulti;
static int awdlCoexOwnWidth,awdlFixedChannel,awdlCoexPhyWidth=-1,awdlPhyBuildingId=-1;
static constexpr int kAwdlCoexPhyId=1;
static bool awdlAdvertise80,awdlWide80Live,awdlWide80Refused;
static unsigned awdlCoexAdvertise80Frames,awdlAdvertise80Frames;
static ieee80211_channel*awdlCoexOwnChan;
static bool awdlWide80Applies(ieee80211com*,ieee80211_channel*){return false;}
'''
reg=(R/'itlwm/hal_iwx/if_iwxreg.h').read_text()
for line in reg.splitlines():
 if re.match(r'#define IWX_PHY_VHT_(CTRL_POS_|CHANNEL_MODE)',line):h+=line+'\n'
h+=function('awdlCoex80BlockFirst')+function('awdlCoexNoIr')+function('awdlCoexBlockIneligible')
h+=function('iwx_get_channel_width','static uint8_t\n')+function('iwx_get_ctrl_pos','static uint8_t\n')+function('itlAWDLChannelClass')
h+=r'''
int main(){unsigned cases=0;ieee80211com ic;Node bss;ic.ic_bss=&bss;ic.ic_state=IEEE80211_S_ASSOC;
 for(unsigned ch=0;ch<256;++ch){auto&c=ic.ic_channels[ch];c.number=ch;c.ic_freq=5000+5*ch;c.ic_flags=IEEE80211_CHAN_VHT80;ic.ic_chan_active[ch]=true;}
 for(unsigned base:{36U,149U})for(unsigned k=0;k<4;++k){auto*c=&ic.ic_channels[base+4*k];
  awdlCoexMulti=true;awdlCoexPhyWidth=IWX_PHY_VHT_CHANNEL_MODE80;awdlPhyBuildingId=1;
  assert(iwx_get_channel_width(&ic,c)==IWX_PHY_VHT_CHANNEL_MODE80);
  unsigned expected[]={IWX_PHY_VHT_CTRL_POS_2_BELOW,IWX_PHY_VHT_CTRL_POS_1_BELOW,IWX_PHY_VHT_CTRL_POS_1_ABOVE,IWX_PHY_VHT_CTRL_POS_2_ABOVE};
  assert(iwx_get_ctrl_pos(&ic,c)==expected[k]);++cases;
  // A normal station PHY must use its own negotiated width and centre, not the listener override.
  awdlPhyBuildingId=0;bss.ni_chan=c;bss.ni_chw=IEEE80211_CHAN_WIDTH_20;
  assert(iwx_get_channel_width(&ic,c)==IWX_PHY_VHT_CHANNEL_MODE20);assert(iwx_get_ctrl_pos(&ic,c)==IWX_PHY_VHT_CTRL_POS_1_BELOW);++cases;
 }
 for(unsigned ch:{6U,44U,149U,165U})for(unsigned flags=0;flags<8;++flags){awdlFixedChannel=ch;awdlCoexMultiActive=flags&1;awdlCoexOwnWidth=flags&2?80:20;awdlCoexMovePending=flags&4;
  assert(itlAWDLChannelClass()==((ch==44||ch==149)&&(flags&3)==3&&!(flags&4)?128U:0U));++cases;}
 awdlCoexMovePending=false;awdlCoexOwnWidth=80;
 for(unsigned base:{36U,149U}){awdlCoexOwnChan=&ic.ic_channels[base==36?44:149];assert(!awdlCoexBlockIneligible(&ic,awdlCoexOwnChan));++cases;
  for(unsigned ch=base;ch<=base+12;ch+=4)for(unsigned why=0;why<5;++why){auto old=ic.ic_channels[ch];
   switch(why){case 0:ic.ic_chan_active[ch]=false;break;case 1:ic.ic_channels[ch].ic_freq=0;break;case 2:ic.ic_channels[ch].ic_flags|=IEEE80211_CHAN_PASSIVE;break;case 3:ic.ic_channels[ch].ic_flags|=IEEE80211_CHAN_DFS;break;case 4:ic.ic_channels[ch].ic_flags|=IEEE80211_CHAN_NOADHOC;break;}
   assert(awdlCoexBlockIneligible(&ic,awdlCoexOwnChan));ic.ic_channels[ch]=old;ic.ic_chan_active[ch]=true;++cases;
  }
 }
 awdlCoexMovePending=true;assert(awdlCoexBlockIneligible(&ic,awdlCoexOwnChan));++cases;
 printf("%u production width/position/advertisement/final-permission cases passed\n",cases);
}
'''
p=T/'test-channel-commands.cpp';p.write_text(h)
subprocess.run(['/usr/bin/clang++','-std=c++17','-fsanitize=address,undefined',str(p),'-o',str(T/'test-channel-commands')],check=True)
subprocess.run([str(T/'test-channel-commands')],check=True)
