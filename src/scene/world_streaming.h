// Included only by world_scene.cpp. GPU uploads/submissions stay on the main thread.
#ifdef FNV_VITA_TARGET
#include <psp2/kernel/processmgr.h>
#else
#include <chrono>
#endif
namespace {
uint32_t streamClockUs() {
#ifdef FNV_VITA_TARGET
    return sceKernelGetProcessTimeLow();
#else
    return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
}
WorldScene::CollisionMesh WorldScene::collisionSource(const NifMesh& mesh,const GxmMesh& gpu) {
    CollisionMesh out;out.geometry=gpu.cpuGeometry;
    if(mesh.parts.empty())out.indices=mesh.indices;
    else for(const auto& p:mesh.parts)if(!p.transparent)
        out.indices.insert(out.indices.end(),mesh.indices.begin()+p.firstIndex,mesh.indices.begin()+p.firstIndex+p.indexCount);
    return out;
}
void WorldScene::collectModels(const std::set<uint32_t>& wanted,GxmRenderer& renderer) {
    for(auto it=m_models.begin();it!=m_models.end();)if(!wanted.count(it->first)) {
        m_meshBytes-=it->second.bytes;renderer.destroyGxmMesh(it->second.gpu);
        renderer.destroyGxmMesh(it->second.distanceGpu);it=m_models.erase(it);
    } else ++it;
}
void WorldScene::abortStream(GxmRenderer& renderer) {
    if(!m_pending.active)return;
    for(auto& tile:m_pending.tiles)if(!tile.borrowed)renderer.destroyGxmMesh(tile.terrain);
    for(auto& p:m_pending.terrainLod)renderer.destroyGxmMesh(p.second);
    for(auto& p:m_pending.objectLod)renderer.destroyGxmMesh(p.second);
    // Newly prepared models are not referenced by active draws; active models survive.
    std::set<uint32_t> active;
    for(const auto& tile:m_tiles)for(const auto& ref:tile.data.references)active.insert(ref.base);
    collectModels(active,renderer);
    m_pending=Pending{};
}
bool WorldScene::beginStream(int x,int y,GxmRenderer& renderer) {
    abortStream(renderer);
    m_pending.active=true;m_pending.x=x;m_pending.y=y;
    // Reserve before storing reference pointers, which remain stable through preparation.
    m_pending.tiles.reserve(9);
    return true;
}
bool WorldScene::loadLodFor(const std::string& path,GxmMesh& gpu,bool terrain,GxmRenderer& renderer) {
    auto found=m_lodSources.find(path);
    if(found==m_lodSources.end()) {
        Source source;if(!readNif(path,source.mesh))return false;
        const auto& mesh=source.mesh;
        source.bytes=mesh.vertices.size()*sizeof(Vec3)+mesh.normals.size()*sizeof(Vec3)+mesh.indices.size()*2+
            mesh.uvs.size()*sizeof(mesh.uvs[0])+mesh.colors.size()*sizeof(mesh.colors[0]);
        // Keep a bounded CPU cache of original coarse meshes, before near-region clipping.
        while(!m_lodSources.empty() && m_lodSourceBytes+source.bytes>16*1024*1024) {
            auto oldest=std::min_element(m_lodSources.begin(),m_lodSources.end(),[](const auto& a,const auto& b){return a.second.age<b.second.age;});
            m_lodSourceBytes-=oldest->second.bytes;m_lodSources.erase(oldest);
        }
        if(source.bytes>16*1024*1024) {
            NifMesh mesh=std::move(source.mesh);
            if((!terrain || m_pending.nearComplete) && !trimNear(mesh,(m_pending.x-1)*4096.f,(m_pending.y-1)*4096.f,(m_pending.x+2)*4096.f,(m_pending.y+2)*4096.f))return false;
            return mesh.indices.empty() || renderer.createGxmMesh(mesh,gpu);
        }
        m_lodSourceBytes+=source.bytes;found=m_lodSources.emplace(path,std::move(source)).first;
    }
    found->second.age=++m_sourceAge;NifMesh mesh=found->second.mesh;
    if((!terrain || m_pending.nearComplete) && !trimNear(mesh,(m_pending.x-1)*4096.f,(m_pending.y-1)*4096.f,(m_pending.x+2)*4096.f,(m_pending.y+2)*4096.f))return false;
    return mesh.indices.empty() || renderer.createGxmMesh(mesh,gpu);
}
bool WorldScene::buildTerrain(Tile& tile,GxmRenderer& renderer) {
    if(!tile.data.hasTerrain)return true;
        NifMesh mesh;mesh.vertexCount=1089;
        int bx=int(floorf(tile.data.x/4.f))*4,by=int(floorf(tile.data.y/4.f))*4;
        char texture[256];snprintf(texture,sizeof(texture),"textures\\landscape\\lod\\%s\\diffuse\\%s.n.level4.x%d.y%d.dds",m_editor.c_str(),m_editor.c_str(),bx,by);
        NifMeshPart part;part.firstIndex=0;part.diffuseTexture=texture;
        for(int yy=0;yy<33;++yy)for(int xx=0;xx<33;++xx) {
            unsigned i=yy*33+xx;mesh.vertices.push_back({tile.data.x*4096.f+xx*128.f,tile.data.y*4096.f+yy*128.f,tile.data.heights[i]});
            Vec3 n{0,0,1};if(tile.data.hasNormals)n={tile.data.normals[i*3]/127.f,tile.data.normals[i*3+1]/127.f,tile.data.normals[i*3+2]/127.f};mesh.normals.push_back(n);
            mesh.uvs.push_back({1.f-(tile.data.x-bx+xx/32.f)/4.f,(tile.data.y-by+yy/32.f)/4.f});
        }
        for(int yy=0;yy<32;++yy)for(int xx=0;xx<32;++xx) {
            uint16_t a=yy*33+xx,b=a+1,c=a+33,d=c+1;
            if((xx+yy)&1)mesh.indices.insert(mesh.indices.end(),{a,b,c,b,d,c});else mesh.indices.insert(mesh.indices.end(),{a,b,d,a,d,c});
        }
        part.indexCount=mesh.indices.size();mesh.parts.push_back(part);mesh.triangleCount=mesh.indices.size()/3;
        if(!renderer.createGxmMesh(mesh,tile.terrain))return false;
        tile.collision=collisionSource(mesh,tile.terrain);return true;

}
bool WorldScene::streamUnit(GxmRenderer& renderer,unsigned& heavy,unsigned& triangles) {
    auto& p=m_pending;
    if(p.phase==0) {
        if(p.cursor<9) {
            int xx=p.x-1+int(p.cursor%3),yy=p.y-1+int(p.cursor/3);++p.cursor;
            auto old=std::find_if(m_tiles.begin(),m_tiles.end(),[&](const Tile& t){return t.data.x==xx && t.data.y==yy;});
            if(old!=m_tiles.end()) {
                p.tiles.push_back(*old);p.tiles.back().borrowed=true;++p.stats.reusedTiles;
            } else {
                ++heavy;Tile tile;
                if(m_data->readTile(xx,yy,tile.data)) {
                    if(!buildTerrain(tile,renderer)) {renderer.destroyGxmMesh(tile.terrain);p.failed=true;return false;}
                    p.tiles.push_back(std::move(tile));
                }
            }
            return true;
        }
        if(p.tiles.empty()){p.failed=true;return false;}
        p.nearComplete=p.tiles.size()==9;
        for(const auto& tile:p.tiles) {
            p.nearComplete&=tile.data.hasTerrain;
            for(const auto& ref:tile.data.references){p.references.push_back(&ref);p.wanted.insert(ref.base);}
        }
        float eyeX=(p.x+.5f)*4096,eyeY=(p.y+.5f)*4096;
        std::sort(p.references.begin(),p.references.end(),[&](const MapReference* a,const MapReference* b) {
            float ax=a->position[0]-eyeX,ay=a->position[1]-eyeY,bx=b->position[0]-eyeX,by=b->position[1]-eyeY;return ax*ax+ay*ay<bx*bx+by*by;
        });
        // Account for shared assets before admitting new models.
        for(auto base:p.wanted) {
            auto it=m_models.find(base);if(it!=m_models.end()){p.desiredBytes+=it->second.bytes;++p.desiredModels;}
        }
        p.phase=1;p.cursor=0;return true;
    }
    if(p.phase==1) {
        if(p.cursor<p.references.size()) {
            const auto* ref=p.references[p.cursor++];auto found=m_models.find(ref->base);
            if(found==m_models.end() && p.attempted.insert(ref->base).second) {
                ++heavy;const auto* path=m_data->model(ref->base);NifMesh mesh;
                std::string pathName=path?lower(*path):"";if(pathName.rfind("meshes\\",0)!=0)pathName="meshes\\"+pathName;
                if(pathName.rfind("meshes\\effects\\",0)==0)return true;
                if(!path || !readNif(pathName,mesh)){++p.failedModels;return true;}
                size_t bytes=mesh.vertices.size()*sizeof(BasicVertex)+(mesh.indices.size()+mesh.parts.size())*2;
                // Outgoing and incoming generations coexist, with a bounded transient allowance.
                if(p.desiredBytes+bytes>64*1024*1024 || p.desiredModels>=768 || m_meshBytes+bytes>88*1024*1024 || m_models.size()>=1024){++p.budgetedModels;return true;}
                Cached cached{};cached.bytes=bytes;
                if(!renderer.createGxmMesh(mesh,cached.gpu)){++p.failedModels;return true;}
                cached.collision=collisionSource(mesh,cached.gpu);
                NifMesh distance;
                if(makeDistanceMesh(mesh,distance)) {
                    size_t lowBytes=distance.vertices.size()*sizeof(BasicVertex)+(distance.indices.size()+distance.parts.size())*2;
                    if(p.desiredBytes+bytes+lowBytes<=64*1024*1024 && m_meshBytes+bytes+lowBytes<=88*1024*1024 && renderer.createGxmMesh(distance,cached.distanceGpu))cached.bytes+=lowBytes;
                }
                p.desiredBytes+=cached.bytes;++p.desiredModels;m_meshBytes+=cached.bytes;++p.stats.newModels;
                found=m_models.emplace(ref->base,std::move(cached)).first;
            }
            if(found==m_models.end())return true;
            PlacedObjectInstance placed;placed.refFormId=ref->form;placed.baseFormId=ref->base;placed.mesh=&found->second.gpu;
            placed.distanceMesh=&found->second.distanceGpu;
            placed.worldMatrix=Mat4::createTransform(ref->position[0],ref->position[1],ref->position[2],ref->rotation[0],ref->rotation[1],ref->rotation[2],ref->scale);
            const auto* sourcePath=m_data->model(ref->base);std::string path=sourcePath?lower(*sourcePath):"";
            if(path.rfind("clutter\\",0)==0 || path.rfind("weapons\\",0)==0 || path.rfind("ammo\\",0)==0 || path.rfind("armor\\",0)==0)placed.detailGroup=1;
            if(path.rfind("trees\\",0)==0 || path.rfind("landscape\\trees\\",0)==0 || path.rfind("landscape\\plants\\",0)==0 || path.find("bush")!=std::string::npos)placed.detailGroup=2;
            p.instances.push_back(std::move(placed));return true;
        }
        // chooseLod uses a temporary selection; active GPU ownership stays unchanged.
        auto selected=std::move(m_selectedLod);m_selectedLod.clear();
        int rootLevel=0;for(const auto& pair:m_lod)rootLevel=std::max(rootLevel,std::get<0>(pair.first));
        for(const auto& pair:m_lod)if(std::get<0>(pair.first)==rootLevel)chooseLod(pair.first,(p.x+.5f)*4096,(p.y+.5f)*4096);
        p.keys=std::move(m_selectedLod);m_selectedLod=std::move(selected);
        p.phase=2;p.cursor=0;return true;
    }
    if(p.phase==2) {
        if(p.cursor<p.keys.size()*2) {
            bool objects=p.cursor%2;const auto& key=p.keys[p.cursor/2];++p.cursor;
            if(objects && !m_graphics.distantObjects)return true;
            auto& source=objects?m_objectLod:m_lod;auto found=source.find(key);if(found==source.end())return true;
            ++heavy;auto& dest=objects?p.objectLod:p.terrainLod;
            if(!loadLodFor(found->second.path,dest[key],!objects,renderer))logMsg("WorldScene: missing LOD %s",found->second.path.c_str());
            return true;
        }
        for(const auto& tile:p.tiles)if(tile.terrain.isValid())p.collisionInputs.push_back({&tile.collision,Mat4::identity()});
        for(const auto& placed:p.instances)p.collisionInputs.push_back({&m_models.at(placed.baseFormId).collision,placed.worldMatrix});
        size_t count=0;for(const auto& input:p.collisionInputs)count+=input.mesh->indices.size()/3;
        p.physics.reserveGeometry(count);++heavy;
        p.phase=3;return true;
    }
    if(p.phase==3) {
        if(p.collisionInput<p.collisionInputs.size()) {
            const auto& input=p.collisionInputs[p.collisionInput];const auto& source=*input.mesh;
            if(p.transformed.empty()) {
                p.transformed.reserve(source.geometry->positions.size());
                for(const auto& vertex:source.geometry->positions) {
                    BasicVertex v{};v.position[0]=vertex.x;v.position[1]=vertex.y;v.position[2]=vertex.z;
                    p.transformed.push_back(transform(input.matrix,v));
                }
            }
            unsigned chunk=triangles<8192?std::min(256u,8192-triangles):256;
            size_t end=std::min(source.indices.size(),p.collisionIndex+chunk*3);
            for(;p.collisionIndex+2<end;p.collisionIndex+=3) {
                p.physics.addTriangle(p.transformed[source.indices[p.collisionIndex]],p.transformed[source.indices[p.collisionIndex+1]],p.transformed[source.indices[p.collisionIndex+2]]);
                ++triangles;++p.stats.collisionTriangles;
            }
            if(p.collisionIndex+2>=source.indices.size()){++p.collisionInput;p.collisionIndex=0;p.transformed.clear();}
            return true;
        }
        p.ready=true;return true;
    }
    return false;
}
bool WorldScene::stepStream(GxmRenderer& renderer,bool synchronous) {
    if(!m_pending.active || m_pending.failed)return false;
    if(m_pending.ready)return true;
    auto& p=m_pending;uint32_t start=streamClockUs();unsigned heavy=0,triangles=0;
    do {
        if(!streamUnit(renderer,heavy,triangles))break;
        if(!synchronous && (heavy>=1 || triangles>=8192 || streamClockUs()-start>=4000))break;
    } while(!p.ready && !p.failed);
    ++p.stats.updates;p.stats.maxSliceTriangles=std::max(p.stats.maxSliceTriangles,triangles);
    p.stats.maxSliceModels=std::max(p.stats.maxSliceModels,heavy);
    p.stats.maxSliceUs=std::max(p.stats.maxSliceUs,streamClockUs()-start);
    return !p.failed;
}
void WorldScene::commitStream(GxmRenderer& renderer,PhysicsEngine& physics) {
    auto& p=m_pending;if(!p.ready)return;
    // Old GPU buffers can still belong to the previous submitted frame.
    renderer.waitForGpu();
    for(auto& tile:p.tiles)if(tile.borrowed) {
        for(auto& old:m_tiles)if(old.data.x==tile.data.x && old.data.y==tile.data.y)old.terrain={};
        tile.borrowed=false;
    }
    for(auto& tile:m_tiles)renderer.destroyGxmMesh(tile.terrain);
    for(auto& pair:m_lod)renderer.destroyGxmMesh(pair.second.gpu);
    for(auto& pair:m_objectLod)renderer.destroyGxmMesh(pair.second.gpu);
    for(auto& pair:p.terrainLod){m_lod.at(pair.first).gpu=std::move(pair.second);pair.second={};}
    for(auto& pair:p.objectLod){m_objectLod.at(pair.first).gpu=std::move(pair.second);pair.second={};}
    m_tiles=std::move(p.tiles);m_instances=std::move(p.instances);m_selectedLod=std::move(p.keys);
    physics.swapGeometry(p.physics);m_x=p.x;m_y=p.y;m_lastStream=p.stats;
    collectModels(p.wanted,renderer);
    logMsg("WorldScene: grid (%d,%d), tiles=%u models=%u instances=%u geometry=%.1f MiB failed=%u budgeted=%u LOD=%u; stream updates=%u reused=%u new=%u collision=%u maxSlice=%u us/%u assets/%u triangles",m_x,m_y,(unsigned)m_tiles.size(),(unsigned)m_models.size(),(unsigned)m_instances.size(),m_meshBytes/1048576.,p.failedModels,p.budgetedModels,(unsigned)m_selectedLod.size(),p.stats.updates,p.stats.reusedTiles,p.stats.newModels,p.stats.collisionTriangles,p.stats.maxSliceUs,p.stats.maxSliceModels,p.stats.maxSliceTriangles);
    m_pending=Pending{};
}
bool WorldScene::refresh(int x,int y,GxmRenderer& renderer,PhysicsEngine& physics) {
    beginStream(x,y,renderer);m_refreshGraphics=false;
    if(!stepStream(renderer,true)){abortStream(renderer);return false;}
    commitStream(renderer,physics);return true;
}
bool WorldScene::update(float x,float y,GxmRenderer& renderer,PhysicsEngine& physics) {
    float dx=x-m_eyeX,dy=y-m_eyeY;m_eyeX=x;m_eyeY=y;
    int gx=int(floorf(x/4096)),gy=int(floorf(y/4096));
    if(m_refreshGraphics) {beginStream(gx,gy,renderer);m_refreshGraphics=false;}
    else if(gx!=m_x || gy!=m_y) {
        if(!m_pending.active || m_pending.x!=gx || m_pending.y!=gy)beginStream(gx,gy,renderer);
    } else {
        int tx=gx,ty=gy;
        float fx=x-gx*4096,fy=y-gy*4096;
        // Start early only in the direction of actual movement.
        if(fabsf(dx)>=fabsf(dy) && fabsf(dx)>.01f) {
            if(dx>0 && fx>3072)++tx;else if(dx<0 && fx<1024)--tx;
        } else if(fabsf(dy)>.01f) {
            if(dy>0 && fy>3072)++ty;else if(dy<0 && fy<1024)--ty;
        }
        if((tx!=gx || ty!=gy) && (!m_pending.active || m_pending.x!=tx || m_pending.y!=ty))beginStream(tx,ty,renderer);
    }
    if(m_pending.active) {
        if(!stepStream(renderer)){abortStream(renderer);return false;}
        // Keep a prepared neighbor until it is actually entered. Never ping-pong
        // the active center just because the player is near a boundary.
        if(m_pending.ready && m_pending.x==gx && m_pending.y==gy)commitStream(renderer,physics);
    }
    return true;
}
bool WorldScene::constrainToLoaded(float& x,float& y) const {
    float oldX=x,oldY=y;
    x=std::max((m_x-1)*4096.f+128,std::min((m_x+2)*4096.f-128,x));
    y=std::max((m_y-1)*4096.f+128,std::min((m_y+2)*4096.f-128,y));
    return x!=oldX || y!=oldY;
}
