"""Exercise exact production beacon-hint block, including malformed VHT layouts."""
from pathlib import Path
import subprocess
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itl80211/openbsd/net80211/ieee80211_input.c').read_text()
a=s.index('    if (!isprobe && rxi->rxi_chan != 0 && chan == rxi->rxi_chan');b=s.index('\n    \n#ifdef IEEE80211_DEBUG',a);block=s[a:b]
h=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#define IEEE80211_CAPINFO_ESS 1
#define IEEE80211_CHAN_DFS 0x00080000
#define setbit(a,c) ((a)[c]=true)
struct ieee80211_ie_vht_operation {unsigned chan_width,center_freq_seg1_idx,center_freq_seg2_idx;};
struct IC {struct {unsigned ic_flags=0;} ic_channels[256];bool ic_beacon_hint[256]{};};
struct RX {unsigned rxi_chan;};
static void hint(IC*ic,bool isprobe,unsigned chan,unsigned received,unsigned capinfo,const void*vhtopmode){RX rx{received};RX*rxi=&rx;
'''+block+r'''
}
int main(){unsigned cases=0;IC ic;ieee80211_ie_vht_operation op{};
 // Real 80 MHz, new 160 MHz and old 160 MHz layouts cover 36-48.
 for(unsigned layout=0;layout<3;++layout){ic={};op={layout==2?2U:1U,layout==2?50U:42U,layout==1?50U:0U};hint(&ic,false,44,44,1,&op);for(unsigned c=36;c<=48;c+=4)assert(ic.ic_beacon_hint[c]);++cases;}
 // Probe responses, mismatched receive channel, and non-ESS never lift passive restrictions.
 for(unsigned guard=0;guard<4;++guard){ic={};op={1,42,0};if(guard==3)ic.ic_channels[44].ic_flags=IEEE80211_CHAN_DFS;
 hint(&ic,guard==0,44,guard==1?36:44,guard==2?0:1,&op);for(unsigned c=0;c<256;++c)assert(!ic.ic_beacon_hint[c]);++cases;}
 // A sibling flagged DFS must remain restricted even inside a real AP block.
 ic={};ic.ic_channels[40].ic_flags=IEEE80211_CHAN_DFS;op={1,42,0};hint(&ic,false,44,44,1,&op);assert(!ic.ic_beacon_hint[40]&&ic.ic_beacon_hint[36]);++cases;
 // Width 1's segment0=50 is not an 80 MHz block centre. Primary-only hint is still valid.
 ic={};op={1,50,0};hint(&ic,false,44,44,1,&op);
 if(ic.ic_beacon_hint[36]||ic.ic_beacon_hint[40]||ic.ic_beacon_hint[48]){fprintf(stderr,"FAIL: invalid 80 MHz centre50 lifted sibling passive restrictions\n");return 2;}
 assert(ic.ic_beacon_hint[44]);++cases;
 printf("%u production beacon-hint cases passed\n",cases);
}
'''
p=T/'test-beacon-hints.cpp';p.write_text(h)
subprocess.run(['/usr/bin/clang++','-std=c++17','-fsanitize=address,undefined',str(p),'-o',str(T/'test-beacon-hints')],check=True)
subprocess.run([str(T/'test-beacon-hints')],check=True)
