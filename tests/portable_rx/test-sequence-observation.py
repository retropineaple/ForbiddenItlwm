"""Test the production passive RX sequence observation and its peer guards."""
from pathlib import Path
import subprocess
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text()
a=s.index('                // Which 5 GHz channel Apple uses here,');b=s.index('                if (!(cf[10] & 1) && ourChannel && !fromOwnAddr)',a);block=s[a:b]
a=s.index('static bool awdlCoexPeerSequence(');b=s.index('// The first channel',a);parser=s[a:b]
h=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
namespace ItlAWDL{bool sameAddress(const uint8_t*a,const uint8_t*b){return !memcmp(a,b,6);}}
static bool awdlCoexBaPeerAdded;
static uint8_t awdlCoexBaPeer[6]{2,3,4,5,6,7},awdlCoexPeerSeq[16],awdlCoexPeer5G;
static uint64_t awdlCoexPeer5GUS;
static unsigned awdlCoexPeer5GFrames,awdlCoexPeer5GChanges,publishes,awdlCoexPeerSeqFrames,awdlCoexPeerSeqChanges;
static void awdlCoexPublish(){++publishes;}
'''+parser+r'''
static void observe(const uint8_t*cf,size_t coexLen,bool fromOwnAddr,uint64_t heardUS){
'''+block+r'''
}
int main(){unsigned cases=0;
 for(unsigned guards=0;guards<16;++guards)for(unsigned kind=0;kind<4;++kind){
  memset(awdlCoexPeerSeq,0,16);awdlCoexPeer5G=awdlCoexPeer5GFrames=awdlCoexPeer5GChanges=publishes=0;awdlCoexPeer5GUS=0;
  uint8_t f[81]{};memcpy(f+10,awdlCoexBaPeer,6);if(guards&1)f[10]|=1;if(guards&2)++f[15];
  awdlCoexBaPeerAdded=guards&4;f[40]=18;f[41]=38;
  for(unsigned n=0;n<16;++n)f[49+2*n]=kind==0?6:kind==1?44:kind==2?149:(n%2?44:149);
  bool allowed=!(guards&1)&&!(guards&8)&&(!(guards&4)||!(guards&2));
  observe(f,sizeof(f),guards&8,100);
  assert(awdlCoexPeer5GFrames==(allowed?1U:0U));assert(awdlCoexPeer5GUS==(allowed?100U:0U));
  assert(awdlCoexPeer5G==(allowed?(kind==0?1:kind==1?44:149):0));
  if(allowed){assert(publishes==1);observe(f,sizeof(f),false,200);assert(publishes==1&&awdlCoexPeer5GUS==200);f[49]=f[49]==149?44:149;observe(f,sizeof(f),false,300);assert(awdlCoexPeer5GChanges==1&&publishes==2);}
  ++cases;
 }
 printf("%u production passive peer-sequence observation cases passed\n",cases);
}
'''
p=T/'test-sequence-observation.cpp';p.write_text(h)
subprocess.run(['/usr/bin/clang++','-std=c++17','-fsanitize=address,undefined',str(p),'-o',str(T/'test-sequence-observation')],check=True)
subprocess.run([str(T/'test-sequence-observation')],check=True)
