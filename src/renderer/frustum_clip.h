#pragma once
#include "near_clip.h"
#include <cfloat>
#include <cmath>

// Clip before projection. GXM tile clipping alone does not bound vertex X/W
// and Y/W; a triangle crossing the eye can otherwise reach huge screen values.
struct FrustumClipper {
    float plane[6][4]{};
    explicit FrustumClipper(const Mat4& wvp) {
        for(unsigned p=0;p<6;++p) {
            unsigned axis=p/2;float sign=(p&1)?-1.f:1.f;
            for(unsigned k=0;k<4;++k) plane[p][k]=wvp.m[k][3]+sign*wvp.m[k][axis];
        }
        plane[4][3]-=.01f;
    }
    float distance(unsigned p,const BasicVertex& v) const {
        return plane[p][0]*v.position[0]+plane[p][1]*v.position[1]+plane[p][2]*v.position[2]+plane[p][3];
    }
    // Negative means outside. Zero means fully inside. Otherwise return the
    // mask of intersected planes, avoiding vertex scans for contained meshes.
    int classify(const Vec3& minimum,const Vec3& maximum) const {
        unsigned mask=0;const float lo[]={minimum.x,minimum.y,minimum.z},hi[]={maximum.x,maximum.y,maximum.z};
        for(unsigned p=0;p<6;++p) {
            float lower=plane[p][3],upper=lower;
            for(unsigned k=0;k<3;++k) {
                lower+=plane[p][k]*(plane[p][k]>=0?lo[k]:hi[k]);
                upper+=plane[p][k]*(plane[p][k]>=0?hi[k]:lo[k]);
            }
            if(upper<0)return -1;
            if(lower<0)mask|=1u<<p;
        }
        return mask;
    }
    unsigned code(const BasicVertex& v,unsigned mask) const {
        unsigned result=0;
        for(unsigned p=0;p<6;++p)if((mask&(1u<<p)) && distance(p,v)<0)result|=1u<<p;
        return result;
    }
    // Cluster bounds must be conservative against cancellation at large world
    // coordinates. Ambiguous boundary groups fall back to the original tests.
    int classifyCluster(const Vec3& minimum,const Vec3& maximum) const {
        unsigned mask=0;const float lo[]={minimum.x,minimum.y,minimum.z},hi[]={maximum.x,maximum.y,maximum.z};
        for(unsigned p=0;p<6;++p) {
            float lower=plane[p][3],upper=lower,error=fabsf(lower);
            for(unsigned k=0;k<3;++k) {
                lower+=plane[p][k]*(plane[p][k]>=0?lo[k]:hi[k]);
                upper+=plane[p][k]*(plane[p][k]>=0?hi[k]:lo[k]);
                error+=fabsf(plane[p][k])*std::max(fabsf(lo[k]),fabsf(hi[k]));
            }
            error=error*(16*FLT_EPSILON)+.00001f;
            if(upper < -error)return -1;
            if(lower<=error)mask|=1u<<p;
        }
        return mask;
    }
    unsigned clip(const BasicVertex* triangle,unsigned mask,BasicVertex* result) const {
        BasicVertex buffers[2][12];unsigned n=3,source=0;
        for(unsigned k=0;k<3;++k)buffers[0][k]=triangle[k];
        for(unsigned p=0;p<6 && n;++p)if(mask&(1u<<p)) {
            unsigned count=0;const auto* in=buffers[source];auto* out=buffers[1-source];
            for(unsigned i=0;i<n;++i) {
                const auto& a=in[i];const auto& b=in[(i+1)%n];
                float da=distance(p,a),db=distance(p,b);bool ia=da>=0,ib=db>=0;
                if(ia)out[count++]=a;
                if(ia!=ib) {
                    float t=da/(da-db);auto& v=out[count++];
                    for(unsigned k=0;k<3;++k) {v.position[k]=a.position[k]+t*(b.position[k]-a.position[k]);v.normal[k]=a.normal[k]+t*(b.normal[k]-a.normal[k]);}
                    for(unsigned k=0;k<4;++k)v.color[k]=a.color[k]+t*(b.color[k]-a.color[k]);
                    for(unsigned k=0;k<2;++k)v.uv[k]=a.uv[k]+t*(b.uv[k]-a.uv[k]);
                }
            }
            n=count;source=1-source;
        }
        for(unsigned i=0;i<n;++i)result[i]=buffers[source][i];
        return n;
    }
};
