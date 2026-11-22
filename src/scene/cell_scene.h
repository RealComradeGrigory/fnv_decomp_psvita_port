#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "../renderer/gxm_renderer.h"
#include "../bsa/bsa_reader.h"
#include "../nif/nif_loader.h"
#include "../dds/dds_loader.h"
#include "map_data.h"

class PhysicsEngine;

// Cell binary file header (88 bytes)
struct CellFileHeader {
    char     magic[4];       // "CELL"
    uint32_t version;        // 1
    uint32_t formId;
    uint32_t flags;
    char     cellName[64];
    uint32_t numObjects;
    float    spawnX, spawnY, spawnZ;
    float    spawnYaw, spawnPitch;
};

// On-disk object entry (164 bytes)
struct CellObjectOnDisk {
    uint32_t refFormId;
    uint32_t baseFormId;
    char     modelPath[128];
    float    posX, posY, posZ;
    float    rotX, rotY, rotZ; // Euler angles in radians
    float    scale;
};

// In-memory placed object instance
struct PlacedObjectInstance {
    uint32_t    refFormId = 0;
    uint32_t    baseFormId = 0;
    std::string modelPath;
    Mat4        worldMatrix;
    GxmMesh*    mesh = nullptr;
    GxmTexture* texture = nullptr;
    GxmMesh* distanceMesh = nullptr;
    uint8_t detailGroup = 0; // Outdoors: 1=small clutter, 2=vegetation; architecture stays intact.
};

class CellScene {
public:
    CellScene() = default;
    CellScene(const CellScene&) = delete;
    CellScene& operator=(const CellScene&) = delete;
    CellScene(CellScene&&) = default;
    CellScene& operator=(CellScene&&) = default;
    ~CellScene();

    // Load a .cell binary file and resolve all meshes/textures through the BSAs
    bool load(const char* cellFilePath,
              BSAReader& meshBsa,
              BSAReader& texBsa1,
              BSAReader* texBsa2,
              GxmRenderer& renderer);

    // Render all visible placed objects in the cell
    void render(GxmRenderer& renderer);
    void buildCollision(PhysicsEngine& physics) const;

    // Free all GPU resources and clear scene
    void cleanup(GxmRenderer& renderer);
    const MapDoor* nearbyDoor(float x,float y,float z) const;

    // Accessors
    const std::string& getCellName() const { return m_cellName; }
    uint32_t getObjectCount() const { return (uint32_t)m_objects.size(); }
    uint32_t getUniqueMeshCount() const { return (uint32_t)m_meshCache.size(); }
    uint32_t getUniqueTextureCount() const { return (uint32_t)m_textureCache.size(); }

    void getSpawnPoint(float& x, float& y, float& z, float& yaw, float& pitch) const {
        x = m_spawnX; y = m_spawnY; z = m_spawnZ;
        yaw = m_spawnYaw; pitch = m_spawnPitch;
    }

    float getFloorZ() const { return m_floorZ; }
    void getBounds(float& minX, float& maxX, float& minY, float& maxY) const {
        minX = m_minX; maxX = m_maxX;
        minY = m_minY; maxY = m_maxY;
    }

private:
    bool loadInternal(const char* cellFilePath, BSAReader& meshBsa, BSAReader& texBsa1,
                      BSAReader* texBsa2, GxmRenderer& renderer);
    std::string m_cellName;
    float       m_spawnX = 0.0f;
    float       m_spawnY = 0.0f;
    float       m_spawnZ = 0.0f;
    float       m_spawnYaw = 0.0f;
    float       m_spawnPitch = 0.0f;

    float       m_floorZ = 0.0f;
    float       m_minX = -10000.0f;
    float       m_maxX = 10000.0f;
    float       m_minY = -10000.0f;
    float       m_maxY = 10000.0f;

    std::vector<PlacedObjectInstance> m_objects;
    std::vector<MapDoor> m_doors;

    // Cache unique meshes and textures so each is uploaded only once
    std::unordered_map<std::string, GxmMesh>    m_meshCache;
    std::unordered_map<std::string, GxmTexture> m_textureCache;
    std::unordered_map<std::string, std::string> m_meshToTexturePath;

    // Helper to extract a texture from either BSA
    bool extractTexture(BSAReader& texBsa1, BSAReader* texBsa2,
                        const std::string& path, std::vector<uint8_t>& outData);
};
