"""Export original first-person KF playback to bounded, compressed vertex clips.

Requires NumPy. Coordinates are relative to the original Camera1st node, in
game units (X right, Y forward, Z up). No per-weapon axis guesses are used.
"""
import argparse, json, struct, zlib
from pathlib import Path
import numpy as np
from asset_formats import Archive, Nif, blocks, quat, trs

def good(v):
    return np.isfinite(v).all() and np.max(np.abs(v)) < 1e6

def slerp(a,b,t):
    a=a/np.linalg.norm(a);b=b/np.linalg.norm(b);dot=np.dot(a,b)
    if dot<0:b=-b;dot=-dot
    if dot>.9995:
        q=a+(b-a)*t;return q/np.linalg.norm(q)
    theta=np.arccos(np.clip(dot,-1,1))
    return (np.sin((1-t)*theta)*a+np.sin(t*theta)*b)/np.sin(theta)

def spline(cp,u):
    n=len(cp);degree=min(3,n-1)
    if u>=1:return cp[-1]
    knots=np.r_[np.zeros(degree+1),np.arange(1,n-degree)/(n-degree),np.ones(degree+1)]
    k=min(n-1,max(degree,np.searchsorted(knots,u,side='right')-1))
    d=cp[k-degree:k+1].copy()
    for r in range(1,degree+1):
        for j in range(degree,r-1,-1):
            lo=knots[k-degree+j];hi=knots[k+1+j-r]
            alpha=(u-lo)/(hi-lo) if hi>lo else 0
            d[j]=(1-alpha)*d[j-1]+alpha*d[j]
    return d[degree]

class Keys:
    def __init__(self,raw,offset,dims,rotation=False):
        self.rotation=rotation;self.axes=None;self.keys=[]
        n=struct.unpack_from('<I',raw,offset)[0];offset+=4
        if not n:self.end=offset;return
        self.kind=struct.unpack_from('<I',raw,offset)[0];offset+=4
        if rotation and self.kind==4:
            self.axes=[]
            for _ in range(3):
                k=Keys(raw,offset,1);offset=k.end;self.axes.append(k)
        else:
            assert self.kind in [1,2,3],self.kind
            for _ in range(n):
                time=struct.unpack_from('<f',raw,offset)[0];offset+=4
                value=np.array(struct.unpack_from('<'+'f'*dims,raw,offset));offset+=4*dims
                forward=backward=None
                if self.kind==2 and not rotation:
                    forward=np.array(struct.unpack_from('<'+'f'*dims,raw,offset));offset+=4*dims
                    backward=np.array(struct.unpack_from('<'+'f'*dims,raw,offset));offset+=4*dims
                elif self.kind==3:offset+=12
                self.keys.append((time,value,forward,backward))
        self.end=offset
    def sample(self,t):
        if self.axes:
            q=np.array([1.,0,0,0])
            for axis,keys in enumerate(self.axes):
                angle=keys.sample(t)[0];r=np.zeros(4);r[0]=np.cos(angle/2);r[axis+1]=np.sin(angle/2)
                w,x,y,z=q;v,i,j,k=r;q=np.array([w*v-x*i-y*j-z*k,w*i+x*v+y*k-z*j,w*j-x*k+y*v+z*i,w*k+x*j-y*i+z*v])
            return q
        if not self.keys:return None
        if t<=self.keys[0][0]:return self.keys[0][1]
        for a,b in zip(self.keys,self.keys[1:]):
            if t<=b[0]:
                u=(t-a[0])/(b[0]-a[0])
                if self.rotation:return slerp(a[1],b[1],u)
                if self.kind==2:
                    return (2*u**3-3*u*u+1)*a[1]+(u**3-2*u*u+u)*a[2]+(-2*u**3+3*u*u)*b[1]+(u**3-u*u)*b[3]
                return a[1]*(1-u)+b[1]*u
        return self.keys[-1][1]

class Clip:
    def __init__(self,raw):
        _,ss,bb=blocks(raw);seq=bb[0][1];n=struct.unpack_from('<I',seq,4)[0]
        self.start,self.stop=struct.unpack_from('<2f',seq,12+29*n+16)
        self.duration=max(.001,self.stop-self.start);self.tracks={}
        for i in range(n):
            inter,controller,priority,name,prop,ctype,cid,iid=struct.unpack_from('<iiB5i',seq,12+i*29)
            if inter<0:continue
            typ,b=bb[inter]
            if typ=='NiTransformInterpolator':
                values=np.array(struct.unpack_from('<8f',b));ref=struct.unpack_from('<i',b,32)[0];keys=[]
                if ref>=0:
                    data=bb[ref][1];offset=0
                    for dims,rotation in [(4,True),(3,False),(1,False)]:
                        k=Keys(data,offset,dims,rotation);offset=k.end;keys.append(k)
                self.tracks[ss[name]]=('keys',values,keys)
            elif typ=='NiBSplineCompTransformInterpolator':
                t0,t1,di,bi=struct.unpack_from('<ffii',b);values=np.array(struct.unpack_from('<8f',b,16))
                handles=struct.unpack_from('<3I',b,48);ranges=struct.unpack_from('<6f',b,60)
                data=bb[di][1];nf=struct.unpack_from('<I',data)[0];offset=4+nf*4;nc=struct.unpack_from('<I',data,offset)[0]
                cp=np.frombuffer(data,dtype='<i2',count=nc,offset=offset+4);nctrl=struct.unpack('<I',bb[bi][1])[0]
                arrays=[]
                for k,(h,dims) in enumerate(zip(handles,[3,4,1])):
                    arrays.append(None if h==65535 else (cp[h:h+nctrl*dims].reshape(nctrl,dims).astype(float)/32767*ranges[k*2+1]+ranges[k*2]))
                self.tracks[ss[name]]=('spline',values,(t0,t1,arrays))
    def sample(self,t):
        result={};t=np.clip(t+self.start,self.start,self.stop)
        for name,(typ,values,data) in self.tracks.items():
            v=[values[:3].copy(),values[3:7].copy(),values[7:8].copy()]
            if typ=='keys' and data:
                for i,j in [(0,1),(1,0),(2,2)]:
                    value=data[j].sample(t)
                    if value is not None:v[i]=value
            elif typ=='spline':
                t0,t1,arrays=data;u=np.clip((t-t0)/(t1-t0),0,1) if t1>t0 else 0
                for i,cp in enumerate(arrays):
                    if cp is not None:v[i]=spline(cp,u)
            result[name]=(v[0],v[1],v[2][0])
        return result

def merge(*layers):
    result={}
    for layer in layers:
        for name,values in layer.items():
            old=result.get(name,(np.full(3,np.nan),np.full(4,np.nan),np.nan))
            result[name]=tuple(value if good(np.asarray(value)) else previous for value,previous in zip(values,old))
    return result

class Geometry:
    def __init__(self,raw):
        self.nif=Nif(raw);n=self.nif;world=n.worlds();self.parts=[];self.count=0
        for i,node in n.nodes.items():
            if 'geom' not in node:continue
            current=i;hidden=False
            while current in n.nodes:
                hidden|=n.nodes[current]['hidden']
                current=n.parents.get(current,-1)
            if hidden:continue
            b=n.blocks[node['geom']][1];num=struct.unpack_from('<H',b,4)[0]
            v=np.frombuffer(b,dtype='<f4',count=num*3,offset=9).reshape(-1,3).copy()
            p=9+num*12;flags=struct.unpack_from('<H',b,p)[0];p+=2
            if struct.unpack_from('<I',raw,raw.index(b'\n')+6)[0]>=12:p+=4
            hasnorm=b[p];p+=1
            normals=np.frombuffer(b,dtype='<f4',count=num*3,offset=p).reshape(-1,3).copy() if hasnorm else np.tile([0.,0,1.],(num,1))
            weights=[]
            if node['skin']>=0:
                skin=n.blocks[node['skin']][1];data,partition,root,count=struct.unpack_from('<4i',skin);bones=struct.unpack_from('<'+'i'*count,skin,16);sd=n.blocks[data][1];p=57
                sums=np.zeros(num);bind=np.zeros((num,3))
                for bone in bones:
                    offset=trs(sd,p,True);p+=68;nv=struct.unpack_from('<H',sd,p)[0];p+=2
                    indices=[];ww=[]
                    for _ in range(nv):
                        idx,weight=struct.unpack_from('<Hf',sd,p);p+=6;indices.append(idx);ww.append(weight)
                    indices=np.array(indices,dtype=int);ww=np.array(ww)
                    sums[indices]+=ww
                    transform=world[bone]@offset
                    bind[indices]+=ww[:,None]*(v[indices]@transform[:3,:3].T+transform[:3,3])
                    weights.append((n.nodes[bone]['name'],offset,indices,ww))
                assert np.allclose(sums,1,atol=.015)
                assert np.max(np.linalg.norm(bind-(v@world[i][:3,:3].T+world[i][:3,3]),axis=1))<.05
            self.parts.append((i,v,normals,weights));self.count+=num
    def frame(self,pose,world,attachment):
        out=[];nodeworld=self.nif.worlds(pose)
        for i,v,normals,weights in self.parts:
            if weights:
                pv=np.zeros_like(v,dtype=float);pn=np.zeros_like(normals,dtype=float)
                for bone,offset,indices,ww in weights:
                    transform=world[bone]@offset
                    pv[indices]+=ww[:,None]*(v[indices]@transform[:3,:3].T+transform[:3,3])
                    pn[indices]+=ww[:,None]*(normals[indices]@transform[:3,:3].T)
                transform=attachment
            else:
                pv=v;pn=normals;transform=attachment@nodeworld[i]
            pv=pv@transform[:3,:3].T+transform[:3,3];pn=pn@transform[:3,:3].T
            pn/=np.maximum(np.linalg.norm(pn,axis=1)[:,None],1e-8)
            out.append(np.concatenate([pv,pn],axis=1))
        return np.concatenate(out)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-data',required=True,type=Path)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'assets')
    parser.add_argument('--fps',type=int,default=24)
    args=parser.parse_args();args.output.mkdir(exist_ok=True,parents=True)
    archive=Archive(args.game_data/'Fallout - Meshes.bsa');base='meshes\\characters\\_1stperson\\'
    skeleton=Nif(archive.extract(base+'skeleton.nif'));baseclip=Clip(archive.extract(base+'mtidle.kf'));basepose=baseclip.sample(0)
    hands=[Geometry(archive.extract('meshes\\characters\\_male\\'+name)) for name in ['righthand1st.nif','lefthandpipboyglove1st.nif']]
    # Original Vault 21 outfit has a separate Arms shape. Keep its authored mesh,
    # weights and materials, and hide the torso/dismemberment alternatives.
    armsraw=bytearray(archive.extract('meshes\\armor\\vaultsuit\\m\\outfit.nif'));armsnif=Nif(armsraw)
    for i,node in armsnif.nodes.items():
        if 'geom' not in node:continue
        b=armsnif.blocks[i][1];extra=struct.unpack_from('<I',b,4)[0];at=armsnif.starts[i]+12+extra*4
        flags=struct.unpack_from('<I',armsraw,at)[0]
        struct.pack_into('<I',armsraw,at,flags&~1 if node['name']=='Arms' else flags|1)
    (args.output/'player_arms.nif').write_bytes(armsraw)
    arms=Geometry(armsraw)
    weapons=[('1handpistol\\10mmpistol.nif','1hp','a'),('2handautomatic\\servicerifle.nif','2ha','f'),('1handpistol\\laserpistol.nif','1hp','f'),('2handrifle\\caravanshotgun.nif','2hr','i'),('2handrifle\\varmintrifle.nif','2hr','m')]
    audit=[]
    for index,(model,group,reload) in enumerate(weapons):
        weapon=Geometry(archive.extract('meshes\\weapons\\'+model));geometry=[weapon,*hands,arms];counts=[g.count for g in geometry]
        names=[group+'aim.kf',group+'aimis.kf',group+'attackright.kf',group+'attackrightis.kf',group+'reload'+reload+'.kf']
        clips=[Clip(archive.extract(base+name)) for name in names]
        buffers=[struct.pack('<4s5I',b'VMQ6',*counts,len(clips))];clipaudit=[]
        for clipindex,(name,clip) in enumerate(zip(names,clips)):
            frames=max(2,int(np.ceil(clip.duration*args.fps))+1)
            assert frames<=241
            buffers.append(struct.pack('<fI',clip.duration,frames));motion=[]
            for time in np.linspace(0,clip.duration,frames):
                # Aim/attack/reload sequences include the original default grip tracks.
                pose=merge(basepose,clips[0].sample(0),clip.sample(time))
                transforms=skeleton.worlds(pose);world={skeleton.nodes[i]['name']:matrix for i,matrix in transforms.items()}
                camera=np.linalg.inv(world['Camera1st']);weaponworld=camera@world['Weapon']
                frame=np.concatenate([weapon.frame(pose,world,weaponworld),*[hand.frame(pose,world,camera) for hand in hands],arms.frame(pose,world,camera)])
                if clipindex in [1,3]:
                    # Original SightingNode defines the weapon-specific iron-sight line.
                    for i,node in weapon.nif.nodes.items():
                        if node['name']=='##SightingNode':
                            sight=weaponworld@weapon.nif.worlds(pose)[i]
                            frame[:,0]-=sight[0,3];frame[:,2]-=sight[2,3];break
                muzzle=np.zeros(3)
                for i,node in weapon.nif.nodes.items():
                    if node['name']=='ProjectileNode':
                        muzzle=(weaponworld@weapon.nif.worlds(pose)[i])[:3,3]
                        if clipindex in [1,3]:muzzle[[0,2]]-=sight[[0,2],3]
                        break
                assert np.isfinite(frame).all() and np.max(np.abs(frame[:,:3]))<1000
                # Positions use 1/64 game unit precision; normals use signed normalized 16-bit.
                quantized=frame*np.array([64,64,64,32767,32767,32767])
                assert np.max(np.abs(quantized))<=32767.51
                pipboy=camera@world['Bip01 L ForeTwist']
                buffers.append(muzzle.astype('<f4').tobytes()+pipboy.T.astype('<f4').tobytes()+np.rint(quantized).astype('<i2').tobytes());motion.append(frame[:,:3])
            clipaudit.append({'source':base+name,'seconds':clip.duration,'frames':frames,'maximum_vertex_motion':float(np.max(np.linalg.norm(np.stack(motion)-motion[0],axis=2)))})
        raw=b''.join(buffers);dest=args.output/f'viewmodel{index}.vma'
        dest.write_bytes(b'VMC6'+struct.pack('<I',len(raw))+zlib.compress(raw,6))
        audit.append({'weapon':model,'counts':counts,'clips':clipaudit,'raw_bytes':len(raw),'compressed_bytes':dest.stat().st_size})
        print(index,model,counts,len(raw),dest.stat().st_size,flush=True)
    (args.output/'viewmodel_audit.json').write_text(json.dumps(audit,indent=2))

if __name__=='__main__':main()
