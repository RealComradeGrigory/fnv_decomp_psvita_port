#pragma once
#include "nif/nif_loader.h"
#include <map>
#include <set>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <array>

// Conservative vertex clustering for outdoor distance meshes. Keep material
// boundaries, coarse UV regions and normal directions separate. All retained vertex
// attributes come from the source mesh; nearby rendering/collision use the full mesh.
inline bool makeDistanceMesh(const NifMesh& source,NifMesh& out) {
    if(source.vertices.empty() || source.indices.size()<600)return false;
    Vec3 lo=source.vertices[0],hi=lo;
    for(const auto& p:source.vertices){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
    float step=std::clamp(std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z})/20.f,8.f,96.f);
    out={};out.diffuseTexture=source.diffuseTexture;
    std::vector<NifMeshPart> parts=source.parts;
    if(parts.empty()){NifMeshPart p;p.indexCount=source.indices.size();p.diffuseTexture=source.diffuseTexture;parts.push_back(p);}
    for(const auto& part:parts) {
        NifMeshPart target=part;target.firstIndex=out.indices.size();
        using Key=std::tuple<int,int,int,int,int,int>;
        std::map<Key,uint16_t> clusters;std::map<uint16_t,uint16_t> remap;std::set<std::tuple<uint16_t,uint16_t,uint16_t>> triangles;
        auto vertex=[&](uint16_t id)->int {
            auto existing=remap.find(id);if(existing!=remap.end())return existing->second;
            const auto& p=source.vertices[id];Vec3 n=id<source.normals.size()?source.normals[id]:Vec3{0,0,1};Vec2 uv=id<source.uvs.size()?source.uvs[id]:Vec2{0,0};
            unsigned axis=fabsf(n.y)>fabsf(n.x)?1:0;if(fabsf(n.z)>(axis?fabsf(n.y):fabsf(n.x)))axis=2;
            float component=axis==0?n.x:axis==1?n.y:n.z;int direction=axis*2+(component<0);
            Key key{int(floorf((p.x-lo.x)/step)),int(floorf((p.y-lo.y)/step)),int(floorf((p.z-lo.z)/step)),int(floorf(uv.u*2)),int(floorf(uv.v*2)),direction};
            auto found=clusters.find(key);if(found!=clusters.end())return remap[id]=found->second;
            if(out.vertices.size()>=65536)return -1;
            uint16_t result=out.vertices.size();out.vertices.push_back(p);out.normals.push_back(n);out.uvs.push_back(uv);out.colors.push_back(id<source.colors.size()?source.colors[id]:NifColor{});
            clusters.emplace(key,result);remap[id]=result;return result;
        };
        for(unsigned i=part.firstIndex;i+2<part.firstIndex+part.indexCount;i+=3) {
            int a=vertex(source.indices[i]),b=vertex(source.indices[i+1]),c=vertex(source.indices[i+2]);if(a<0 || b<0 || c<0)return false;
            if(a==b || b==c || a==c)continue;
            std::array<uint16_t,3> sorted{uint16_t(a),uint16_t(b),uint16_t(c)};std::sort(sorted.begin(),sorted.end());
            if(!triangles.emplace(sorted[0],sorted[1],sorted[2]).second)continue;
            out.indices.insert(out.indices.end(),{uint16_t(a),uint16_t(b),uint16_t(c)});
        }
        target.indexCount=out.indices.size()-target.firstIndex;if(target.indexCount)out.parts.push_back(target);
    }
    out.vertexCount=out.vertices.size();out.triangleCount=out.indices.size()/3;out.center=source.center;out.radius=source.radius;
    return !out.indices.empty() && out.indices.size()<source.indices.size()*.85f;
}
