from pathlib import Path
import importlib.util,subprocess,json,sys
w=Path(__file__).resolve().parent;binary=Path(sys.argv[1]);output=Path(sys.argv[2])
spec=importlib.util.spec_from_file_location('layout',w/'check_allocation_layout.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
f=m.functions(subprocess.check_output(['/usr/bin/otool','-tvV',str(binary)],text=True));names=['__ZN12AirportItlwm22createVirtualInterfaceEP10ether_addrj','__ZNK16ItlAWDLInterface9MetaClass5allocEv'];sizes=[]
for name in names:
 a=m.allocations(f[name]);assert len(a)==1
 assert any('__ZN16ItlAWDLInterfaceC2Ev' in line for line in a[0][1])
 sizes.append(a[0][0])
assert sizes[0]==sizes[1]
r={'class':'ItlAWDLInterface','binary':str(binary.resolve()),'factoryBytes':sizes[0],'metaClassBytes':sizes[1],'status':'pass','association':'Both allocation paths call exact ItlAWDLInterfaceC2 constructor','limits':'One matched allocation invariant; not every field offset or hardware behavior.'}
output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
