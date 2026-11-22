import struct, zlib
import numpy as np
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

