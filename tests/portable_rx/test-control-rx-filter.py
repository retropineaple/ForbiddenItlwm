"""Production task/filter command integration: opening, expiry, failure and retry."""
from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text();p=(T/'test-samech.cpp').read_text().replace('int main(){','static void baselineCases(){')
def balanced(a):
 b=s.index('{',a);n=1;e=b+1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return e
a=s.index('        const bool wantOff = receiveSession &&');b=balanced(s.index('        if (wantOff != awdlCoexBaFilterOff)',a));want=s[a:b]
a=s.index('    const bool controlRxWanted =');b=s.index('    if (filterChange) {',a);control=s[a:b];restore=s[b:balanced(b)]
p=p.replace('void runFilterChange(iwx_softc*,bool,bool);','void runFilterChange(iwx_softc*,bool,bool);void filterTask(iwx_softc*,bool);')
p+='''\nvoid ItlIwx::filterTask(iwx_softc*sc,bool receiveSession){const uint64_t answerNow=awdlUptimeUS();bool filterChange=false;const bool filterWasOff=awdlCoexBaFilterOff;const int gen=sc->sc_generation;const uint32_t tok=awdlCoexToken;auto stillOurs=[&](){return gen==sc->sc_generation&&tok==awdlCoexToken;};\n'''+want+'\n'+control+'\n'+restore+'\n}\n'
p+=r'''
int main(){baselineCases();unsigned cases=0;iwx_softc sc;ItlIwx d;Phy phy;ieee80211_node node;ieee80211_channel station;
 auto reset=[&](){sc.sc_generation=1;own.number=station.number=149;phy.channel=&station;node.in_phyctxt=&phy;sc.sc_ic.ic_bss=&node;awdlCoexToken=1;awdlCoexFilter=3;awdlCoexAddrKnown=true;awdlCoexBaPeerAdded=true;awdlCoexBaTids=1;awdlCoexBaTxEnabled=true;awdlCoexBaTxState=2;awdlCoexSameChannelProbe={};awdlCoexSameChannelProbe.enabled=true;awdlCoexBaFilter={};awdlCoexBaFilter.off=awdlCoexBaFilterOff=true;awdlCoexControlRxRestorePending=false;awdlCoexPromiscLive=awdlCoexPromiscUncertain=false;nowUS=1000000;awdlCoexBaProbeUntilUS=nowUS;controlWantedFlag=true;controlDeadline=nowUS+250000;injected=0;};
 reset();d.filterTask(&sc,true);assert(awdlCoexSameChannelProbe.applied&&awdlCoexPromiscLive&&!awdlCoexBaFilterOff&&awdlCoexControlRxRestorePending);assert(submitted.filter_flags==7);++cases;
 nowUS=controlDeadline-1;d.filterTask(&sc,true);assert(awdlCoexSameChannelProbe.applied&&awdlCoexSameChannelProbe.opens==1&&awdlCoexSameChannelProbe.closes==0);++cases;
 nowUS=controlDeadline;d.filterTask(&sc,true);assert(!awdlCoexSameChannelProbe.applied&&!awdlCoexPromiscLive&&awdlCoexBaFilterOff&&!awdlCoexControlRxRestorePending&&awdlCoexSameChannelProbe.closes==1);assert(submitted.filter_flags==4);++cases;
 // An uncertain open still requires a closing command after expiry, even though applied=false.
 reset();injected=1;d.filterTask(&sc,true);assert(!awdlCoexSameChannelProbe.applied&&awdlCoexPromiscUncertain&&awdlCoexBaFilterOff&&awdlCoexControlRxRestorePending);++cases;
 nowUS=controlDeadline;injected=0;d.filterTask(&sc,true);assert(!awdlCoexPromiscUncertain&&!awdlCoexControlRxRestorePending&&!awdlCoexSameChannelProbe.applied&&submitted.filter_flags==4);++cases;
 // Failed close rolls software state back; the pending obligation and isolation remain until retry succeeds.
 reset();d.filterTask(&sc,true);nowUS=controlDeadline;injected=1;d.filterTask(&sc,true);assert(awdlCoexSameChannelProbe.applied&&awdlCoexPromiscUncertain&&!awdlCoexBaFilterOff&&awdlCoexControlRxRestorePending);++cases;
 injected=0;nowUS+=2000;d.filterTask(&sc,true);assert(!awdlCoexSameChannelProbe.applied&&!awdlCoexPromiscUncertain&&awdlCoexBaFilterOff&&!awdlCoexControlRxRestorePending);++cases;
 reset();d.filterTask(&sc,true);controlWantedFlag=false;nowUS++;d.filterTask(&sc,true);assert(!awdlCoexSameChannelProbe.applied&&!awdlCoexControlRxRestorePending);++cases;
 // The same opening/restoration applies before any BA receive session exists.
 reset();awdlCoexBaPeerAdded=false;awdlCoexBaTids=0;awdlCoexBaFilterOff=awdlCoexBaFilter.off=false;d.filterTask(&sc,false);assert(awdlCoexSameChannelProbe.applied);nowUS=controlDeadline;d.filterTask(&sc,false);assert(!awdlCoexSameChannelProbe.applied&&!awdlCoexControlRxRestorePending);++cases;
 printf("%u production control receive task/filter/restore cases passed\n",cases);
}
'''
f=T/'test-control-rx-filter.cpp';f.write_text(p)
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'test';subprocess.run(['/usr/bin/clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
