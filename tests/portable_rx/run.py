"""Run extracted production-code regressions on macOS with CLT, no root/network/driver loading."""
from pathlib import Path
import subprocess,sys,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1]
names=['test-picker.py', 'test-beacon-hints.py', 'test-parser-country.py', 'test-sequence-observation.py', 'test-channel-commands.py', 'test-retarget.py', 'test-control-rx.py', 'test-control-rx-wanted.py', 'test-samech.py', 'test-control-rx-filter.py', 'test-control-lifecycle-tx.py', 'test-control-peer.py', 'test-control-handoff.py', 'test-handoff.py', 'test-handoff-rx.py', 'test-single-rate.py']
for name in names:
 print('RUN',name,flush=True);subprocess.run([sys.executable,str(T/name)],check=True)
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'channel-lease'
 subprocess.run(['xcrun','clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(T/'test-channel-lease.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS all portable-rx regression groups')
