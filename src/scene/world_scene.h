#pragma once
#include "map_data.h"
#include "cell_scene.h"
#include "ui/graphics_settings.h"
#include <map>
#include <tuple>
#include <set>
#include "physics/physics_engine.h"

class WorldScene {
public:
    bool load(MapDatabase& data,uint32_t world,float x,float y,BSAReader& meshes,
              GxmRenderer& renderer,PhysicsEngine& physics);
    bool update(float x,float y,GxmRenderer& renderer,PhysicsEngine& physics);
    void render(GxmRenderer& renderer);
    void cleanup(GxmRenderer& renderer);
    const MapDoor* nearbyDoor(float x,float y,float z) const;
    const std::string& name() const {return m_name;}
    uint32_t worldID() const {return m_world;}
    bool ready() const {return m_world!=0;}
    bool streaming() const {return m_pending.active;}
    bool constrainToLoaded(float& x,float& y) const;
    void setGraphics(const GraphicsSettings& settings) {
        if(!settings.valid())return;
        m_refreshGraphics|=settings.distance!=m_graphics.distance || settings.lod!=m_graphics.lod || settings.distantObjects!=m_graphics.distantObjects;
        m_graphics=settings;
    }
private:
    using LodKey=std::tuple<int,int,int>;
    struct Lod {std::string path;GxmMesh gpu;};
    struct CollisionMesh {std::shared_ptr<const GxmCpuGeometry> geometry;std::vector<uint16_t> indices;};
    struct Cached {GxmMesh gpu,distanceGpu;size_t bytes=0;CollisionMesh collision;};
    struct Tile {MapTile data;GxmMesh terrain;CollisionMesh collision;bool borrowed=false;};
    struct CollisionInput {const CollisionMesh* mesh;Mat4 matrix;};
    struct StreamStats {unsigned updates=0,reusedTiles=0,newModels=0,collisionTriangles=0,maxSliceTriangles=0,maxSliceModels=0;uint32_t maxSliceUs=0;};
    struct Pending {
        bool active=false,failed=false,ready=false,nearComplete=false;int x=0,y=0;unsigned phase=0,cursor=0,lodPass=0;
        std::vector<Tile> tiles;std::vector<const MapReference*> references;
        std::set<uint32_t> wanted,attempted;std::vector<PlacedObjectInstance> instances;
        std::vector<LodKey> keys;std::map<LodKey,GxmMesh> terrainLod,objectLod;
        std::vector<CollisionInput> collisionInputs;std::vector<Vec3> transformed;
        size_t collisionInput=0,collisionIndex=0,desiredBytes=0,desiredModels=0;
        PhysicsEngine physics;StreamStats stats;unsigned failedModels=0,budgetedModels=0;
    } m_pending;
    struct Source {NifMesh mesh;size_t bytes=0;uint64_t age=0;};
    std::map<std::string,Source> m_lodSources;size_t m_lodSourceBytes=0;uint64_t m_sourceAge=0;
    StreamStats m_lastStream;
    MapDatabase* m_data=nullptr;BSAReader* m_archive=nullptr;
    uint32_t m_world=0;int m_x=0,m_y=0;std::string m_name,m_editor;
    std::vector<Tile> m_tiles;
    std::unordered_map<uint32_t,Cached> m_models;
    std::vector<PlacedObjectInstance> m_instances;
    std::map<LodKey,Lod> m_lod,m_objectLod;
    std::vector<LodKey> m_selectedLod;
    size_t m_meshBytes=0;
    GraphicsSettings m_graphics;bool m_refreshGraphics=false;
    float m_eyeX=0,m_eyeY=0;
    bool refresh(int x,int y,GxmRenderer& renderer,PhysicsEngine& physics);
    void chooseLod(const LodKey& key,float x,float y);
    bool readNif(const std::string& path,NifMesh& mesh);
    bool loadLodFor(const std::string& path,GxmMesh& gpu,bool terrain,GxmRenderer& renderer);
    bool beginStream(int x,int y,GxmRenderer& renderer);
    bool streamUnit(GxmRenderer& renderer,unsigned& heavy,unsigned& triangles);
    bool stepStream(GxmRenderer& renderer,bool synchronous=false);
    void commitStream(GxmRenderer& renderer,PhysicsEngine& physics);
    void abortStream(GxmRenderer& renderer);
    void collectModels(const std::set<uint32_t>& wanted,GxmRenderer& renderer);
    bool buildTerrain(Tile& tile,GxmRenderer& renderer);
    static CollisionMesh collisionSource(const NifMesh& mesh,const GxmMesh& gpu);
};
