"""Exact production TLV parser and country-hint state transition under sanitizers."""
from pathlib import Path
import subprocess
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text()
a=s.index('static bool awdlCoexPeerSequence(');b=s.index('// The first channel',a);parser=s[a:b]
a=s.index('    const bool usableCountry = ItlAWDL::countryNoticeUsable(mcc, source);');b=s.index('    awdlCoexCountryAnswered = mcc;',a);transition=s[a:b]
h=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include "AWDLRegulatory.hpp"
'''+parser+r'''
struct {struct{uint8_t ic_beacon_hint[32];}sc_ic;} hw;
static uint16_t awdlCoexHintsCountry;
static unsigned awdlCoexCountryUnusableAnswers,awdlCoexHintClears;
static void update(uint16_t mcc,uint8_t source){auto*sc=&hw;
'''+transition+r'''
}
int main(){unsigned cases=0;uint8_t out[16];
 assert(!awdlCoexPeerSequence(nullptr,0,out));++cases;
 // Exact-size buffers expose all short headers and payloads to ASan.
 for(unsigned n=0;n<81;++n){std::vector<uint8_t>p(n);if(n>40)p[40]=18;if(n>41)p[41]=38;if(n>42)p[42]=0;
  for(unsigned k=0;k<16;++k)if(n>49+2*k)p[49+2*k]=k%2?44:149;
  memset(out,0xCC,16);bool ok=awdlCoexPeerSequence(p.data(),n,out);assert(ok==(n>=81));if(!ok)for(auto v:out)assert(v==0xCC);++cases;}
 std::vector<uint8_t>p(81);p[40]=18;p[41]=38;
 for(unsigned k=0;k<16;++k)p[49+2*k]=uint8_t(k+1);
 assert(awdlCoexPeerSequence(p.data(),p.size(),out));for(unsigned k=0;k<16;++k)assert(out[k]==k+1);++cases;
 // Claimed TLV length may exceed capture, be too short, or span all uint16 values.
 for(unsigned length=0;length<=65535;++length){p[41]=length&255;p[42]=length>>8;bool ok=awdlCoexPeerSequence(p.data(),p.size(),out);assert(ok==(length==38));++cases;}
 // A prior unrelated TLV must be skipped without changing slot offsets.
 std::vector<uint8_t>q(86);q[40]=7;q[41]=2;memcpy(q.data()+45,p.data()+40,41);q[46]=38;q[47]=0;
 assert(awdlCoexPeerSequence(q.data(),q.size(),out));for(unsigned k=0;k<16;++k)assert(out[k]==k+1);++cases;
 // Preserve upstream behavior: a different usable country (including the first) clears hints.
 // Same-country and unusable/world notices preserve them.
 for(uint16_t old:std::array<uint16_t,4>{0,0x5553,0x4A50,0x434E})
 for(uint16_t next:std::array<uint16_t,7>{0,0x5553,0x4A50,0x434E,0x5A5A,0x7573,0x3031})
 for(unsigned source=0;source<256;++source){awdlCoexHintsCountry=old;memset(hw.sc_ic.ic_beacon_hint,0xAA,32);update(next,source);
  bool usable=source==5&&next!=0x5A5A&&(next>>8)>='A'&&(next>>8)<='Z'&&(next&255)>='A'&&(next&255)<='Z';
  bool clear=usable&&old!=next;assert(awdlCoexHintsCountry==(usable?next:old));
  for(auto v:hw.sc_ic.ic_beacon_hint)assert(v==(clear?0:0xAA));++cases;
 }
 printf("%u production TLV/country cases passed\n",cases);
}
'''
p=T/'test-parser-country.cpp';p.write_text(h)
subprocess.run(['/usr/bin/clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(R/'include'),str(p),'-o',str(T/'test-parser-country')],check=True)
subprocess.run([str(T/'test-parser-country')],check=True)
