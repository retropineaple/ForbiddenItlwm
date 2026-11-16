#!/usr/bin/env python3
"""Read-only allowlisted IORegistry sampler for the requested 149/149 A/B.
No capture, driver setting, service restart, or raw registry/peer output.
"""
import argparse,datetime,json,plistlib,struct,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--label',required=True,choices=['beta2-baseline','same-channel-on','diagnostic-only']);p.add_argument('--seconds',type=int,default=60);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
if not 1<=a.seconds<=300:p.error('--seconds must be 1..300')
keys=['ITLAWDLCoexChannel','ITLAWDLCoexOwnWidth','ITLAWDLCoexBaTxStateName','ITLAWDLCoexBaTxAsks','ITLAWDLCoexBaTxAnswers','ITLAWDLCoexBaTxAgreed','ITLAWDLCoexBaActionSeen','ITLAWDLCoexSameChannelProbeEnabled','ITLAWDLCoexSameChannelProbeLive','ITLAWDLCoexSameChannelProbeOpens','ITLAWDLCoexSameChannelProbeCloses','ITLAWDLCoexSameChannelProbeRestoreFailures']
rows=[];begin=time.monotonic()
try:
 while True:
  tree=plistlib.loads(subprocess.check_output(['ioreg','-a','-l','-w0','-d1']))
  root=tree[0] if isinstance(tree,list) else tree
  if not isinstance(root,dict):raise ValueError('Unexpected IORegistry archive shape')
  row={'elapsedSeconds':round(time.monotonic()-begin,3)}
  for k in keys:
   if k not in root:continue
   v=root[k]
   if k=='ITLAWDLCoexBaActionSeen' and isinstance(v,bytes) and len(v)==16:v=list(struct.unpack('<4I',v))
   if isinstance(v,(str,int,float,bool,list)):row[k]=v
  rows.append(row)
  if time.monotonic()-begin>=a.seconds:break
  time.sleep(min(1,max(0,a.seconds-(time.monotonic()-begin))))
except KeyboardInterrupt:pass
finally:
 a.out.write_text(json.dumps({'label':a.label,'timeUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'samples':rows,'limits':'Read-only registry samples; file completion, AP channel, speed and transfer path require separate evidence.'},indent=2)+'\n')
print('Saved',len(rows),'allowlisted samples to',a.out)
