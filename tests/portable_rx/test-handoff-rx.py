"""Compare actual old/new ADDBA dispatch with feature off, and bounded staging on."""
from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1]
old=(T/'fixtures/handoff-before.cpp').read_text()
new=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text()
def body(s):
 a=s.index('                        if (request.immediate) {',s.index('ItlAWDL::parseAddbaRequest(cf, coexLen'))
 b=s.index('                    } else if (ItlAWDL::parseDelba',a)
 return s[a:b]
prefix=(T/'test-handoff.cpp').read_text().split('int main(){')[0]
prefix=prefix.replace('int gen=7;','int gen=7;int sc_generation=7;')
prefix+='''
namespace ItlAWDL { static bool sameAddress(const uint8_t*a,const uint8_t*b){return !memcmp(a,b,6);} }
static uint8_t awdlCoexBaPeer[6]={2,0,0,0,0,3},awdlCoexBaAnswer[33],awdlCoexBaAnswerTid;
static unsigned awdlCoexBaOtherPeer;
'''
for name,s in [('baseline',old),('candidate',new)]:
 prefix+='static void '+name+'(ItlAWDL::AddbaRequest request){SC*sc=&hardware;\n'+body(s)+'\n}\n'
h=prefix+r'''
struct Result{unsigned other;int answer;bool pending;ItlAWDL::AddbaRequest request;};
static Result result(){Result s{};s.other=awdlCoexBaOtherPeer;s.answer=awdlCoexBaAnswerLength;
 s.pending=awdlCoexBaRequestsPending.take(0,s.request);return s;}
static void setup(bool owner,int pending,bool answer){reset();enabled=false;awdlCoexPeerHandoff.clear();awdlCoexBaPeerAdded=owner;
 awdlCoexBaOtherPeer=0;awdlCoexBaAnswerLength=answer?33:0;awdlCoexBaAnswerTid=0;awdlCoexBaAnswer[26]=7;
 if(pending){auto p=request();if(pending==2)p.peer[5]=9;assert(awdlCoexBaRequestsPending.note(p));}}
int main(){unsigned cases=0;
 for(bool owner:{false,true})for(bool foreign:{false,true})for(bool immediate:{false,true})for(int pending:{0,1,2})
 for(bool answer:{false,true})for(unsigned token:{0U,7U})for(unsigned tid:{0U,7U}){
  auto r=request();r.peer[5]=foreign?4:3;r.immediate=immediate;r.token=token;r.tid=tid;
  setup(owner,pending,answer);baseline(r);auto a=result();
  setup(owner,pending,answer);candidate(r);auto b=result();
  assert(a.other==b.other&&a.answer==b.answer&&a.pending==b.pending&&!awdlCoexPeerHandoff.valid);
  if(a.pending){assert(!memcmp(a.request.peer,b.request.peer,6)&&a.request.token==b.request.token&&a.request.tid==b.request.tid);}
  setup(owner,pending,answer);enabled=true;candidate(r);auto c=result();
  assert(c.other==a.other&&c.answer==a.answer&&c.pending==a.pending);
  assert(awdlCoexPeerHandoff.valid==(owner&&foreign&&immediate));
  if(awdlCoexPeerHandoff.valid)assert(!memcmp(awdlCoexPeerHandoff.request.peer,r.peer,6));
  ++cases;
 }
 printf("PASS %u actual baseline/candidate ADDBA dispatch comparisons; no firmware call in RX\n",cases);
}
'''
(T/'test-handoff-rx.cpp').write_text(h)
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'test'
 subprocess.run(['xcrun','clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(T/'test-handoff-rx.cpp'),'-o',str(exe)],check=True)
 out=subprocess.check_output([str(exe)],text=True);(T/'test-handoff-rx.log').write_text(out);print(out,end='')
