#include "cell_scene.h"
#include "physics/physics_engine.h"
#include <psp2/io/fcntl.h>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>

extern void logMsg(const char* fmt, ...);

static void toLowerInPlace(std::string& s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

static std::string normalizeModelPath(const char* path) {
    std::string s(path);
    toLowerInPlace(s);
    for (auto& c : s) {
        if (c == '/') c = '\\';
    }
    // Prefix "meshes\" if not present
    if (s.rfind("meshes\\", 0) != 0) {
        s = "meshes\\" + s;
    }
    return s;
}

CellScene::~CellScene() {
    // Note: cleanup must be called with a valid GxmRenderer before destruction
}

void CellScene::cleanup(GxmRenderer& renderer) {
    logMsg("CellScene: cleaning up '%s' resources...", m_cellName.c_str());

    for (auto& pair : m_meshCache) {
        renderer.destroyGxmMesh(pair.second);
    }
    m_meshCache.clear();

    for (auto& pair : m_textureCache) {
        renderer.destroyGxmTexture(pair.second);
    }
    m_textureCache.clear();

    m_meshToTexturePath.clear();
    m_objects.clear();
    m_doors.clear();
    m_cellName.clear();
}

bool CellScene::extractTexture(BSAReader& texBsa1, BSAReader* texBsa2,
                               const std::string& path, std::vector<uint8_t>& outData) {
    if (path.empty()) return false;

    size_t slash = path.find_last_of("/\\");
    std::string folder = (slash != std::string::npos) ? path.substr(0, slash) : "";
    std::string file   = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    auto tryExtract = [&](BSAReader& bsa) -> bool {
        if (bsa.extractFile(folder.c_str(), file.c_str(), outData)) {
            return true;
        }
        if (folder.rfind("textures", 0) != 0) {
            std::string withTex = "textures\\" + folder;
            if (bsa.extractFile(withTex.c_str(), file.c_str(), outData)) {
                return true;
            }
        } else {
            size_t afterTex = folder.find_first_of("/\\");
            if (afterTex != std::string::npos) {
                std::string withoutTex = folder.substr(afterTex + 1);
                if (bsa.extractFile(withoutTex.c_str(), file.c_str(), outData)) {
                    return true;
                }
            }
        }
        return false;
    };

    if (tryExtract(texBsa1)) return true;
    if (texBsa2 && texBsa2->isOpen() && tryExtract(*texBsa2)) return true;

    return false;
}

bool CellScene::load(const char* cellFilePath,
                     BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2,
                     GxmRenderer& renderer) {
    CellScene replacement;
    if (!replacement.loadInternal(cellFilePath, meshBsa, texBsa1, texBsa2, renderer)) {
        replacement.cleanup(renderer);
        return false;
    }
    cleanup(renderer);
    *this = std::move(replacement);
    return true;
}

bool CellScene::loadInternal(const char* cellFilePath,
                     BSAReader& meshBsa,
                     BSAReader& texBsa1,
                     BSAReader* texBsa2,
                     GxmRenderer& renderer) {
    cleanup(renderer);

    logMsg("==================================================");
    logMsg("CellScene: Loading cell binary '%s'", cellFilePath);

    SceUID fd = sceIoOpen(cellFilePath, SCE_O_RDONLY, 0);
    if (fd < 0) {
        logMsg("CellScene: failed to open cell file: %s (err 0x%08X)", cellFilePath, (unsigned int)fd);
        return false;
    }

    CellFileHeader hdr;
    int bytesRead = sceIoRead(fd, &hdr, sizeof(CellFileHeader));
    if (bytesRead != sizeof(CellFileHeader)) {
        logMsg("CellScene: failed to read cell header");
        sceIoClose(fd);
        return false;
    }

    if (memcmp(hdr.magic, "CELL", 4) != 0 || hdr.version != 1 ||
        hdr.numObjects > 32768 ||
        !std::isfinite(hdr.spawnX) || !std::isfinite(hdr.spawnY) || !std::isfinite(hdr.spawnZ) ||
        !std::isfinite(hdr.spawnYaw) || !std::isfinite(hdr.spawnPitch)) {
        logMsg("CellScene: invalid magic in cell file");
        sceIoClose(fd);
        return false;
    }

    m_cellName.assign(hdr.cellName, strnlen(hdr.cellName, sizeof(hdr.cellName)));
    m_spawnX = hdr.spawnX;
    m_spawnY = hdr.spawnY;
    m_spawnZ = hdr.spawnZ;
    m_spawnYaw = hdr.spawnYaw;
    m_spawnPitch = hdr.spawnPitch;

    logMsg("CellScene: '%s' (0x%08X) - %u placed objects",
           m_cellName.c_str(), hdr.formId, hdr.numObjects);
    logMsg("CellScene: Spawn at (%.1f, %.1f, %.1f), Yaw=%.1f",
           m_spawnX, m_spawnY, m_spawnZ, m_spawnYaw);

    std::vector<CellObjectOnDisk> diskObjects(hdr.numObjects);
    int objDataSize = hdr.numObjects * sizeof(CellObjectOnDisk);
    bytesRead = sceIoRead(fd, diskObjects.data(), objDataSize);
    struct {char magic[4];uint32_t count;} extension{};
    int extra=sceIoRead(fd,&extension,sizeof(extension));
    if(extra!=0) {
        if(extra!=sizeof(extension) || memcmp(extension.magic,"DOOR",4) || extension.count>4096) {sceIoClose(fd);return false;}
        m_doors.resize(extension.count);
        size_t size=m_doors.size()*sizeof(MapDoor);
        if(sceIoRead(fd,m_doors.data(),size)!=(int)size) {sceIoClose(fd);return false;}
        for(const auto& door:m_doors) {
            for(unsigned k=0;k<3;++k)if(!std::isfinite(door.source[k]) || !std::isfinite(door.target[k])) {sceIoClose(fd);return false;}
            if(!std::isfinite(door.yaw)) {sceIoClose(fd);return false;}
        }
    }
    sceIoClose(fd);

    if (bytesRead != objDataSize) {
        logMsg("CellScene: truncated object data (expected %u bytes, got %d)",
               (unsigned int)objDataSize, bytesRead);
        return false;
    }

    m_objects.reserve(hdr.numObjects);

    m_minX = 1e9f;  m_maxX = -1e9f;
    m_minY = 1e9f;  m_maxY = -1e9f;
    std::unordered_map<int, int> floorZCounts;

    uint32_t meshesLoaded = 0;
    uint32_t texturesLoaded = 0;

    for (uint32_t i = 0; i < hdr.numObjects; ++i) {
        const auto& dObj = diskObjects[i];
        if (!memchr(dObj.modelPath, '\0', sizeof(dObj.modelPath)) ||
            !std::isfinite(dObj.posX) || !std::isfinite(dObj.posY) || !std::isfinite(dObj.posZ) ||
            !std::isfinite(dObj.rotX) || !std::isfinite(dObj.rotY) || !std::isfinite(dObj.rotZ) ||
            !std::isfinite(dObj.scale)) return false;
        std::string normPath = normalizeModelPath(dObj.modelPath);

        // Track spatial bounds
        if (dObj.posX < m_minX) m_minX = dObj.posX;
        if (dObj.posX > m_maxX) m_maxX = dObj.posX;
        if (dObj.posY < m_minY) m_minY = dObj.posY;
        if (dObj.posY > m_maxY) m_maxY = dObj.posY;

        // Sample floor height from architecture/room models
        if (normPath.find("craftsman") != std::string::npos ||
            normPath.find("room") != std::string::npos ||
            normPath.find("floor") != std::string::npos ||
            normPath.find("hall") != std::string::npos ||
            normPath.find("wall") != std::string::npos) {
            int zKey = (int)roundf(dObj.posZ);
            floorZCounts[zKey]++;
        }

        // Check if mesh is in cache; if not, load from meshes BSA
        if (m_meshCache.find(normPath) == m_meshCache.end()) {
            size_t slash = normPath.find_last_of("/\\");
            std::string folder = (slash != std::string::npos) ? normPath.substr(0, slash) : "";
            std::string file   = (slash != std::string::npos) ? normPath.substr(slash + 1) : normPath;

            std::vector<uint8_t> nifData;
            if (meshBsa.extractFile(folder.c_str(), file.c_str(), nifData)) {
                NifLoader nif;
                if (nif.load(nifData.data(), nifData.size(), true)) {
                    const NifMesh& mesh = nif.getMesh();
                    GxmMesh gMesh;
                    if (renderer.createGxmMesh(mesh, gMesh)) {
                        m_meshCache[normPath] = gMesh;
                        meshesLoaded++;

                        // If mesh has a diffuse texture, load it
                        if (mesh.parts.empty() && !mesh.diffuseTexture.empty()) {
                            m_meshToTexturePath[normPath] = mesh.diffuseTexture;
                            if (m_textureCache.find(mesh.diffuseTexture) == m_textureCache.end()) {
                                std::vector<uint8_t> ddsData;
                                if (extractTexture(texBsa1, texBsa2, mesh.diffuseTexture, ddsData)) {
                                    DDSLoader dds;
                                    if (dds.load(ddsData.data(), ddsData.size())) {
                                        GxmTexture gTex;
                                        if (renderer.createGxmTexture(dds.getTexture(), gTex)) {
                                            m_textureCache[mesh.diffuseTexture] = gTex;
                                            texturesLoaded++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Create in-memory placed instance
        PlacedObjectInstance inst;
        inst.refFormId = dObj.refFormId;
        inst.modelPath = normPath;
        inst.worldMatrix = Mat4::createTransform(
            dObj.posX, dObj.posY, dObj.posZ,
            dObj.rotX, dObj.rotY, dObj.rotZ,
            (dObj.scale > 0.001f) ? dObj.scale : 1.0f);

        auto meshIt = m_meshCache.find(normPath);
        inst.mesh = (meshIt != m_meshCache.end()) ? &meshIt->second : nullptr;

        auto texPathIt = m_meshToTexturePath.find(normPath);
        if (texPathIt != m_meshToTexturePath.end()) {
            auto texIt = m_textureCache.find(texPathIt->second);
            inst.texture = (texIt != m_textureCache.end()) ? &texIt->second : nullptr;
        } else {
            inst.texture = nullptr;
        }

        m_objects.push_back(inst);
    }

    // Determine dominant floor elevation
    int bestZ = 0, maxCount = 0;
    for (const auto& pair : floorZCounts) {
        if (pair.second > maxCount) {
            maxCount = pair.second;
            bestZ = pair.first;
        }
    }
    if (maxCount > 0) {
        m_floorZ = static_cast<float>(bestZ);
    } else {
        m_floorZ = m_spawnZ - 64.0f;
    }

    logMsg("CellScene: '%s' loaded successfully!", m_cellName.c_str());
    logMsg("CellScene: Bounds X=[%.1f, %.1f], Y=[%.1f, %.1f], Dominant Floor Z=%.1f",
           m_minX, m_maxX, m_minY, m_maxY, m_floorZ);
    logMsg("CellScene: %u unique meshes uploaded, %u unique textures uploaded",
           meshesLoaded, texturesLoaded);
    logMsg("CellScene: %u total instances ready for rendering", (unsigned int)m_objects.size());

    return meshesLoaded > 0 || hdr.numObjects==0;
}

const MapDoor* CellScene::nearbyDoor(float x,float y,float z) const {
    const MapDoor* nearest=nullptr;float best=150*150;
    for(const auto& door:m_doors) {
        float dx=x-door.source[0],dy=y-door.source[1],dz=z-door.source[2],d=dx*dx+dy*dy+dz*dz*.2f;
        if(d<best){best=d;nearest=&door;}
    }
    return nearest;
}

void CellScene::render(GxmRenderer& renderer) {
    renderer.beginStaticBatch();
    // Finish opaque architecture before drawing alpha surfaces and light rays.
    for(int pass=0;pass<2;++pass) for(const auto& inst:m_objects) {
        if(inst.mesh && inst.mesh->isValid()) renderer.drawMeshInstance(*inst.mesh,inst.texture,inst.worldMatrix,pass);
    }
    renderer.endStaticBatch();
}

void CellScene::buildCollision(PhysicsEngine& physics) const {
    for (const auto& object : m_objects) {
        if (!object.mesh || !object.mesh->isValid()) continue;
        const auto& mesh = *object.mesh;
        auto worldVertex = [&](uint16_t index) {
            const float* p = mesh.vertices[index].position;
            const auto& m = object.worldMatrix.m;
            return Vec3{m[0][0] * p[0] + m[1][0] * p[1] + m[2][0] * p[2] + m[3][0],
                        m[0][1] * p[0] + m[1][1] * p[1] + m[2][1] * p[2] + m[3][1],
                        m[0][2] * p[0] + m[1][2] * p[1] + m[2][2] * p[2] + m[3][2]};
        };
        auto addRange = [&](uint32_t first, uint32_t count) {
            for (uint32_t i = first; i + 2 < first + count; i += 3) {
                physics.addTriangle(worldVertex(mesh.indices[i]), worldVertex(mesh.indices[i + 1]),
                                    worldVertex(mesh.indices[i + 2]));
            }
        };
        if (mesh.parts.empty()) addRange(0, mesh.indexCount);
        else for (const auto& part : mesh.parts) if(!part.transparent) addRange(part.firstIndex, part.indexCount);
    }
}
