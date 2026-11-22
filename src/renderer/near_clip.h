#pragma once
#include "gxm_renderer.h"

// Clip in object space against the homogeneous near plane (clip.z + clip.w).
// Interpolate every attribute before perspective division, including texture UVs.
inline unsigned clipNearTriangle(const BasicVertex* triangle, const float* distance,
                                 BasicVertex* polygon) {
    unsigned count=0;
    for(unsigned i=0;i<3;++i) {
        unsigned j=(i+1)%3;
        bool inside=distance[i]>=0, nextInside=distance[j]>=0;
        if(inside) polygon[count++]=triangle[i];
        if(inside!=nextInside) {
            float t=distance[i]/(distance[i]-distance[j]);
            const auto& a=triangle[i];const auto& b=triangle[j];auto& v=polygon[count++];
            for(unsigned k=0;k<3;++k) {v.position[k]=a.position[k]+t*(b.position[k]-a.position[k]);v.normal[k]=a.normal[k]+t*(b.normal[k]-a.normal[k]);}
            for(unsigned k=0;k<4;++k) v.color[k]=a.color[k]+t*(b.color[k]-a.color[k]);
            for(unsigned k=0;k<2;++k) v.uv[k]=a.uv[k]+t*(b.uv[k]-a.uv[k]);
        }
    }
    return count;
}
