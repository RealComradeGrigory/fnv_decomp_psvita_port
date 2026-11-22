import sys,struct,json,mmap,zlib
from pathlib import Path
import numpy as np
import argparse
parser=argparse.ArgumentParser(description="Bake original idle and weapon-ready poses from your local Fallout New Vegas assets; requires NumPy.")
parser.add_argument('--game-data',required=True,type=Path)
parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'assets')
args=parser.parse_args();GAME=args.game_data;OUT=args.output;OUT.mkdir(parents=True,exist_ok=True)
class Archive:
    def __init__(self, path):
        self.file = path.open('rb')
        magic, version, offset, self.flags, folders, files, folder_names, file_names, _ = struct.unpack('<4s8I', self.file.read(36))
        assert magic == b'BSA\0' and version == 104
        self.file.seek(offset)
        records = [struct.unpack('<QII', self.file.read(16)) for _ in range(folders)]
        entries = []
        for _, count, _ in records:
            length = self.file.read(1)[0]
            folder = self.file.read(length).rstrip(b'\0').decode('ascii').lower()
            for _ in range(count):
                _, size, position = struct.unpack('<QII', self.file.read(16))
                entries.append((folder, size, position))
        names = self.file.read(file_names).split(b'\0')
        self.entries = {folder + '\\' + name.decode('ascii').lower(): (size, position)
                        for (folder, size, position), name in zip(entries, names)}

    def extract(self, path):
        size, position = self.entries[path.lower()]
        self.file.seek(position)
        raw = self.file.read(size & 0x3fffffff)
        if self.flags & 0x100:
            raw = raw[1 + raw[0]:]
        if bool(self.flags & 4) != bool(size & 0x40000000):
            expected, = struct.unpack_from('<I', raw)
            raw = zlib.decompress(raw[4:])
            assert len(raw) == expected
        return raw


def blocks(raw):
    p=raw.index(b'\n')+1
    version,endian,user,n,bs=struct.unpack_from('<IBIII',raw,p);p+=17
    for _ in range(3): p+=1+raw[p]
    count=struct.unpack_from('<H',raw,p)[0];p+=2; types=[]
    for _ in range(count):
        size=struct.unpack_from('<I',raw,p)[0];p+=4;types.append(raw[p:p+size].decode());p+=size
    ids=struct.unpack_from('<'+'H'*n,raw,p);p+=2*n
    sizes=struct.unpack_from('<'+'I'*n,raw,p);p+=4*n
    count,maxlen=struct.unpack_from('<II',raw,p);p+=8;strings=[]
    for _ in range(count):
        size=struct.unpack_from('<I',raw,p)[0];p+=4;strings.append(raw[p:p+size].decode(errors='replace'));p+=size
    groups=struct.unpack_from('<I',raw,p)[0];p+=4+4*groups
    out=[]
    for t,size in zip(ids,sizes):out.append((types[t&32767],raw[p:p+size]));p+=size
    return bs,strings,out

a=Archive(GAME/'Fallout - Meshes.bsa')
def mat(r,t,s=1):
 m=np.eye(4);m[:3,:3]=np.array(r).reshape(3,3)*s;m[:3,3]=t;return m
def trs(b,p,skin=False):
 f=struct.unpack_from('<13f',b,p)
 return mat(f[:9],f[9:12],f[12]) if skin else mat(f[3:12],f[:3],f[12])
def quat(q):
 w,x,y,z=q/np.linalg.norm(q)
 return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
class Nif:
 def __init__(self,raw):
  self.raw=raw;self.bs,self.strings,self.blocks=blocks(raw);self.nodes={};self.parents={};self.starts=[]
  # Block payloads are a contiguous suffix followed by root refs. Locate with the first block.
  total=sum(len(b) for t,b in self.blocks);first=self.blocks[0][1];start=raw.find(first,raw.index(b'\n')+50)
  assert start>=0 and b''.join(b for t,b in self.blocks)==raw[start:start+total]
  for i,(typ,b) in enumerate(self.blocks):
   self.starts.append(start);start+=len(b)
   if typ not in ['NiNode','BSFadeNode','NiTriShape','NiTriStrips','BSFaceGenNiNode']:continue
   ni,extra=struct.unpack_from('<iI',b);p=12+extra*4;flag=struct.unpack_from('<I',b,p)[0];p+=4
   local=trs(b,p);p+=52
   count=struct.unpack_from('<I',b,p)[0];p+=4;props=struct.unpack_from('<'+'i'*count,b,p);p+=4*count+4
   node=dict(name=self.strings[ni] if ni>=0 else '',local=local,props=props,hidden=flag&1)
   if typ in ['NiTriShape','NiTriStrips']:node['geom'],node['skin']=struct.unpack_from('<ii',b,p)
   else:
    count=struct.unpack_from('<I',b,p)[0];p+=4
    for child in struct.unpack_from('<'+'i'*count,b,p):
     if child>=0:self.parents[child]=i
   self.nodes[i]=node
 def worlds(self,pose=None):
  result={}
  def get(i):
   if i not in result:
    n=self.nodes[i];local=n['local'].copy()
    if pose and n['name']=='Bip01' and 'Bip01 NonAccum' in pose and 'Bip01' not in pose:local=np.eye(4)
    if pose and n['name'] in pose:
     t,q,s=pose[n['name']]
     if np.isfinite(t).all() and max(abs(t))<1e6:local[:3,3]=t
     if np.isfinite(q).all() and max(abs(q))<1e6:local[:3,:3]=quat(q)
     if np.isfinite(s) and abs(s)<1e6:local[:3,:3]*=s
    result[i]=get(self.parents[i])@local if i in self.parents else local
   return result[i]
  for i in self.nodes:get(i)
  return result

def pose(raw):
 bs,ss,bb=blocks(raw);b=bb[0][1];n=struct.unpack_from('<I',b,4)[0];out={}
 for i in range(n):
  inter,controller,priority,name,prop,ctype,cid,iid=struct.unpack_from('<iiB5i',b,12+i*29)
  if inter<0:continue
  typ,x=bb[inter]
  if typ=='NiBSplineCompTransformInterpolator':
   t0,t1,di,bi=struct.unpack_from('<ffii',x);values=np.array(struct.unpack_from('<8f',x,16));t,q,s=values[:3],values[3:7],values[7]
   handles=struct.unpack_from('<3I',x,48);ranges=struct.unpack_from('<6f',x,60)
   db=bb[di][1];nf=struct.unpack_from('<I',db)[0];p=4+nf*4;nc=struct.unpack_from('<I',db,p)[0];cp=np.frombuffer(db,dtype='<i2',count=nc,offset=p+4)
   for k,(h,dims) in enumerate(zip(handles,[3,4,1])):
    if h==65535:continue
    v=cp[h:h+dims].astype(float)/32767*ranges[k*2+1]+ranges[k*2]
    if k==0:t=v
    elif k==1:q=v
    else:s=v[0]
  elif typ=='NiTransformInterpolator':
   values=np.array(struct.unpack_from('<8f',x));t,q,s=values[:3],values[3:7],values[7];ref=struct.unpack_from('<i',x,32)[0]
   if ref>=0:
    kd=bb[ref][1];p=0
    def keys(dims,rot=False):
     nonlocal p
     n=struct.unpack_from('<I',kd,p)[0];p+=4
     if not n:return None
     kind=struct.unpack_from('<I',kd,p)[0];p+=4
     if rot and kind==4:
      angles=[keys(1)[0] for _ in range(3)]
      def mul(a,b):
       w,x,y,z=a;v,i,j,k=b;return np.array([w*v-x*i-y*j-z*k,w*i+x*v+y*k-z*j,w*j-x*k+y*v+z*i,w*k+x*j-y*i+z*v])
      q=np.array([1.,0,0,0])
      for axis,angle in enumerate(angles):
       v=np.zeros(4);v[0]=np.cos(angle/2);v[axis+1]=np.sin(angle/2);q=mul(q,v)
      return q
     v=np.array(struct.unpack_from('<'+'f'*dims,kd,p+4));stride=4+dims*4
     if kind==2 and not rot:stride+=dims*8
     elif kind==3:stride+=12
     assert kind in [1,2,3]
     p+=stride*n;return v
    v=keys(4,True)
    if v is not None:q=v
    v=keys(3)
    if v is not None:t=v
    v=keys(1)
    if v is not None:s=v[0]
  else:continue
  out[ss[name]]=(t,q,s)
 return out

skel=Nif(a.extract('meshes\\characters\\_male\\skeleton.nif'));idle=pose(a.extract('meshes\\characters\\_male\\locomotion\\mtidle.kf'))
w=skel.worlds(idle);posed={skel.nodes[i]['name']:v for i,v in w.items()}
print('Idle pose bones',len(posed))
report=[]
def bake(path,name,poseworld=posed,output_transform=None):
 raw=a.extract(path);n=Nif(raw);world=n.worlds();out=bytearray(raw);worst=0;weighted=0
 for i,node in n.nodes.items():
  if 'geom' not in node or node['skin']<0:continue
  typ,b=n.blocks[node['geom']];num=struct.unpack_from('<H',b,4)[0];verts=np.frombuffer(b,dtype='<f4',count=num*3,offset=9).reshape(-1,3).copy()
  user=struct.unpack_from('<I',raw,raw.index(b'\n')+6)[0];np0=9+num*12+2+(4 if user>=12 else 0);hasnorm=b[np0];np0+=1
  norms=np.frombuffer(b,dtype='<f4',count=num*3,offset=np0).reshape(-1,3).copy() if hasnorm else None
  skin=n.blocks[node['skin']][1];data,part,root,count=struct.unpack_from('<4i',skin);bones=struct.unpack_from('<'+'i'*count,skin,16);sd=n.blocks[data][1];st=trs(sd,0,True);ns=struct.unpack_from('<I',sd,52)[0];assert ns==count and sd[56]==1
  p=57;pv=np.zeros((num,3));pn=np.zeros((num,3));sums=np.zeros(num);bind=np.zeros((num,3))
  for bone in bones:
   offset=trs(sd,p,True);p+=52+16;nv=struct.unpack_from('<H',sd,p)[0];p+=2
   bn=n.nodes[bone]['name'];bw=world[bone];pw=poseworld.get(bn,bw)
   transform=pw@offset;bt=bw@offset

   for j in range(nv):
    idx,weight=struct.unpack_from('<Hf',sd,p);p+=6;assert idx<num
    pv[idx]+=weight*(transform[:3,:3]@verts[idx]+transform[:3,3]);sums[idx]+=weight
    bind[idx]+=weight*(bt[:3,:3]@verts[idx]+bt[:3,3])
    if norms is not None:pn[idx]+=weight*(transform[:3,:3]@norms[idx])
  assert np.allclose(sums,1,atol=.015), (path,sums.min(),sums.max())
  expected=verts@world[i][:3,:3].T+world[i][:3,3]
  err=np.max(np.linalg.norm(bind-expected,axis=1));worst=max(worst,float(err))
  assert err<.05,(path,err)
  if output_transform is not None:
   pv=pv@output_transform[:3,:3].T+output_transform[:3,3]
   pn=pn@output_transform[:3,:3].T
  inv=np.linalg.inv(world[i])
  local=pv@inv[:3,:3].T+inv[:3,3]
  start=n.starts[node['geom']];out[start+9:start+9+num*12]=local.astype('<f4').tobytes()
  if norms is not None:
   nn=pn@np.linalg.inv(world[i][:3,:3]).T;nn/=np.maximum(np.linalg.norm(nn,axis=1)[:,None],1e-8)
   out[start+np0:start+np0+num*12]=nn.astype('<f4').tobytes()
  weighted+=num
 (OUT/name).write_bytes(out);report.append(dict(source=path,output=name,vertices=weighted,bind_reconstruction_error=worst));print(report[-1]);return n
for path,name in [('armor\\republicans\\republican_02.nif','doc_body.nif'),('characters\\_male\\righthand.nif','doc_right.nif'),('characters\\_male\\lefthand.nif','doc_left.nif')]:bake('meshes\\'+path,name)
(OUT/'pose_audit.json').write_text(json.dumps(report,indent=2))

fp=Nif(a.extract('meshes\\characters\\_1stperson\\skeleton.nif'))
for index,attack,grip in [(0,'1hpattackright.kf','1hphandgrip1.kf'),(1,'2haattackright.kf',None),(2,'1hpattackright.kf','1hphandgrip1.kf'),(3,'2hrattackright.kf',None),(4,'2hrattackright.kf',None)]:
 base='meshes\\characters\\_1stperson\\'
 ready=pose(a.extract(base+'mtidle.kf'))
 # First key of the firing sequence is the original ready-to-fire pose.
 ready.update(pose(a.extract(base+attack)))
 if grip:ready.update(pose(a.extract(base+grip)))
 world=fp.worlds(ready);pw={fp.nodes[i]['name']:m for i,m in world.items()}
 attachment=np.linalg.inv(pw['Weapon'])
 if index==1:
  axes=np.eye(4);axes[:3,:3]=[[1,0,0],[0,0,-1],[0,1,0]];attachment=axes@attachment
 for side,path in [('right','righthand1st.nif'),('left','lefthandpipboyglove1st.nif')]:
  bake('meshes\\characters\\_male\\'+path,f'hands{index}_{side}.nif',pw,attachment)
(OUT/'pose_audit.json').write_text(json.dumps(report,indent=2))
