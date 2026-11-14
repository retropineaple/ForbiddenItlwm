from pathlib import Path
import re,subprocess,tempfile,json
T=Path(__file__).resolve().parent;R=T.parents[1]
new=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text();old=(T/'fixtures/single-rate-before.cpp').read_text();reg=(R/'itlwm/hal_iwx/if_iwxreg.h').read_text()
def branch(s):
 start=s.index('iwx_tx_fill_cmd(struct iwx_softc');a=s.index('    if (awdlCoexSending) {',start);b=s.index(' else if (awdlUnicastData',a)
 footer=s[s.index('    rinfo = &iwx_rates[ridx];',b):s.index('\nvoid ItlIwx::\niwx_tx_update_byte_tbl',b)]
 return s[a:b]+ '\n'+footer
macros=[];lines=reg.splitlines();i=0
while i<len(lines):
 line=lines[i];i+=1
 if re.match(r'#define IWX_RATE_',line):
  while line.endswith('\\'):line+='\n'+lines[i];i+=1
  macros.append(line)
a=reg.rfind('enum {',0,reg.index('IWX_RATE_1M_INDEX = 0'));b=reg.index('};',a)+2
prefix=r'''
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cerrno>
#include <initializer_list>
#define nitems(x) (sizeof(x)/sizeof((x)[0]))
#define IEEE80211_IS_MULTICAST(p) ((p)[0]&1)
#define IEEE80211_FC0_TYPE_DATA 8
#define IEEE80211_M_MONITOR 6
#define IEEE80211_RATE_VAL 127
#define IWX_TX_FLAGS_CMD_RATE 1
#define IWX_LONG_GROUP 0
#define IWX_TX_CMD 0
#define IWX_FW_CMD_VER_UNKNOWN 255
#define IWX_RIDX_IS_CCK(x) ((x)<IWX_FIRST_OFDM_RATE)
#define XYLog(...) ((void)0)
'''+ '\n'.join(macros)+'\n'+reg[a:b]+r'''
struct ieee80211_frame { uint8_t i_addr1[6]{}; };
struct ieee80211com { int ic_opmode=0; };
struct iwx_softc {};
struct ieee80211_node {};
struct ieee80211_rateset {};
struct iwx_rate { int rate=48; uint8_t plcp=9; };
static iwx_rate iwx_rates[IWX_RATE_COUNT]{};
static unsigned iwx_mcs2ridx[16]{};
static int commandVersion=10,antennas=3;
static unsigned awdlCoexRateStep,awdlCoexOwnWidth,awdlCoexVhtFrames;
static bool awdlCoexSending,awdlCoexBaSendingAgg,awdlCoexSingleLegacyUsed;
static int awdlCoexSingleLegacy;
static bool awdlSingleLegacyEnabled(){return __atomic_load_n(&awdlCoexSingleLegacy,__ATOMIC_RELAXED)==1;}
static const uint8_t kAwdlCoexRateMcs[6]={0,4,5,7,4,7};
static uint32_t htole32(uint32_t n){return n;}
static int iwx_lookup_cmd_ver(iwx_softc*,int,int){return commandVersion;}
static int iwx_fw_valid_tx_ant(iwx_softc*){return antennas;}
static int iwx_get_tx_ant(iwx_softc*,ieee80211_node*,const iwx_rate*,int,ieee80211_frame*){return IWX_RATE_MCS_ANT_A_MSK;}
static int iwx_rate2idx(int x){return x/2;}
static int iwx_ridx2rate(ieee80211_rateset*,int x){return iwx_rates[x].rate;}
static int ieee80211_min_basic_rate(ieee80211com*){return 12;}
'''
def function(name,s):
 return 'static const iwx_rate *'+name+r'''(int type,bool group,uint32_t *flags,uint32_t *rate_n_flags){iwx_softc soft;auto*sc=&soft;ieee80211com com;auto*ic=&com;ieee80211_node node;auto*ni=&node;ieee80211_rateset rates;auto*rs=&rates;ieee80211_frame frame;auto*wh=&frame;frame.i_addr1[0]=group;int ridx=IWX_RATE_24M_INDEX,rate_flags=0,min_ridx=IWX_RATE_6M_INDEX;uint8_t cmdver=0,hwrate=0;const iwx_rate*rinfo=nullptr;*flags=0;awdlCoexSingleLegacyUsed=false;
if(awdlCoexBaSendingAgg)return &iwx_rates[IWX_RATE_MCS_7_INDEX];
if(!awdlCoexSending)return nullptr;
'''+branch(s)+'\n'
handler=new[new.index('static int awdlSingleLegacySysctl SYSCTL_HANDLER_ARGS'):new.index('\nSYSCTL_PROC(',new.index('static int awdlSingleLegacySysctl SYSCTL_HANDLER_ARGS'))]
start=new.index('                if (kind == 2) {',new.index('The status belongs to the descriptor'))
end=new.index(' else if (kind == 1)',start)
completion=new[start:end]
prefix+=r'''
static uint32_t awdlCoexUcastOK,awdlCoexUcastFailed,awdlCoexRateWindowOK,awdlCoexRateWindowFailed,awdlCoexRateUps,awdlCoexRateDowns;
static uint32_t awdlCoexSingleStatus[2][256];static uint8_t awdlCoexSlotSingleLegacy[256];
#define IWX_TX_STATUS_MSK 255
'''
harness=prefix+function('baseline',old)+function('candidate',new)+r'''
struct sysctl_oid{};struct sysctl_req{void*newptr;int value;int error;};
#define SYSCTL_HANDLER_ARGS (sysctl_oid*oidp,void*arg1,int arg2,sysctl_req*req)
static int sysctl_handle_int(sysctl_oid*,int*p,int,sysctl_req*r){if(r->error)return r->error;if(r->newptr)*p=r->value;return 0;}
'''+handler+'\nstatic void complete(unsigned slot,unsigned status,bool ok){int kind=2;\n'+completion+'\n}\n'+r'''
int main(){
 for(unsigned i=0;i<16;++i)iwx_mcs2ridx[i]=i;
 unsigned cases=0;
 for(unsigned step=0;step<8;++step)for(unsigned width:{20U,80U})for(int version:{7,10,255})for(int ant:{1,3})for(bool sending:{false,true})for(bool agg:{false,true})for(bool group:{false,true})for(int type:{0,8})for(int mode:{0,1}){
  commandVersion=version;antennas=ant;awdlCoexOwnWidth=width;awdlCoexSending=sending;awdlCoexBaSendingAgg=agg;awdlCoexSingleLegacy=mode;
  uint32_t af=0,ar=0,bf=0,br=0;awdlCoexRateStep=step;auto a=baseline(type,group,&af,&ar);auto oldstep=awdlCoexRateStep;
  awdlCoexRateStep=step;auto b=candidate(type,group,&bf,&br);auto used=awdlCoexSingleLegacyUsed;
  bool eligible=mode&&sending&&!agg&&!group&&type==8;
  if(!eligible){assert(a==b&&af==bf&&ar==br&&awdlCoexRateStep==oldstep);assert(!used);}
  else{assert(b==&iwx_rates[IWX_RATE_24M_INDEX]&&used&&awdlCoexRateStep==step);awdlCoexRateStep=0;uint32_t cf=0,cr=0;assert(b==baseline(type,group,&cf,&cr));assert(bf==cf&&br==cr);}
  ++cases;
 }

 // Actual descriptor completion code: changing the control while a packet is in flight
 // cannot reclassify that packet or train adaptation with the fixed-rate sample.
 for(unsigned mode=0;mode<2;++mode)for(unsigned slot:{0U,255U})for(unsigned status=0;status<256;++status){
  for(auto &row:awdlCoexSingleStatus)for(auto &n:row)n=0;
  awdlCoexSlotSingleLegacy[slot]=mode;awdlCoexSingleLegacy=1-mode;
  awdlCoexRateStep=3;awdlCoexRateWindowOK=10;awdlCoexRateWindowFailed=2;
  bool ok=status==1;complete(slot,status,ok);
  assert(awdlCoexSingleStatus[mode][status]==1&&awdlCoexSingleStatus[1-mode][status]==0);
  assert(awdlCoexRateStep==3);
  assert(awdlCoexRateWindowOK==10+unsigned(!mode&&ok));
  assert(awdlCoexRateWindowFailed==2+unsigned(!mode&&!ok));++cases;
 }
 // Run the actual production sysctl handler, including error/no-write paths.
 sysctl_oid oid;sysctl_req req{nullptr,1,0};awdlCoexSingleLegacy=0;
 assert(!awdlSingleLegacySysctl(&oid,nullptr,0,&req)&&awdlCoexSingleLegacy==0);
 req.newptr=&req;for(int v:{0,1}){req.value=v;assert(!awdlSingleLegacySysctl(&oid,nullptr,0,&req)&&awdlCoexSingleLegacy==v);++cases;}
 for(int v:{-1,2,INT32_MAX,INT32_MIN}){req.value=v;assert(awdlSingleLegacySysctl(&oid,nullptr,0,&req)==EINVAL&&awdlCoexSingleLegacy==1);++cases;}
 req.value=0;req.error=EFAULT;assert(awdlSingleLegacySysctl(&oid,nullptr,0,&req)==EFAULT&&awdlCoexSingleLegacy==1);
 printf("PASS %u actual production rate-builder and sysctl-handler cases\n",cases);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'check.cpp';p.write_text(harness);exe=Path(d)/'check'
 subprocess.run(['xcrun','clang++','-std=c++14','-fsanitize=address,undefined','-g',str(p),'-o',str(exe)],check=True)
 result=subprocess.check_output([str(exe)],text=True);print(result,end='');(T/'test-single-rate.log').write_text(result)
