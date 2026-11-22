from pathlib import Path
import sys,mmap,struct,json
sys.path.insert(0,'work/visual_review')
from audit_assets import GAME,records
models={}
with (GAME/'FalloutNV.esm').open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as raw:
    for typ,form,flags,group,fs in records(raw):
        d=dict(fs)
        if b'MODL' in d:models[form]=d[b'MODL'].rstrip(b'\0').decode('cp1252')
normalize=lambda s:s.lower().replace('/','\\').removeprefix('meshes\\')
failures=[];count=0
for path in Path('work/fnv_vita_00.04/cells').glob('*.cell'):
    b=path.read_bytes()
    for i in range(struct.unpack_from('<I',b,80)[0]):
        ref,base,model,*_=struct.unpack_from('<II128s7f',b,104+i*164)
        actual=model.split(b'\0')[0].decode('cp1252');expected=models.get(base,'')
        count+=1
        if normalize(actual)!=normalize(expected):failures.append({'cell':path.stem,'ref':hex(ref),'actual':actual,'expected':expected})
report={'verified_model_paths':count,'failures':failures}
Path('work/model_paths004.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2));assert not failures
