import ctypes as C
import json
import struct
import sys
from pathlib import Path

root=Path(__file__).parent
dll=C.CDLL(str(root/'loader_checks.dll'))
class Stats(C.Structure):
    _fields_=[('vertices',C.c_uint32),('indices',C.c_uint32),('parts',C.c_uint32),('materials',C.c_uint32),
              ('center',C.c_float*3),('radius',C.c_float)]
dll.check_nif.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Stats)]
dll.check_dds.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(C.c_uint32),C.POINTER(C.c_uint32),C.c_void_p]
def nif(raw):
    stats=Stats()
    return bool(dll.check_nif(raw,len(raw),C.byref(stats))),stats
def dds(raw):
    w,h=C.c_uint32(),C.c_uint32(); pixel=(C.c_uint8*4)()
    ok=dll.check_dds(raw,len(raw),C.byref(w),C.byref(h),pixel)
    return bool(ok),(w.value,h.value),tuple(pixel)
def make_dds(fourcc,payload,w=4,h=4,bits=0,masks=(0,0,0,0),pitch=0):
    header=[124,0x1007|(8 if pitch else 0),h,w,pitch,0,1]+[0]*11
    header += [32,4 if fourcc else 0x41,int.from_bytes(fourcc,'little') if fourcc else 0,bits,*masks]
    header += [0x1000,0,0,0,0]
    return b'DDS '+struct.pack('<31I',*header)+payload

fixture_dir=Path(sys.argv[1]) if len(sys.argv)>1 else root/'assets'
manifest=json.loads((fixture_dir/'manifest.json').read_text())
counts={'nif':0,'dds':0}; failures=[]; materials=0
for item in manifest:
    p=Path(item['file']); raw=p.read_bytes()
    if p.suffix=='.nif':
        ok,s=nif(raw); materials+=s.materials
    else:
        ok,dims,pixel=dds(raw)
        ok=ok and 0<max(dims)<=512
    counts[p.suffix[1:]]+=1
    if not ok: failures.append(item['asset'])
assert not failures,failures

color=struct.pack('<HHI',0xf800,0,0)
ok,dim,pixel=dds(make_dds(b'DXT3',bytes([0x33])*8+color))
assert ok and pixel==(255,0,0,51),pixel
ok,dim,pixel=dds(make_dds(b'DXT5',bytes([24,240])+bytes(6)+color))
assert ok and pixel==(255,0,0,24),pixel
ok,dim,pixel=dds(make_dds(b'DXT1',struct.pack('<HHI',0,0,0xffffffff)))
assert ok and pixel[3]==0,pixel
ok,dim,pixel=dds(make_dds(b'',bytes([17,34,51,68]),1,1,32,(255,65280,16711680,4278190080)))
assert ok and pixel==(17,34,51,68),pixel
for fourcc,block in [(b'DXT1',color),(b'DXT3',bytes(8)+color),(b'DXT5',bytes(8)+color)]:
    raw=make_dds(fourcc,block)
    for end in range(len(raw)):
        assert not dds(raw[:end])[0],(fourcc,end)
sample=Path(next(x['file'] for x in manifest if x['file'].endswith('.nif'))).read_bytes()
for end in (0,1,16,40,80,len(sample)//2,len(sample)-64):
    assert not nif(sample[:end])[0],end
report={'fixtures':counts,'material_parts':materials,'synthetic_alpha_and_channel_cases':4,
        'truncated_dds_rejections':sum(128+n for n in (8,16,16)),'truncated_nif_rejections':7,'failures':failures}
(root/'loader_test_results.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
