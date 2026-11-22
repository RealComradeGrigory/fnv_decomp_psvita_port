#include "world_scene.h"
#include "physics/physics_engine.h"
#include "renderer/frustum_clip.h"
#include "renderer/mesh_lod.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
extern void logMsg(const char*,...);
namespace {
std::string lower(std::string s) {for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
Vec3 transform(const Mat4& m,const BasicVertex& v) {
    Vec3 r{m.m[3][0],m.m[3][1],m.m[3][2]};
    for(unsigned k=0;k<3;++k){r.x+=m.m[k][0]*v.position[k];r.y+=m.m[k][1]*v.position[k];r.z+=m.m[k][2]*v.position[k];}return r;
}
void appendVertex(NifMesh& out,const BasicVertex& v) {
    out.indices.push_back(static_cast<uint16_t>(out.vertices.size()));
    out.vertices.push_back({v.position[0],v.position[1],v.position[2]});out.normals.push_back({v.normal[0],v.normal[1],v.normal[2]});
    out.uvs.push_back({v.uv[0],v.uv[1]});out.colors.push_back({v.color[0],v.color[1],v.color[2],v.color[3]});
}
// Remove the exact near-cell rectangle from original coarse terrain. Preserve
// crossing polygons rather than dropping whole triangles at the LOD boundary.
bool trimNear(NifMesh& mesh,float minX,float minY,float maxX,float maxY) {
    NifMesh result=mesh;result.indices.clear();result.parts.clear();
    result.normals.resize(mesh.vertices.size(),{0,0,1});result.uvs.resize(mesh.vertices.size());result.colors.resize(mesh.vertices.size());
    for(const auto& part:mesh.parts) {
        NifMeshPart target=part;target.firstIndex=result.indices.size();
        for(unsigned i=part.firstIndex;i+2<part.firstIndex+part.indexCount;i+=3) {
            const auto& a=mesh.vertices[mesh.indices[i]];const auto& b=mesh.vertices[mesh.indices[i+1]];const auto& c=mesh.vertices[mesh.indices[i+2]];
            float left=std::min({a.x,b.x,c.x}),right=std::max({a.x,b.x,c.x}),bottom=std::min({a.y,b.y,c.y}),top=std::max({a.y,b.y,c.y});
            if(right<=minX || left>=maxX || top<=minY || bottom>=maxY) {result.indices.insert(result.indices.end(),mesh.indices.begin()+i,mesh.indices.begin()+i+3);continue;}
            if(left>=minX && right<=maxX && bottom>=minY && top<=maxY)continue;
            BasicVertex tri[3]{};
            for(unsigned k=0;k<3;++k) {
                unsigned id=mesh.indices[i+k];const auto& p=mesh.vertices[id];tri[k].position[0]=p.x;tri[k].position[1]=p.y;tri[k].position[2]=p.z;
                if(id<mesh.normals.size()){tri[k].normal[0]=mesh.normals[id].x;tri[k].normal[1]=mesh.normals[id].y;tri[k].normal[2]=mesh.normals[id].z;}
                if(id<mesh.uvs.size()){tri[k].uv[0]=mesh.uvs[id].u;tri[k].uv[1]=mesh.uvs[id].v;}
                for(unsigned c=0;c<4;++c)tri[k].color[c]=1;
                if(id<mesh.colors.size()){auto c=mesh.colors[id];tri[k].color[0]=c.r;tri[k].color[1]=c.g;tri[k].color[2]=c.b;tri[k].color[3]=c.a;}
            }
            for(unsigned region=0;region<4;++region) {
                FrustumClipper clip(Mat4::identity());for(auto& plane:clip.plane)for(float& f:plane)f=0;
                unsigned mask=1;
                if(region==0) {clip.plane[0][0]=-1;clip.plane[0][3]=minX;}
                if(region==1) {clip.plane[0][0]=1;clip.plane[0][3]=-maxX;}
                if(region>=2) {
                    clip.plane[0][0]=1;clip.plane[0][3]=-minX;clip.plane[1][0]=-1;clip.plane[1][3]=maxX;
                    clip.plane[2][1]=region==2?-1:1;clip.plane[2][3]=region==2?minY:-maxY;mask=7;
                }
                BasicVertex polygon[12];unsigned n=clip.clip(tri,mask,polygon);
                for(unsigned k=1;k+1<n;++k) {
                    if(result.vertices.size()+3>65536)return false;
                    appendVertex(result,polygon[0]);appendVertex(result,polygon[k]);appendVertex(result,polygon[k+1]);
                }
            }
        }
        target.indexCount=result.indices.size()-target.firstIndex;if(target.indexCount)result.parts.push_back(target);
    }
    result.vertexCount=result.vertices.size();result.triangleCount=result.indices.size()/3;result.diffuseTexture=mesh.diffuseTexture;
    mesh=std::move(result);return true;
}
}
bool WorldScene::readNif(const std::string& path,NifMesh& mesh) {
    auto slash=path.find_last_of("\\/");if(slash==std::string::npos)return false;
    std::vector<uint8_t> bytes;NifLoader nif;
    // LOD files already contain world-space coordinates; placed model roots
    // are replaced by the source REFR transform, just as interior references are.
    bool placed=path.find("\\landscape\\lod\\")==std::string::npos;
    if(!m_archive->extractFile(path.substr(0,slash).c_str(),path.substr(slash+1).c_str(),bytes) || !nif.load(bytes.data(),bytes.size(),placed))return false;
    mesh=nif.getMesh();return true;
}
bool WorldScene::load(MapDatabase& data,uint32_t world,float x,float y,BSAReader& meshes,GxmRenderer& renderer,PhysicsEngine& physics) {
    if(!data.selectWorld(world))return false;
    cleanup(renderer);m_data=&data;m_archive=&meshes;m_world=world;
    m_eyeX=x;m_eyeY=y;
    const auto* info=data.world(world);m_name=info->name;m_editor=lower(info->editor);
    auto discover=[&](const std::string& editor) {
        std::string folder="meshes\\landscape\\lod\\"+editor;
        for(unsigned type=0;type<2;++type) {
            std::string dir=folder+(type?"\\blocks":"");
            for(auto file:meshes.listFiles(dir.c_str())) {
                file=lower(file);std::string prefix=editor+".";if(file.rfind(prefix,0)!=0)continue;
                int level,gx,gy;
                if(sscanf(file.c_str()+prefix.size(),"level%d.x%d.y%d.nif",&level,&gx,&gy)!=3 || level<4 || level>32 || (level&(level-1)))continue;
                (type?m_objectLod:m_lod)[{level,gx,gy}].path=dir+"\\"+file;
            }
        }
    };
    discover(m_editor);
    if(m_lod.empty() && info->parent && data.world(info->parent)) {m_editor=lower(data.world(info->parent)->editor);discover(m_editor);}
    renderer.setMaterialTextureLimit(256);
    if(!refresh(int(floorf(x/4096)),int(floorf(y/4096)),renderer,physics)) {cleanup(renderer);return false;}
    logMsg("WorldScene: original world %s (%08X), %u terrain LOD assets, %u nearby references",m_name.c_str(),world,(unsigned)m_lod.size(),(unsigned)m_instances.size());
    return true;
}
void WorldScene::chooseLod(const LodKey& key,float x,float y) {
    auto found=m_lod.find(key);if(found==m_lod.end())return;
    int level=std::get<0>(key),gx=std::get<1>(key),gy=std::get<2>(key);
    float minX=gx*4096.f,minY=gy*4096.f,size=level*4096.f;
    float dx=std::max({minX-x,0.f,x-minX-size}),dy=std::max({minY-y,0.f,y-minY-size});
    const float range=m_graphics.viewDistance()+4096;
    if(dx*dx+dy*dy>range*range)return;
    const int minimum[]={16,8,4,4};const float thresholds[]={.0025f,.04f,.16f,.36f};
    bool split=level>minimum[m_graphics.lod] && dx*dx+dy*dy<size*size*thresholds[m_graphics.lod];
    if(split) {
        int child=level/2;
        for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx)if(!m_lod.count({child,gx+xx*child,gy+yy*child}))split=false;
        if(split) {for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx)chooseLod({child,gx+xx*child,gy+yy*child},x,y);return;}
    }
    m_selectedLod.push_back(key);
}
#include "world_streaming.h"
void WorldScene::render(GxmRenderer& renderer) {
    renderer.beginStaticBatch();
    for(const auto& pair:m_lod)if(pair.second.gpu.isValid())renderer.drawMeshInstance(pair.second.gpu,nullptr,Mat4::identity());
    for(const auto& tile:m_tiles)if(tile.terrain.isValid())renderer.drawMeshInstance(tile.terrain,nullptr,Mat4::identity());
    for(const auto& pair:m_objectLod)if(pair.second.gpu.isValid())renderer.drawMeshInstance(pair.second.gpu,nullptr,Mat4::identity());
    for(int pass=0;pass<2;++pass)for(const auto& object:m_instances) {
        if(object.detailGroup) {
            float limit=object.detailGroup==1?m_graphics.propDistance():m_graphics.plantDistance();
            // Use the transformed mesh bounds, including its radius, so a
            // large fence/rock does not disappear merely because its pivot is far.
            const auto& mesh=*object.mesh;
            BasicVertex center{};center.position[0]=(mesh.boundsMin.x+mesh.boundsMax.x)*.5f;center.position[1]=(mesh.boundsMin.y+mesh.boundsMax.y)*.5f;center.position[2]=(mesh.boundsMin.z+mesh.boundsMax.z)*.5f;
            Vec3 p=transform(object.worldMatrix,center);
            float sx=mesh.boundsMax.x-mesh.boundsMin.x,sy=mesh.boundsMax.y-mesh.boundsMin.y,sz=mesh.boundsMax.z-mesh.boundsMin.z;
            const auto& m=object.worldMatrix.m;float scale=sqrtf(m[0][0]*m[0][0]+m[0][1]*m[0][1]+m[0][2]*m[0][2]);
            float radius=.5f*sqrtf(sx*sx+sy*sy+sz*sz)*scale;
            float dx=p.x-m_eyeX,dy=p.y-m_eyeY;
            if(dx*dx+dy*dy>(limit+radius)*(limit+radius))continue;
        }
        const GxmMesh* draw=object.mesh;
        if(m_graphics.lod<3 && object.distanceMesh && object.distanceMesh->isValid()) {
            const auto& mesh=*object.mesh;BasicVertex center{};
            center.position[0]=(mesh.boundsMin.x+mesh.boundsMax.x)*.5f;center.position[1]=(mesh.boundsMin.y+mesh.boundsMax.y)*.5f;center.position[2]=(mesh.boundsMin.z+mesh.boundsMax.z)*.5f;
            Vec3 p=transform(object.worldMatrix,center);const auto& m=object.worldMatrix.m;
            float sx=mesh.boundsMax.x-mesh.boundsMin.x,sy=mesh.boundsMax.y-mesh.boundsMin.y,sz=mesh.boundsMax.z-mesh.boundsMin.z;
            float scale=sqrtf(m[0][0]*m[0][0]+m[0][1]*m[0][1]+m[0][2]*m[0][2]);float radius=.5f*sqrtf(sx*sx+sy*sy+sz*sz)*scale;
            const float thresholds[]={768,1536,3072};float limit=thresholds[m_graphics.lod]+radius;
            float dx=p.x-m_eyeX,dy=p.y-m_eyeY;if(dx*dx+dy*dy>limit*limit)draw=object.distanceMesh;
        }
        renderer.drawMeshInstance(*draw,nullptr,object.worldMatrix,pass);
    }
    renderer.endStaticBatch();
}
const MapDoor* WorldScene::nearbyDoor(float x,float y,float z) const {
    const MapDoor* nearest=nullptr;float best=150*150;
    for(const auto& tile:m_tiles)for(const auto& door:tile.data.doors) {
        float dx=x-door.source[0],dy=y-door.source[1],dz=z-door.source[2];float d=dx*dx+dy*dy+dz*dz*.2f;
        if(d<best){best=d;nearest=&door;}
    }
    return nearest;
}
void WorldScene::cleanup(GxmRenderer& renderer) {
    abortStream(renderer);m_lodSources.clear();m_lodSourceBytes=0;m_sourceAge=0;
    for(auto& pair:m_lod)renderer.destroyGxmMesh(pair.second.gpu);
    m_lod.clear();
    for(auto& pair:m_objectLod)renderer.destroyGxmMesh(pair.second.gpu);
    m_objectLod.clear();m_selectedLod.clear();
    for(auto& tile:m_tiles)renderer.destroyGxmMesh(tile.terrain);
    m_tiles.clear();
    for(auto& pair:m_models){renderer.destroyGxmMesh(pair.second.gpu);renderer.destroyGxmMesh(pair.second.distanceGpu);}
    m_models.clear();m_instances.clear();
    m_meshBytes=0;m_world=0;m_data=nullptr;m_archive=nullptr;renderer.setMaterialTextureLimit(512);
}
