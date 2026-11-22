#include "map_data.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <zlib.h>
extern void logMsg(const char*,...);
namespace {
uint64_t key(int32_t x,int32_t y) {return (uint64_t(uint32_t(x))<<32)|uint32_t(y);}
template<class T> bool get(FILE* f,T& x) {return fread(&x,1,sizeof(x),f)==sizeof(x);}
template<class T> T value(const uint8_t* p) {T x;memcpy(&x,p,sizeof(x));return x;}
bool finite(const float* f,unsigned n) {for(unsigned i=0;i<n;++i)if(!std::isfinite(f[i]))return false;return true;}
}
MapDatabase::~MapDatabase(){close();}
void MapDatabase::close() {if(m_pack)fclose(m_pack);m_pack=nullptr;m_selected=0;m_tiles.clear();m_models.clear();m_worlds.clear();m_interiors.clear();m_interiorEditors.clear();}
bool MapDatabase::open(const char* root) {
    close();m_root=root;FILE* f=fopen((m_root+"/catalog.bin").c_str(),"rb");if(!f)return false;
    uint32_t h[6];bool ok=fread(h,1,sizeof(h),f)==sizeof(h) && !memcmp(h,"FWLD",4) && h[1]==1 && h[2]<=100000 && h[3]<=10000 && h[4]<=256 && h[5]<=10000;
    if(ok)for(unsigned group=0;group<2 && ok;++group)for(unsigned i=0;i<h[2+group] && ok;++i) {
        uint32_t id;char path[128];ok=get(f,id) && fread(path,1,128,f)==128 && memchr(path,0,128);
        if(ok && group==0)m_models[id]=path;
    }
    if(ok)for(unsigned i=0;i<h[4] && ok;++i) {
        MapWorld w;ok=get(f,w) && memchr(w.name,0,64) && memchr(w.editor,0,64) && std::isfinite(w.defaultLand) && std::isfinite(w.water) && finite(w.sky,3);
        if(ok)m_worlds[w.form]=w;
    }
    if(ok)for(unsigned i=0;i<h[5] && ok;++i) {
        uint32_t id;char name[64],display[64];ok=get(f,id) && fread(name,1,64,f)==64 && memchr(name,0,64) && fread(display,1,64,f)==64 && memchr(display,0,64);
        if(ok) {m_interiorEditors[id]=name;m_interiors[id]=display;}
    }
    fclose(f);if(!ok){close();return false;}return true;
}
bool MapDatabase::selectWorld(uint32_t form) {
    if(form==m_selected && m_pack)return true;
    if(!world(form))return false;
    char name[32];snprintf(name,sizeof(name),"/%08X.fwp",form);FILE* f=fopen((m_root+name).c_str(),"rb");if(!f)return false;
    uint32_t h[4];bool ok=fread(h,1,16,f)==16 && !memcmp(h,"FWPK",4) && h[1]==1 && h[2]==form && h[3]<=65536;
    if(!ok){fclose(f);return false;}
    fseek(f,0,SEEK_END);long size=ftell(f);if(size<16 || (size_t)size<16+size_t(h[3])*24 || fseek(f,16,SEEK_SET)){fclose(f);return false;}
    std::unordered_map<uint64_t,Entry> entries;
    for(unsigned i=0;i<h[3];++i) {
        Entry e;ok=get(f,e) && e.offset>=16+size_t(h[3])*24 && e.packed>0 && e.raw>=20 && e.raw<=16*1024*1024 && e.offset<=uint32_t(size) && e.packed<=uint32_t(size)-e.offset;
        if(!ok || !entries.emplace(key(e.x,e.y),e).second){fclose(f);return false;}
    }
    if(m_pack)fclose(m_pack);
    m_pack=f;m_packSize=size;m_tiles=std::move(entries);m_selected=form;return true;
}
bool MapDatabase::readTile(int32_t x,int32_t y,MapTile& tile) {
    auto found=m_tiles.find(key(x,y));if(found==m_tiles.end() || !m_pack)return false;const auto& e=found->second;
    std::vector<uint8_t> compressed(e.packed),raw(e.raw);
    if(fseek(m_pack,e.offset,SEEK_SET) || fread(compressed.data(),1,e.packed,m_pack)!=e.packed)return false;
    uLongf length=e.raw;if(uncompress(raw.data(),&length,compressed.data(),compressed.size())!=Z_OK || length!=e.raw)return false;
    const auto* p=raw.data();if(memcmp(p,"FTIL",4) || value<uint32_t>(p+4)!=1)return false;
    uint32_t refs=value<uint32_t>(p+8),land=value<uint32_t>(p+12),doors=value<uint32_t>(p+16);
    if(refs>32768 || doors>4096 || 20ull+refs*sizeof(MapReference)+land+doors*sizeof(MapDoor)!=raw.size())return false;
    MapTile result;result.x=x;result.y=y;result.form=e.form;result.references.resize(refs);result.doors.resize(doors);
    memcpy(result.references.data(),p+20,refs*sizeof(MapReference));p+=20+refs*sizeof(MapReference);
    if(land && !decodeTerrain(p,land,result))return false;
    p+=land;
    memcpy(result.doors.data(),p,doors*sizeof(MapDoor));
    for(const auto& r:result.references)if(!finite(r.position,3) || !finite(r.rotation,3) || !std::isfinite(r.scale) || r.scale<=0)return false;
    for(const auto& d:result.doors)if(!finite(d.source,3) || !finite(d.target,3) || !std::isfinite(d.yaw))return false;
    tile=std::move(result);return true;
}
bool MapDatabase::decodeTerrain(const uint8_t* data,size_t size,MapTile& tile) {
    size_t p=0;uint32_t extended=0;
    while(p<size) {
        if(size-p<6)return false;
        const auto* tag=data+p;uint32_t n=value<uint16_t>(tag+4);p+=6;
        if(!memcmp(tag,"XXXX",4)) {if(n!=4 || size-p<4)return false;extended=value<uint32_t>(data+p);p+=4;continue;}
        if(extended){n=extended;extended=0;}if(n>size-p)return false;const auto* b=data+p;
        if(!memcmp(tag,"VHGT",4)) {
            if(n!=1096 || !std::isfinite(value<float>(b)))return false;
            float row=value<float>(b);
            for(unsigned y=0;y<33;++y) {
                row+=int8_t(b[4+y*33]);float height=row;tile.heights[y*33]=height*8;
                for(unsigned x=1;x<33;++x){height+=int8_t(b[4+y*33+x]);tile.heights[y*33+x]=height*8;}
            }
            tile.hasTerrain=true;
        } else if(!memcmp(tag,"VNML",4)) {
            if(n!=3267)return false;
            memcpy(tile.normals.data(),b,3267);tile.hasNormals=true;
        }
        p+=n;
    }
    return !extended;
}
const MapWorld* MapDatabase::world(uint32_t form) const {auto i=m_worlds.find(form);return i==m_worlds.end()?nullptr:&i->second;}
const std::string* MapDatabase::model(uint32_t form) const {auto i=m_models.find(form);return i==m_models.end()?nullptr:&i->second;}
const std::string* MapDatabase::interiorName(uint32_t form) const {auto i=m_interiors.find(form);return i==m_interiors.end()?nullptr:&i->second;}
std::string MapDatabase::interiorPath(uint32_t form) const {char name[40];snprintf(name,sizeof(name),"/interiors/%08X.cell",form);return m_root+name;}
uint32_t MapDatabase::interiorID(const char* name) const {for(const auto& entry:m_interiorEditors)if(entry.second==name)return entry.first;return 0;}
