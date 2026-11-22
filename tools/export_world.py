"""Export original base-game worlds/interiors for random-access Vita streaming.

No geometry is synthesized: terrain retains original LAND subrecords; placed
objects retain original transforms and BSA model paths. Uses only stdlib.
"""
import argparse,collections,hashlib,json,math,mmap,struct,zlib
from pathlib import Path
from esm_assets import MODEL_TYPES,records,subrecords,payload,text

def fixed(s,n):
    b=s.encode('cp1252')
    if len(b)>=n:raise ValueError('path/name too long: '+s)
    return b+b'\0'*(n-len(b))

def heights(land):
    d=dict(subrecords(land));b=d.get(b'VHGT')
    if b is None:return None
    if len(b)!=1096:raise ValueError('unexpected VHGT size')
    offset,=struct.unpack_from('<f',b);deltas=struct.unpack_from('<1089b',b,4)
    if not math.isfinite(offset):raise ValueError('invalid terrain offset')
    result=[];row=offset
    for y in range(33):
        row+=deltas[y*33];value=row;result.append(value*8)
        for x in range(1,33):value+=deltas[y*33+x];result.append(value*8)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-data',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'world')
    args=parser.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
    (out/'interiors').mkdir(exist_ok=True)
    worlds={};cells={};models={};textures={};ltex={};lands={};refs={};errors=[];climates={};weather={};editor_markers=set()
    esm=args.game_data/'FalloutNV.esm'
    with esm.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as raw:
        for tag,form,flags,world,cell,offset,size in records(raw):
            if tag not in MODEL_TYPES|{b'WRLD',b'CELL',b'LAND',b'REFR',b'LTEX',b'TXST',b'CLMT',b'WTHR'}:continue
            try:
                fs=list(subrecords(payload(raw,offset,size,flags)));d=dict(fs)
            except (ValueError,zlib.error) as e:
                errors.append({'form':form,'type':tag.decode(),'world':world,'cell':cell,'error':str(e)});continue
            if tag in MODEL_TYPES and b'MODL' in d:
                models[form]=text(d[b'MODL']).replace('/','\\')
                path=models[form].lower()
                # WallMarker/ChairMarker are editor representations of idle
                # furniture positions, not visible world architecture.
                if 'marker' in path.split('\\')[-1] or '\\markers\\' in path:editor_markers.add(form)
            if tag==b'WRLD':
                land,water=struct.unpack('<ff',d.get(b'DNAM',struct.pack('<ff',-2048,0)))
                worlds[form]={'name':text(d.get(b'FULL',d.get(b'EDID',b'')))[:63],'editor_id':text(d.get(b'EDID',b'')),'parent':struct.unpack('<I',d.get(b'WNAM',b'\0'*4))[0],'land':land,'water':water,'climate':struct.unpack('<I',d.get(b'CNAM',b'\0'*4))[0]}
            elif tag==b'CELL':cells[form]={'world':world,'interior':bool(d.get(b'DATA',b'\0')[0]&1),'grid':struct.unpack_from('<ii',d[b'XCLC']) if b'XCLC' in d else None,'name':text(d.get(b'EDID',d.get(b'FULL',b'')))[:63],'display':text(d.get(b'FULL',d.get(b'EDID',b'')))[:63]}
            elif tag==b'LAND':lands[cell]=(form,offset,size,flags)
            elif tag==b'TXST' and b'TX00' in d:textures[form]=text(d[b'TX00'])
            elif tag==b'LTEX':ltex[form]=struct.unpack('<I',d.get(b'TNAM',b'\0'*4))[0]
            elif tag==b'WTHR' and len(d.get(b'NAM0',b''))>=8:weather[form]=list(d[b'NAM0'][4:7])
            elif tag==b'CLMT':climates[form]=d.get(b'WLST',b'')
            elif tag==b'REFR' and b'NAME' in d and b'DATA' in d:
                transform=struct.unpack('<6f',d[b'DATA']);scale=struct.unpack('<f',d.get(b'XSCL',struct.pack('<f',1)))[0]
                if not all(math.isfinite(v) for v in (*transform,scale)):raise ValueError('nonfinite REFR')
                refs[form]={'cell':cell,'world':world,'flags':flags,'base':struct.unpack('<I',d[b'NAME'])[0],'transform':transform,'scale':scale,'teleport':d.get(b'XTEL'),'parent':d.get(b'XESP')}

        # Initial enabled state only. Script/quest changes are outside map import.
        def enabled(form,chain=()):
            r=refs[form]
            if r['flags']&0x820:return False
            if r['parent']:
                parent,flags=struct.unpack_from('<II',r['parent'])
                if parent in chain:return False
                if parent in refs:return enabled(parent,chain+(form,)) != bool(flags&1)
            return not bool(r['flags']&0x800)

        grouped=collections.defaultdict(list);doors=collections.defaultdict(list);incoming={};fallback={}
        for form,r in refs.items():
            if not enabled(form):continue
            c=cells.get(r['cell'])
            if not c:continue
            fallback.setdefault(r['cell'],r['transform'])
            if c['interior']:key=('interior',r['cell'])
            else:
                x,y=r['transform'][:2];key=(r['world'],math.floor(x/4096),math.floor(y/4096))
            if r['base'] in models and r['base'] not in editor_markers:grouped[key].append(struct.pack('<II7f',form,r['base'],*r['transform'],r['scale']))
            if r['teleport'] and len(r['teleport'])>=28:
                target,*pos=struct.unpack_from('<I6f',r['teleport']);dest=refs.get(target)
                if dest:
                    destcell=cells[dest['cell']]
                    incoming.setdefault(dest['cell'],pos)
                    doors[key].append(struct.pack('<III7f',form,0 if destcell['interior'] else dest['world'],dest['cell'],*r['transform'][:3],*pos[:3],pos[5]))

        terrain_edges={};terrain_count=0;exported_refs=0;exported_doors=0
        world_cells=collections.defaultdict(dict)
        for form,c in cells.items():
            if c['world'] and c['grid'] is not None:world_cells[c['world']][tuple(c['grid'])]=form
        for key in grouped:
            if key[0]!='interior':world_cells[key[0]].setdefault(key[1:],0)
        summary=[]
        for world,info in sorted(worlds.items()):
            grids=world_cells[world];entries=[];path=out/f'{world:08X}.fwp'
            with path.open('wb') as pack:
                pack.write(struct.pack('<4sIII',b'FWPK',1,world,len(grids)));pack.write(b'\0'*(len(grids)*24))
                numland=numrefs=numdoors=0
                for (x,y),form in sorted(grids.items(),key=lambda v:(v[0][1],v[0][0])):
                    land=b''
                    if form in lands:
                        lf,offset,size,flags=lands[form];land=payload(raw,offset,size,flags)
                        values=heights(land)
                        if values is not None:
                            terrain_edges[(world,x,y)]=(values[:33],values[-33:],values[::33],values[32::33])
                            terrain_count+=1;numland+=1
                    rr=grouped[(world,x,y)];dd=doors[(world,x,y)]
                    blob=struct.pack('<4sIIII',b'FTIL',1,len(rr),len(land),len(dd))+b''.join(rr)+land+b''.join(dd)
                    packed=zlib.compress(blob,6);offset=pack.tell();pack.write(packed)
                    entries.append(struct.pack('<iiIIII',x,y,offset,len(packed),len(blob),form));numrefs+=len(rr);numdoors+=len(dd)
                pack.seek(16);pack.write(b''.join(entries))
            exported_refs+=numrefs;exported_doors+=numdoors
            summary.append({'world':world,'editor_id':info['editor_id'],'tiles':len(grids),'terrain_tiles':numland,'placed_models':numrefs,'doors':numdoors,'bytes':path.stat().st_size})
        seams=[]
        for (world,x,y),(south,north,west,east) in terrain_edges.items():
            for neighbor,edge,other in [((world,x+1,y),east,2),((world,x,y+1),north,0)]:
                if neighbor in terrain_edges:
                    difference=max(abs(a-b) for a,b in zip(edge,terrain_edges[neighbor][other]))
                    if difference>.01:seams.append({'world':world,'grid':[x,y],'neighbor':list(neighbor[1:]),'max_difference':difference})
        interior_summary=[]
        for form,c in sorted(cells.items()):
            if not c['interior']:continue
            rr=grouped[('interior',form)];dd=doors[('interior',form)]
            spawn=incoming.get(form,fallback.get(form,(0,0,0,0,0,0)))
            data=struct.pack('<4sIII64sI5f',b'CELL',1,form,0,fixed(c['display'],64),len(rr),*spawn[:3],spawn[5],0)
            for entry in rr:
                ref,base,*pose=struct.unpack('<II7f',entry);data+=struct.pack('<II128s7f',ref,base,fixed(models[base],128),*pose)
            data+=struct.pack('<4sI',b'DOOR',len(dd))+b''.join(dd)
            (out/'interiors'/f'{form:08X}.cell').write_bytes(data)
            exported_refs+=len(rr);exported_doors+=len(dd);interior_summary.append((form,c['name'],c['display']))
        resolved_ltex={form:textures[tx] for form,tx in ltex.items() if tx in textures}
        with (out/'catalog.bin').open('wb') as cat:
            cat.write(struct.pack('<4s5I',b'FWLD',1,len(models),len(resolved_ltex),len(worlds),len(interior_summary)))
            for form,path in sorted(models.items()):cat.write(struct.pack('<I128s',form,fixed(path,128)))
            for form,path in sorted(resolved_ltex.items()):cat.write(struct.pack('<I128s',form,fixed(path,128)))
            for form,info in sorted(worlds.items()):
                climate=info['climate'] or worlds.get(info['parent'],{}).get('climate',0)
                entries=climates.get(climate,b'');choices=[struct.unpack_from('<III',entries,p) for p in range(0,len(entries)-11,12)]
                unconditioned=[e for e in choices if e[2]==0];choices=unconditioned or choices
                weather_id=max(choices,key=lambda e:e[1])[0] if choices else 0
                sky=[v/255 for v in weather.get(weather_id,[50,71,135])]
                info['weather']=weather_id
                cat.write(struct.pack('<II64s64s5f',form,info['parent'],fixed(info['name'],64),fixed(info['editor_id'],64),info['land'],info['water'],*sky))
            for form,name,display in interior_summary:cat.write(struct.pack('<I64s64s',form,fixed(name,64),fixed(display,64)))
        report={'esm_sha256':hashlib.sha256(raw).hexdigest(),'worlds':summary,'interiors':len(interior_summary),'terrain_tiles':terrain_count,'placed_models':exported_refs,'doors':exported_doors,'models':len(models),'editor_marker_base_forms_excluded':len(editor_markers),'land_texture_paths':len(resolved_ltex),'source_record_errors':errors,'terrain_seam_mismatches':seams,'runtime_verified':False,'hardware_verified':False,'scope':'FalloutNV.esm initial enabled map geometry; no DLC overrides or quest state execution'}
        (out/'import_report.json').write_text(json.dumps(report,indent=2))
        print(json.dumps({k:v for k,v in report.items() if k not in ('terrain_seam_mismatches',)},indent=2));print('Terrain seam mismatches:',len(seams))

if __name__=='__main__':main()
