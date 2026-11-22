#pragma once
#include <array>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct MapDoor {
    uint32_t sourceRef,targetWorld,targetCell;
    float source[3],target[3],yaw;
};
static_assert(sizeof(MapDoor)==40,"map door format");
struct MapReference {uint32_t form,base;float position[3],rotation[3],scale;};
static_assert(sizeof(MapReference)==36,"map ref format");
struct MapWorld {uint32_t form,parent;char name[64],editor[64];float defaultLand,water,sky[3];};
static_assert(sizeof(MapWorld)==156,"world catalog format");
struct MapTile {
    int32_t x=0,y=0;uint32_t form=0;
    std::vector<MapReference> references;
    std::vector<MapDoor> doors;
    std::array<float,1089> heights{};
    std::array<int8_t,3267> normals{};
    bool hasTerrain=false,hasNormals=false;
};

class MapDatabase {
public:
    ~MapDatabase();
    bool open(const char* root);
    void close();
    bool selectWorld(uint32_t form);
    bool readTile(int32_t x,int32_t y,MapTile& tile);
    const MapWorld* world(uint32_t form) const;
    const std::string* model(uint32_t form) const;
    std::string interiorPath(uint32_t form) const;
    const std::string* interiorName(uint32_t form) const;
    uint32_t interiorID(const char* name) const;
    uint32_t selectedWorld() const {return m_selected;}
    bool ready() const {return !m_worlds.empty();}
    static bool decodeTerrain(const uint8_t* data,size_t size,MapTile& tile);
private:
    struct Entry {int32_t x,y;uint32_t offset,packed,raw,form;};
    std::string m_root;
    std::unordered_map<uint32_t,std::string> m_models,m_interiors,m_interiorEditors;
    std::unordered_map<uint32_t,MapWorld> m_worlds;
    std::unordered_map<uint64_t,Entry> m_tiles;
    uint32_t m_selected=0;FILE* m_pack=nullptr;size_t m_packSize=0;
};
