"""Strict Fallout New Vegas ESM record/subrecord traversal (24-byte headers)."""
import struct,zlib

MODEL_TYPES={b'STAT',b'SCOL',b'MSTT',b'ACTI',b'DOOR',b'CONT',b'FURN',b'TREE',b'LIGH',b'WEAP',b'ARMO',b'MISC',b'BOOK',b'ALCH',b'AMMO',b'TACT',b'KEYM',b'FLOR'}

def subrecords(data):
    p=0;extended=None
    while p<len(data):
        if p+6>len(data):raise ValueError('truncated subrecord header')
        tag,n=struct.unpack_from('<4sH',data,p);p+=6
        if tag==b'XXXX':
            if n!=4 or p+4>len(data):raise ValueError('bad extended field')
            extended=struct.unpack_from('<I',data,p)[0];p+=4;continue
        n=extended if extended is not None else n;extended=None
        if p+n>len(data):raise ValueError('truncated subrecord')
        yield tag,data[p:p+n];p+=n

def records(raw,start=0,end=None,world=0,cell=0):
    if end is None:end=len(raw)
    p=start
    while p<end:
        if p+24>end:raise ValueError('truncated record header')
        tag,n=struct.unpack_from('<4sI',raw,p)
        if tag==b'GRUP':
            if n<24 or p+n>end:raise ValueError('bad group size')
            label,kind=struct.unpack_from('<Ii',raw,p+8)
            yield from records(raw,p+24,p+n,label if kind==1 else world,label if kind==6 else cell)
            p+=n;continue
        flags,form=struct.unpack_from('<II',raw,p+8)
        if p+24+n>end:raise ValueError('bad record size')
        yield tag,form,flags,world,cell,p+24,n
        p+=24+n

def payload(raw,offset,size,flags):
    data=raw[offset:offset+size]
    if flags&0x40000:
        if len(data)<4:raise ValueError('truncated compressed record')
        expected,=struct.unpack_from('<I',data)
        data=zlib.decompress(data[4:])
        if len(data)!=expected:raise ValueError('uncompressed record size mismatch')
    return data

def text(data):return data.rstrip(b'\0').decode('cp1252')
