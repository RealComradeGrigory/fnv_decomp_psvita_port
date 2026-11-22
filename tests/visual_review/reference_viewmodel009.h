#pragma once
#include "renderer/gxm_renderer.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <zlib.h>

// Prepared from original first-person KFs, skin weights and Camera1st/Weapon nodes.
// Animation data remains in CPU memory; only the current interpolated frame is uploaded.
class ViewmodelAnimationReference {
    struct Clip {float duration=0;uint32_t frames=0;size_t offset=0;};
    std::vector<uint8_t> m_data;
    uint32_t m_counts[4]{};
    Clip m_clips[5];
    size_t m_stride=0;
    bool m_valid=false;
    template<class T> T read(size_t offset) const {T value;memcpy(&value,m_data.data()+offset,sizeof(value));return value;}
    struct Sample {size_t a,b;float mix;};
    Sample at(unsigned clip,float time,bool normalized=false) const {
        const auto& c=m_clips[clip];
        float t=normalized?time:time/c.duration;
        if(clip<2) t-=floorf(t);else t=std::max(0.f,std::min(1.f,t));
        float frame=t*(c.frames-1);unsigned a=std::min(c.frames-1,(unsigned)frame),b=std::min(c.frames-1,a+1);
        return {c.offset+a*m_stride,c.offset+b*m_stride,frame-a};
    }
    float component(const Sample& s,size_t offset,float scale) const {
        float a=read<int16_t>(s.a+76+offset),b=read<int16_t>(s.b+76+offset);
        return (a+(b-a)*s.mix)*scale;
    }
    float muzzle(const Sample& s,unsigned k) const {
        float a=read<float>(s.a+k*4),b=read<float>(s.b+k*4);return a+(b-a)*s.mix;
    }
public:
    void clear() {m_data.clear();m_data.shrink_to_fit();m_valid=false;}
    bool valid() const {return m_valid;}
    float duration(unsigned clip) const {return m_valid && clip<5?m_clips[clip].duration:0;}
    bool load(const char* path,const uint32_t* expected) {
        clear();FILE* f=fopen(path,"rb");if(!f)return false;
        if(fseek(f,0,SEEK_END)!=0) {fclose(f);return false;}
        long size=ftell(f);if(size<8 || size>48*1024*1024 || fseek(f,0,SEEK_SET)!=0) {fclose(f);return false;}
        std::vector<uint8_t> packed(size);bool ok=fread(packed.data(),1,size,f)==(size_t)size;fclose(f);
        uint32_t rawSize=0;memcpy(&rawSize,packed.data()+4,4);
        if(!ok || memcmp(packed.data(),"VMC6",4) || rawSize<24 || rawSize>64*1024*1024)return false;
        m_data.resize(rawSize);uLongf actual=rawSize;
        if(uncompress(m_data.data(),&actual,packed.data()+8,size-8)!=Z_OK || actual!=rawSize) {clear();return false;}
        if(memcmp(m_data.data(),"VMQ6",4) || read<uint32_t>(20)!=5) {clear();return false;}
        uint32_t total=0;
        for(unsigned i=0;i<4;++i) {m_counts[i]=read<uint32_t>(4+i*4);if(m_counts[i]!=expected[i] || m_counts[i]>65536) {clear();return false;}total+=m_counts[i];}
        m_stride=76+(size_t)total*12;
        size_t p=24;
        for(auto& c:m_clips) {
            if(p>m_data.size() || m_data.size()-p<8) {clear();return false;}
            c.duration=read<float>(p);c.frames=read<uint32_t>(p+4);p+=8;c.offset=p;
            if(!std::isfinite(c.duration) || c.duration<=0 || c.duration>30 || c.frames<2 || c.frames>241 || c.frames>(m_data.size()-p)/m_stride) {clear();return false;}
            // Imported floating point anchor data must also be finite and bounded.
            for(unsigned frame=0;frame<c.frames;++frame) for(unsigned axis=0;axis<19;++axis) {
                float x=read<float>(p+frame*m_stride+axis*4);
                if(!std::isfinite(x) || fabsf(x)>1000) {clear();return false;}
            }
            p+=c.frames*m_stride;
        }
        if(p!=m_data.size()) {clear();return false;}
        m_valid=true;return true;
    }
    bool apply(GxmMesh* meshes[4],float time,float ads,float attackAge,bool reloading,float reloadProgress,float outMuzzle[3],Mat4* pipboy=nullptr) const {
        if(!m_valid)return false;
        for(unsigned i=0;i<4;++i) if(!meshes[i] || meshes[i]->vertexCount!=m_counts[i] || !meshes[i]->vertices)return false;
        bool attack=attackAge<m_clips[2].duration;
        Sample hip=at(attack?2:0,attack?attackAge:time),aim=at(attack?3:1,attack?attackAge:time);
        if(reloading) {hip=aim=at(4,reloadProgress,true);ads=0;}
        ads=std::max(0.f,std::min(1.f,ads));size_t offset=0;
        for(unsigned i=0;i<4;++i) {
            auto& mesh=*meshes[i];mesh.dynamic=true;mesh.hasBounds=false;
            for(unsigned j=0;j<mesh.vertexCount;++j) {
                auto& vertex=mesh.vertices[j];
                for(unsigned k=0;k<6;++k) {
                    float scale=k<3?1.f/64.f:1.f/32767.f;
                    float a=component(hip,offset+k*2,scale),b=component(aim,offset+k*2,scale);
                    if(k<3)vertex.position[k]=a+(b-a)*ads;else vertex.normal[k-3]=a+(b-a)*ads;
                }
                offset+=12;
            }
        }
        for(unsigned k=0;k<3;++k)outMuzzle[k]=muzzle(hip,k)+(muzzle(aim,k)-muzzle(hip,k))*ads;
        if(pipboy) for(unsigned k=0;k<16;++k) {
            auto matrix=[&](const Sample& s) {float a=read<float>(s.a+12+k*4),b=read<float>(s.b+12+k*4);return a+(b-a)*s.mix;};
            float a=matrix(hip),b=matrix(aim);(&pipboy->m[0][0])[k]=a+(b-a)*ads;
        }
        return true;
    }
};
