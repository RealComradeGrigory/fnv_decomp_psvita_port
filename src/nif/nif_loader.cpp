/**
 * NIF Mesh Loader - Implementation
 *
 * Parses Gamebryo NIF (NetImmerse File) format version 20.2.0.7
 * used by Fallout 3 and Fallout: New Vegas.
 *
 * Only geometry data (NiTriShapeData / NiTriStripsData) is extracted;
 * all other block types are skipped by consuming blockSizes[i] bytes.
 */

#include "nif_loader.h"
#include <cstring>
#include <algorithm>
#include <cmath>

extern void logMsg(const char* fmt, ...);

namespace {
void sanitizeColor(NifColor& c) {
    // Some shipped FNV meshes (WoodSteps01NV) contain NaN/uninitialized
    // vertex alpha. Keep their valid geometry and use opaque white for an
    // invalid channel; never propagate non-finite values into GXM vertices.
    auto channel=[](float f){return std::isfinite(f)?std::max(0.f,std::min(1.f,f)):1.f;};
    c.r=channel(c.r);c.g=channel(c.g);c.b=channel(c.b);c.a=channel(c.a);
}
}

// ── Templated binary reader ────────────────────────────────────────────────
template<typename T>
T NifLoader::read(const uint8_t* data, size_t size, size_t& offset)
{
    static int s_errCount = 0;
    if (offset > size || sizeof(T) > size - offset) {
        m_readFailed = true;
        offset = size;
        if (++s_errCount <= 5) {
            logMsg("[NIF] read out of bounds at offset 0x%X (size 0x%X)",
                   (unsigned int)offset, (unsigned int)size);
        }
        T zero{};
        return zero;
    }
    T val;
    std::memcpy(&val, data + offset, sizeof(T));
    offset += sizeof(T);
    return val;
}

// Explicit instantiations for the types we use
template uint8_t  NifLoader::read<uint8_t>(const uint8_t*, size_t, size_t&);
template uint16_t NifLoader::read<uint16_t>(const uint8_t*, size_t, size_t&);
template uint32_t NifLoader::read<uint32_t>(const uint8_t*, size_t, size_t&);
template int32_t  NifLoader::read<int32_t>(const uint8_t*, size_t, size_t&);
template float    NifLoader::read<float>(const uint8_t*, size_t, size_t&);

// ── Sized string ───────────────────────────────────────────────────────────
std::string NifLoader::readSizedString(const uint8_t* data, size_t size,
                                       size_t& offset)
{
    uint32_t len = read<uint32_t>(data, size, offset);
    if (len == 0)
        return {};
    if (offset > size || len > size - offset) {
        m_readFailed = true;
        return {};
    }
    std::string str(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return str;
}

// ── Short string (uint8 length prefix) ──────────────────────────────────────
std::string NifLoader::readShortString(const uint8_t* data, size_t size,
                                       size_t& offset)
{
    uint8_t len = read<uint8_t>(data, size, offset);
    if (len == 0)
        return {};
    if (offset > size || len > size - offset) {
        m_readFailed = true;
        return {};
    }
    std::string str(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return str;
}

// ── Block-type resolution ──────────────────────────────────────────────────
NifBlockType NifLoader::resolveBlockType(uint32_t blockIndex) const
{
    if (blockIndex >= m_header.blockTypeIndices.size())
        return NifBlockType::Unknown;

    uint16_t typeIdx = m_header.blockTypeIndices[blockIndex] & 0x7FFF;
    if (typeIdx >= m_header.blockTypeNames.size())
        return NifBlockType::Unknown;

    const std::string& name = m_header.blockTypeNames[typeIdx];

    if (name == "NiTriShapeData")   return NifBlockType::NiTriShapeData;
    if (name == "NiTriStripsData")  return NifBlockType::NiTriStripsData;
    if (name == "NiTriShape")       return NifBlockType::NiTriShape;
    if (name == "NiTriStrips")      return NifBlockType::NiTriStrips;
    if (name == "NiNode")           return NifBlockType::NiNode;
    if (name == "NiBillboardNode" || name == "NiSwitchNode" || name == "NiLODNode" ||
        name == "BSOrderedNode" || name == "BSMultiBoundNode" || name == "BSLeafAnimNode")
        return NifBlockType::NiNode;
    if (name == "BSFadeNode")       return NifBlockType::BSFadeNode;
    if (name == "BSShaderTextureSet") return NifBlockType::BSShaderTextureSet;
    if (name == "BSShaderPPLightingProperty") return NifBlockType::BSShaderPPLightingProperty;

    if (name == "BSShaderNoLightingProperty") return NifBlockType::BSShaderNoLightingProperty;
    if (name == "NiAlphaProperty") return NifBlockType::NiAlphaProperty;
    return NifBlockType::Unknown;
}

// ── Header parser ──────────────────────────────────────────────────────────
bool NifLoader::parseHeader(const uint8_t* data, size_t size, size_t& offset)
{
    // ---- Version line string (newline-terminated) ----
    const char* start = reinterpret_cast<const char*>(data + offset);
    const char* end   = reinterpret_cast<const char*>(data + size);
    const char* nl    = std::find(start, end, '\n');

    if (nl == end) {
        logMsg("[NIF] Could not find version string terminator");
        return false;
    }

    size_t lineLen = static_cast<size_t>(nl - start);
    if (lineLen >= sizeof(m_header.versionString))
        lineLen = sizeof(m_header.versionString) - 1;

    std::memcpy(m_header.versionString, start, lineLen);
    m_header.versionString[lineLen] = '\0';
    offset += static_cast<size_t>(nl - start) + 1; // skip past '\n'

    logMsg("[NIF] Version string: \"%s\"", m_header.versionString);

    // ---- Binary fields ----
    m_header.version    = read<uint32_t>(data, size, offset);
    m_header.endianness = read<uint8_t>(data, size, offset);
    m_header.userVersion = read<uint32_t>(data, size, offset);
    m_header.numBlocks  = read<uint32_t>(data, size, offset);
    if (m_readFailed || m_header.endianness != 1 || m_header.numBlocks > 65536 ||
        m_header.numBlocks > (size - offset) / 6) return false;

    logMsg("[NIF] Version 0x%08X, endian %u, userVer %u, numBlocks %u",
           m_header.version, m_header.endianness,
           m_header.userVersion, m_header.numBlocks);

    if (m_header.version != 0x14020007) {
        logMsg("[NIF] Unsupported NIF version 0x%08X (expected 0x14020007)",
               m_header.version);
        return false;
    }

    // ---- Bethesda-specific user version 2 (BS Stream Header / Export Info) ----
    // For Bethesda games (FO3/FNV: version 20.2.0.7 with userVersion >= 11):
    m_header.userVersion2 = read<uint32_t>(data, size, offset);

    // Export info compound: Creator, Export Info 1, Export Info 2 (ShortString: uint8 len + chars)
    std::string creator = readShortString(data, size, offset);
    std::string exportInfo1 = readShortString(data, size, offset);
    std::string exportInfo2 = readShortString(data, size, offset);
    logMsg("[NIF] UserVer2: %u, Creator: \"%s\", Info1: \"%s\", Info2: \"%s\"",
           m_header.userVersion2, creator.c_str(), exportInfo1.c_str(), exportInfo2.c_str());

    // ---- Block type list ----
    uint16_t numBlockTypes = read<uint16_t>(data, size, offset);
    logMsg("[NIF] Block types: %u", numBlockTypes);
    if (numBlockTypes > 1024) {
        logMsg("[NIF] Error: excessive block types (%u), header desync!", numBlockTypes);
        return false;
    }

    m_header.blockTypeNames.resize(numBlockTypes);
    for (uint16_t i = 0; i < numBlockTypes; i++) {
        m_header.blockTypeNames[i] = readSizedString(data, size, offset);
        logMsg("[NIF]   BlockType[%u]: %s", i, m_header.blockTypeNames[i].c_str());
    }

    // ---- Block type index mapping (one per block) ----
    m_header.blockTypeIndices.resize(m_header.numBlocks);
    for (uint32_t i = 0; i < m_header.numBlocks; i++) {
        m_header.blockTypeIndices[i] = read<uint16_t>(data, size, offset);
    }

    // ---- Block sizes (one per block) ----
    m_header.blockSizes.resize(m_header.numBlocks);
    for (uint32_t i = 0; i < m_header.numBlocks; i++) {
        m_header.blockSizes[i] = read<uint32_t>(data, size, offset);
    }

    // ---- String table ----
    uint32_t numStrings     = read<uint32_t>(data, size, offset);
    uint32_t maxStringLen   = read<uint32_t>(data, size, offset);
    if (m_readFailed || numStrings > (size - offset) / 4) return false;
    (void)maxStringLen;

    logMsg("[NIF] String table: %u strings (max len %u)", numStrings, maxStringLen);

    for (uint32_t i = 0; i < numStrings; i++) {
        readSizedString(data, size, offset); // we don't need these for geometry
    }

    // ---- Groups ----
    uint32_t numGroups = read<uint32_t>(data, size, offset);
    if (m_readFailed || numGroups > (size - offset) / 4) return false;
    logMsg("[NIF] Groups: %u", numGroups);

    // Skip group sizes if any (each is a uint32)
    for (uint32_t i = 0; i < numGroups; i++) {
        read<uint32_t>(data, size, offset);
    }

    logMsg("[NIF] Header parsed OK, block data begins at offset 0x%08X", (unsigned int)offset);
    return true;
}

// ── Top-level loader ───────────────────────────────────────────────────────
bool NifLoader::load(const uint8_t* data, size_t size, bool placedReference)
{
    if (!data || size < 16) {
        logMsg("[NIF] Invalid buffer (null or too small)");
        return false;
    }

    // Reset
    m_header = {};
    m_mesh   = {};
    m_readFailed = false;
    m_placedReference = placedReference;
    m_readLimit = size;

    size_t offset = 0;

    if (!parseHeader(data, size, offset) || m_readFailed)
        return false;

    m_sceneObjects.assign(m_header.numBlocks, {});
    m_geometry.assign(m_header.numBlocks, {});
    m_textureSets.assign(m_header.numBlocks, {});
    m_shaderTextures.assign(m_header.numBlocks, -1);
    m_alphaFlags.assign(m_header.numBlocks, 0);
    // Parse each block sequentially
    for (uint32_t i = 0; i < m_header.numBlocks; i++) {
        if (!parseBlock(data, size, offset, i))
            return false;
    }

    if (!assembleScene()) return false;
    for (const auto& vertex : m_mesh.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)) return false;
    }
    for (uint16_t index : m_mesh.indices) {
        if (index >= m_mesh.vertices.size()) return false;
    }
    m_mesh.vertexCount   = static_cast<uint32_t>(m_mesh.vertices.size());
    m_mesh.triangleCount = static_cast<uint32_t>(m_mesh.indices.size() / 3);

    if (!m_mesh.vertices.empty()) {
        Vec3 minV = m_mesh.vertices[0];
        Vec3 maxV = m_mesh.vertices[0];
        for (const auto& v : m_mesh.vertices) {
            minV.x = std::min(minV.x, v.x); minV.y = std::min(minV.y, v.y); minV.z = std::min(minV.z, v.z);
            maxV.x = std::max(maxV.x, v.x); maxV.y = std::max(maxV.y, v.y); maxV.z = std::max(maxV.z, v.z);
        }
        m_mesh.center = { (minV.x + maxV.x) * 0.5f, (minV.y + maxV.y) * 0.5f, (minV.z + maxV.z) * 0.5f };
        float dx = maxV.x - minV.x, dy = maxV.y - minV.y, dz = maxV.z - minV.z;
        m_mesh.radius = std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 10.0f);
        logMsg("[NIF] Mesh bounds: center=(%.1f, %.1f, %.1f), radius=%.1f",
               m_mesh.center.x, m_mesh.center.y, m_mesh.center.z, m_mesh.radius);
    }

    logMsg("[NIF] Loaded mesh: %u vertices, %u triangles",
           m_mesh.vertexCount, m_mesh.triangleCount);
    return !m_readFailed && !m_mesh.vertices.empty() && !m_mesh.indices.empty();
}

// ── Per-block dispatcher ───────────────────────────────────────────────────
bool NifLoader::skipObjectNet(const uint8_t* data, size_t size, size_t& offset) {
    read<int32_t>(data, size, offset); // Name in the string table.
    uint32_t extraCount = read<uint32_t>(data, size, offset);
    if (m_readFailed || extraCount > (size - offset) / 4) return false;
    skip(offset, extraCount * 4);
    read<int32_t>(data, size, offset); // Controller.
    return !m_readFailed;
}

bool NifLoader::parseSceneObject(const uint8_t* data, size_t size, size_t& offset,
                                 uint32_t blockIndex, bool isNode) {
    if (!skipObjectNet(data, size, offset)) return false;
    auto& object = m_sceneObjects[blockIndex];
    object.present = true;
    uint32_t flags = m_header.userVersion2 > 26
        ? read<uint32_t>(data, size, offset) : read<uint16_t>(data, size, offset);
    object.hidden = (flags & 1) != 0;
    object.local.translation.x = read<float>(data, size, offset);
    object.local.translation.y = read<float>(data, size, offset);
    object.local.translation.z = read<float>(data, size, offset);
    for (float& element : object.local.rotation) element = read<float>(data, size, offset);
    object.local.scale = read<float>(data, size, offset);
    if (!std::isfinite(object.local.scale)) return false;
    uint32_t propertyCount = read<uint32_t>(data, size, offset);
    if (m_readFailed || propertyCount > (size - offset) / 4) return false;
    object.properties.resize(propertyCount);
    for (int32_t& property : object.properties) property = read<int32_t>(data, size, offset);
    read<int32_t>(data, size, offset); // Collision object.
    if (isNode) {
        uint32_t childCount = read<uint32_t>(data, size, offset);
        if (m_readFailed || childCount > (size - offset) / 4) return false;
        object.children.resize(childCount);
        for (int32_t& child : object.children) child = read<int32_t>(data, size, offset);
    } else {
        object.data = read<int32_t>(data, size, offset);
        read<int32_t>(data, size, offset); // Skin instance. Mesh remains in its bind pose.
    }
    return !m_readFailed;
}

bool NifLoader::assembleScene() {
    bool hasShapes = false;
    for (size_t i = 0; i < m_sceneObjects.size(); ++i) {
        auto& object = m_sceneObjects[i];
        hasShapes |= object.present && object.data >= 0;
        for (int32_t child : object.children) {
            if (child == -1) continue;
            if (child < 0 || static_cast<size_t>(child) >= m_sceneObjects.size()) return false;
            auto& parent = m_sceneObjects[child].parent;
            if (parent != -1 && parent != static_cast<int32_t>(i)) return false;
            parent = i;
        }
    }
    if (!hasShapes) return true;

    NifMesh result;
    result.vertices.reserve(m_mesh.vertices.size());
    result.normals.reserve(m_mesh.normals.size());
    result.uvs.reserve(m_mesh.uvs.size());
    result.indices.reserve(m_mesh.indices.size());
    auto rotate = [](const Transform& transform, const Vec3& point) {
        const float* r = transform.rotation;
        return Vec3{r[0] * point.x + r[1] * point.y + r[2] * point.z,
                    r[3] * point.x + r[4] * point.y + r[5] * point.z,
                    r[6] * point.x + r[7] * point.y + r[8] * point.z};
    };
    for (size_t shapeIndex = 0; shapeIndex < m_sceneObjects.size(); ++shapeIndex) {
        const auto& shape = m_sceneObjects[shapeIndex];
        if (!shape.present || shape.data < 0) continue;
        if (static_cast<size_t>(shape.data) >= m_geometry.size()) return false;
        const auto& range = m_geometry[shape.data];
        if (range.vertexCount == 0 || range.indexCount == 0) continue;

        std::vector<int32_t> chain;
        int32_t current = shapeIndex;
        bool hidden = false;
        while (current >= 0) {
            if (chain.size() >= 256 || std::find(chain.begin(), chain.end(), current) != chain.end()) return false;
            chain.push_back(current);
            hidden |= m_sceneObjects[current].hidden;
            current = m_sceneObjects[current].parent;
        }
        if (hidden) continue;
        uint32_t base = result.vertices.size();
        if (base + range.vertexCount > 65536) return false;
        for (uint32_t i = 0; i < range.vertexCount; ++i) {
            Vec3 vertex = m_mesh.vertices[range.firstVertex + i];
            Vec3 normal = m_mesh.normals[range.firstVertex + i];
            for (int32_t node : chain) {
                if(m_placedReference && m_sceneObjects[node].parent<0)continue;
                const auto& transform = m_sceneObjects[node].local;
                vertex = rotate(transform, vertex);
                vertex.x = vertex.x * transform.scale + transform.translation.x;
                vertex.y = vertex.y * transform.scale + transform.translation.y;
                vertex.z = vertex.z * transform.scale + transform.translation.z;
                normal = rotate(transform, normal);
                if (transform.scale < 0.0f) { normal.x = -normal.x; normal.y = -normal.y; normal.z = -normal.z; }
            }
            result.vertices.push_back(vertex);
            result.normals.push_back(normal);
            result.uvs.push_back(m_mesh.uvs[range.firstVertex + i]);
            result.colors.push_back(m_mesh.colors[range.firstVertex+i]);
        }
        NifMeshPart part;
        part.firstIndex = result.indices.size();
        part.indexCount = range.indexCount;
        for (uint32_t i = 0; i < range.indexCount; ++i) {
            uint32_t index = m_mesh.indices[range.firstIndex + i];
            if (index < range.firstVertex || index >= range.firstVertex + range.vertexCount) return false;
            result.indices.push_back(static_cast<uint16_t>(base + index - range.firstVertex));
        }
        for (int32_t property : shape.properties) {
            if (property < 0 || static_cast<size_t>(property) >= m_shaderTextures.size()) continue;
            part.transparent |= (m_alphaFlags[property] & 0x201)!=0;
            int32_t textureSet = m_shaderTextures[property];
            if (textureSet >= 0 && static_cast<size_t>(textureSet) < m_textureSets.size()) {
                part.diffuseTexture = m_textureSets[textureSet];
            }
        }
        if (result.diffuseTexture.empty()) result.diffuseTexture = part.diffuseTexture;
        result.parts.push_back(std::move(part));
    }
    m_mesh = std::move(result);
    return !m_mesh.vertices.empty() && !m_mesh.indices.empty();
}

bool NifLoader::parseBlock(const uint8_t* data, size_t size, size_t& offset,
                           uint32_t blockIndex)
{
    if (blockIndex >= m_header.blockSizes.size()) {
        logMsg("[NIF] Block index %u out of range", blockIndex);
        return false;
    }

    uint32_t blockSize = m_header.blockSizes[blockIndex];
    size_t   blockEnd  = offset + blockSize;
    m_readLimit = blockEnd;

    if (blockEnd > size) {
        logMsg("[NIF] Block %u extends past end of data (need 0x%08X, have 0x%08X)",
               blockIndex, (unsigned int)blockEnd, (unsigned int)size);
        return false;
    }

    NifBlockType type = resolveBlockType(blockIndex);

    switch (type) {
        case NifBlockType::NiNode:
        case NifBlockType::BSFadeNode:
        case NifBlockType::NiTriShape:
        case NifBlockType::NiTriStrips: {
            bool isNode = type == NifBlockType::NiNode || type == NifBlockType::BSFadeNode;
            if (!parseSceneObject(data, blockEnd, offset, blockIndex, isNode)) return false;
            offset = blockEnd;
            break;
        }
        case NifBlockType::NiTriShapeData: {
            size_t savedOffset = offset;
            auto& range = m_geometry[blockIndex];
            range.firstVertex = m_mesh.vertices.size();
            range.firstIndex = m_mesh.indices.size();
            if (!parseTriShapeData(data, blockEnd, offset) || m_readFailed) {
                logMsg("[NIF] Failed to parse NiTriShapeData at block %u", blockIndex);
                return false;
            }
            m_mesh.normals.resize(m_mesh.vertices.size(), Vec3{0.0f, 0.0f, 1.0f});
            m_mesh.uvs.resize(m_mesh.vertices.size(), Vec2{0.0f, 0.0f});
            range.vertexCount = m_mesh.vertices.size() - range.firstVertex;
            range.indexCount = m_mesh.indices.size() - range.firstIndex;
            // Ensure we consumed exactly blockSize bytes
            if (offset != blockEnd) {
                logMsg("[NIF] NiTriShapeData block %u: consumed %u bytes, expected %u – correcting",
                       blockIndex, (unsigned int)(offset - savedOffset), blockSize);
                offset = blockEnd;
            }
            break;
        }

        case NifBlockType::NiTriStripsData: {
            size_t savedOffset = offset;
            auto& range = m_geometry[blockIndex];
            range.firstVertex = m_mesh.vertices.size();
            range.firstIndex = m_mesh.indices.size();
            if (!parseTriStripsData(data, blockEnd, offset) || m_readFailed) {
                logMsg("[NIF] Failed to parse NiTriStripsData at block %u", blockIndex);
                return false;
            }
            m_mesh.normals.resize(m_mesh.vertices.size(), Vec3{0.0f, 0.0f, 1.0f});
            m_mesh.uvs.resize(m_mesh.vertices.size(), Vec2{0.0f, 0.0f});
            range.vertexCount = m_mesh.vertices.size() - range.firstVertex;
            range.indexCount = m_mesh.indices.size() - range.firstIndex;
            if (offset != blockEnd) {
                logMsg("[NIF] NiTriStripsData block %u: consumed %u bytes, expected %u – correcting",
                       blockIndex, (unsigned int)(offset - savedOffset), blockSize);
                offset = blockEnd;
            }
            break;
        }

        case NifBlockType::BSShaderTextureSet: {
            {
                uint32_t numTextures = read<uint32_t>(data, blockEnd, offset);
                if (numTextures > (blockEnd - offset) / 4) return false;
                for (uint32_t t = 0; t < numTextures; ++t) {
                    std::string tex = readSizedString(data, blockEnd, offset);
                    if (t == 0 && !tex.empty()) {
                        m_textureSets[blockIndex] = tex;
                        if (m_mesh.diffuseTexture.empty()) m_mesh.diffuseTexture = tex;
                        logMsg("[NIF] Found diffuse texture: %s", tex.c_str());
                    }
                }
            }
            offset = blockEnd;
            break;
        }

        case NifBlockType::NiAlphaProperty: {
            if (!skipObjectNet(data,blockEnd,offset)) return false;
            m_alphaFlags[blockIndex]=read<uint16_t>(data,blockEnd,offset);
            offset=blockEnd;break;
        }
        case NifBlockType::BSShaderNoLightingProperty: {
            if (!skipObjectNet(data,blockEnd,offset)) return false;
            skip(offset,22);
            m_textureSets[blockIndex]=readSizedString(data,blockEnd,offset);
            m_shaderTextures[blockIndex]=blockIndex;
            offset=blockEnd;break;
        }
        case NifBlockType::BSShaderPPLightingProperty: {
            if (!skipObjectNet(data, blockEnd, offset)) return false;
            // NiShade flags, shader type/flags, environment scale and clamp mode.
            skip(offset, 22);
            m_shaderTextures[blockIndex] = read<int32_t>(data, blockEnd, offset);
            offset = blockEnd;
            break;
        }

        default:
            // Skip unknown / unneeded blocks entirely
            skip(offset, blockSize);
            break;
    }

    return !m_readFailed;
}

// ── NiTriShapeData parser ──────────────────────────────────────────────────
bool NifLoader::parseTriShapeData(const uint8_t* data, size_t size,
                                  size_t& offset)
{
    // Track index offset for merging multiple shapes into one mesh
    uint32_t baseVertex = static_cast<uint32_t>(m_mesh.vertices.size());

    // NiObject base: groupID (int32 ref)
    /*int32_t groupID =*/ read<int32_t>(data, size, offset);

    // ── Vertices ──
    uint16_t numVertices  = read<uint16_t>(data, size, offset);
    /*uint8_t keepFlags   =*/ read<uint8_t>(data, size, offset);
    /*uint8_t compressFlags=*/ read<uint8_t>(data, size, offset);

    uint8_t hasVertices = read<uint8_t>(data, size, offset);
    if (m_readFailed || !hasVertices || numVertices == 0 || baseVertex + numVertices > 65536) return false;

    if (hasVertices) {
        if (offset + numVertices * sizeof(Vec3) > size) {
            logMsg("[NIF] Vertex data overflows buffer");
            return false;
        }
        size_t prevSize = m_mesh.vertices.size();
        m_mesh.vertices.resize(prevSize + numVertices);
        for (uint16_t i = 0; i < numVertices; i++) {
            m_mesh.vertices[prevSize + i].x = read<float>(data, size, offset);
            m_mesh.vertices[prevSize + i].y = read<float>(data, size, offset);
            m_mesh.vertices[prevSize + i].z = read<float>(data, size, offset);
        }
    }

    // ── BS Vector Flags & Material CRC ──
    uint16_t bsVectorFlags = read<uint16_t>(data, size, offset);
    if (m_header.userVersion >= 12) {
        /*uint32_t materialCRC =*/ read<uint32_t>(data, size, offset);
    }

    // ── Normals ──
    uint8_t hasNormals = read<uint8_t>(data, size, offset);

    if (hasNormals) {
        if (offset + numVertices * sizeof(Vec3) > size) {
            logMsg("[NIF] Normal data overflows buffer");
            return false;
        }
        size_t prevSize = m_mesh.normals.size();
        m_mesh.normals.resize(prevSize + numVertices);
        for (uint16_t i = 0; i < numVertices; i++) {
            m_mesh.normals[prevSize + i].x = read<float>(data, size, offset);
            m_mesh.normals[prevSize + i].y = read<float>(data, size, offset);
            m_mesh.normals[prevSize + i].z = read<float>(data, size, offset);
        }

        // If tangent space is stored (Bethesda flag 0x1000), skip tangents + bitangents
        if (bsVectorFlags & 0x1000) {
            // tangents: numVertices * Vec3
            skip(offset, static_cast<size_t>(numVertices) * 3 * sizeof(float));
            // bitangents: numVertices * Vec3
            skip(offset, static_cast<size_t>(numVertices) * 3 * sizeof(float));
        }
    }

    // ── Bounding sphere ──
    /*Vec3  center;*/
    read<float>(data, size, offset); // center.x
    read<float>(data, size, offset); // center.y
    read<float>(data, size, offset); // center.z
    /*float radius =*/ read<float>(data, size, offset);

    // ── Vertex colors ──
    uint8_t hasVertexColors = read<uint8_t>(data, size, offset);
    m_mesh.colors.resize(m_mesh.vertices.size());
    if (hasVertexColors) {
        for(uint32_t i=0;i<numVertices;++i) {
            auto& color=m_mesh.colors[baseVertex+i];
            color.r=read<float>(data,size,offset);color.g=read<float>(data,size,offset);
            color.b=read<float>(data,size,offset);color.a=read<float>(data,size,offset);
            sanitizeColor(color);
        }
    }

    // ── UV sets ──
    // In NIF 20.2.0.7 the UV count is packed into bsVectorFlags bits 0-0xFFF
    // but there's also a hasUV byte and optional numUVSets field.
    // Bethesda NIFs: the number of UV sets is ((bsVectorFlags & 0x0001) != 0) ? 1 : 0
    // but we read the file-level fields to stay format-correct.

    // For version 20.2.0.7 the layout is:
    //   - If version >= 20.2.0.7: no separate numUVSets field; UV count comes
    //     from bsVectorFlags bits. But there IS a hasUV byte (undocumented in
    //     some references). We read it to stay aligned.

    // Actually, in Bethesda NIFs (userVersion >= 12), the UV sets count is
    // derived from (bsVectorFlags & 1). But the binary layout after vertex
    // colors simply has the UV data with no extra count field.
    // Let's determine numUVSets from flags.

    uint16_t numUVSets = (bsVectorFlags & 0x0001) ? 1 : 0;
    // Some meshes set bit 0 of bsVectorFlags for 1 UV set.
    // For safety, also try bits 0-3 if zero.
    if (numUVSets == 0) {
        numUVSets = bsVectorFlags & 0x003F; // lower 6 bits
    }

    if (numUVSets > 0 && hasVertices) {
        // Read first UV set into our mesh, skip the rest
        if (offset + numVertices * sizeof(Vec2) > size) {
            logMsg("[NIF] UV data overflows buffer");
            return false;
        }
        size_t prevSize = m_mesh.uvs.size();
        m_mesh.uvs.resize(prevSize + numVertices);
        for (uint16_t i = 0; i < numVertices; i++) {
            m_mesh.uvs[prevSize + i].u = read<float>(data, size, offset);
            m_mesh.uvs[prevSize + i].v = read<float>(data, size, offset);
        }

        // Skip additional UV sets (if any beyond the first)
        if (numUVSets > 1) {
            skip(offset, static_cast<size_t>(numUVSets - 1) *
                         numVertices * sizeof(Vec2));
        }
    }

    // ── Consistency flags ──
    /*uint16_t consistencyFlags =*/ read<uint16_t>(data, size, offset);

    // ── Additional data ref ──
    /*int32_t additionalDataRef =*/ read<int32_t>(data, size, offset);

    // ── Triangles ──
    uint16_t numTriangles      = read<uint16_t>(data, size, offset);
    uint32_t numTrianglePoints = read<uint32_t>(data, size, offset);
    (void)numTrianglePoints;

    uint8_t hasTriangles = read<uint8_t>(data, size, offset);

    if (hasTriangles && numTriangles > 0) {
        if (offset + static_cast<size_t>(numTriangles) * 3 * sizeof(uint16_t) > size) {
            logMsg("[NIF] Triangle index data overflows buffer");
            return false;
        }
        size_t prevSize = m_mesh.indices.size();
        m_mesh.indices.resize(prevSize + numTriangles * 3);
        for (uint16_t t = 0; t < numTriangles; t++) {
            for (uint32_t corner = 0; corner < 3; ++corner) {
                uint16_t localIndex = read<uint16_t>(data, size, offset);
                if (m_readFailed || localIndex >= numVertices) return false;
                m_mesh.indices[prevSize + t * 3 + corner] = localIndex + baseVertex;
            }
        }
    }

    logMsg("[NIF] NiTriShapeData: %u verts, %u tris (base offset %u)",
           numVertices, numTriangles, baseVertex);
    return true;
}

// ── NiTriStripsData parser ─────────────────────────────────────────────────
// Triangle strips: each strip is a sequence of uint16 vertex indices.
// We convert strips to triangle lists during loading.
bool NifLoader::parseTriStripsData(const uint8_t* data, size_t size,
                                   size_t& offset)
{
    uint32_t baseVertex = static_cast<uint32_t>(m_mesh.vertices.size());

    // NiObject base: groupID
    /*int32_t groupID =*/ read<int32_t>(data, size, offset);

    // ── Vertices ──
    uint16_t numVertices = read<uint16_t>(data, size, offset);
    /*uint8_t keepFlags =*/ read<uint8_t>(data, size, offset);
    /*uint8_t compressFlags =*/ read<uint8_t>(data, size, offset);

    uint8_t hasVertices = read<uint8_t>(data, size, offset);
    if (m_readFailed || !hasVertices || numVertices == 0 || baseVertex + numVertices > 65536) return false;

    if (hasVertices) {
        if (offset + numVertices * sizeof(Vec3) > size) {
            logMsg("[NIF] TriStrips vertex data overflows buffer");
            return false;
        }
        size_t prevSize = m_mesh.vertices.size();
        m_mesh.vertices.resize(prevSize + numVertices);
        for (uint16_t i = 0; i < numVertices; i++) {
            m_mesh.vertices[prevSize + i].x = read<float>(data, size, offset);
            m_mesh.vertices[prevSize + i].y = read<float>(data, size, offset);
            m_mesh.vertices[prevSize + i].z = read<float>(data, size, offset);
        }
    }

    // ── BS Vector Flags & Material CRC ──
    uint16_t bsVectorFlags = read<uint16_t>(data, size, offset);
    if (m_header.userVersion >= 12) {
        /*uint32_t materialCRC =*/ read<uint32_t>(data, size, offset);
    }

    // ── Normals ──
    uint8_t hasNormals = read<uint8_t>(data, size, offset);

    if (hasNormals) {
        if (offset + numVertices * sizeof(Vec3) > size) {
            logMsg("[NIF] TriStrips normal data overflows buffer");
            return false;
        }
        size_t prevSize = m_mesh.normals.size();
        m_mesh.normals.resize(prevSize + numVertices);
        for (uint16_t i = 0; i < numVertices; i++) {
            m_mesh.normals[prevSize + i].x = read<float>(data, size, offset);
            m_mesh.normals[prevSize + i].y = read<float>(data, size, offset);
            m_mesh.normals[prevSize + i].z = read<float>(data, size, offset);
        }

        if (bsVectorFlags & 0x1000) {
            skip(offset, static_cast<size_t>(numVertices) * 3 * sizeof(float)); // tangents
            skip(offset, static_cast<size_t>(numVertices) * 3 * sizeof(float)); // bitangents
        }
    }

    // ── Bounding sphere ──
    read<float>(data, size, offset); // center.x
    read<float>(data, size, offset); // center.y
    read<float>(data, size, offset); // center.z
    read<float>(data, size, offset); // radius

    // ── Vertex colors ──
    uint8_t hasVertexColors = read<uint8_t>(data, size, offset);
    m_mesh.colors.resize(m_mesh.vertices.size());
    if (hasVertexColors) {
        for(uint32_t i=0;i<numVertices;++i) {
            auto& color=m_mesh.colors[baseVertex+i];
            color.r=read<float>(data,size,offset);color.g=read<float>(data,size,offset);
            color.b=read<float>(data,size,offset);color.a=read<float>(data,size,offset);
            sanitizeColor(color);
        }
    }

    // ── UV sets ──
    uint16_t numUVSets = (bsVectorFlags & 0x0001) ? 1 : 0;
    if (numUVSets == 0)
        numUVSets = bsVectorFlags & 0x003F;

    if (numUVSets > 0 && hasVertices) {
        if (offset + numVertices * sizeof(Vec2) > size) {
            logMsg("[NIF] TriStrips UV data overflows buffer");
            return false;
        }
        size_t prevSize = m_mesh.uvs.size();
        m_mesh.uvs.resize(prevSize + numVertices);
        for (uint16_t i = 0; i < numVertices; i++) {
            m_mesh.uvs[prevSize + i].u = read<float>(data, size, offset);
            m_mesh.uvs[prevSize + i].v = read<float>(data, size, offset);
        }
        if (numUVSets > 1) {
            skip(offset, static_cast<size_t>(numUVSets - 1) *
                         numVertices * sizeof(Vec2));
        }
    }

    // ── Consistency flags ──
    /*uint16_t consistencyFlags =*/ read<uint16_t>(data, size, offset);

    // ── Additional data ref ──
    /*int32_t additionalDataRef =*/ read<int32_t>(data, size, offset);

    // ── NiTriBasedGeomData: Num Triangles ──
    /*uint16_t numTriangles =*/ read<uint16_t>(data, size, offset);

    // ── Strips ──
    uint16_t numStrips = read<uint16_t>(data, size, offset);

    // Read strip lengths
    std::vector<uint16_t> stripLengths(numStrips);
    for (uint16_t s = 0; s < numStrips; s++) {
        stripLengths[s] = read<uint16_t>(data, size, offset);
    }

    // hasPoints flag
    uint8_t hasPoints = read<uint8_t>(data, size, offset);

    if (hasPoints) {
        for (uint16_t s = 0; s < numStrips; s++) {
            uint16_t len = stripLengths[s];
            if (len < 3) {
                // Degenerate strip, just skip
                skip(offset, static_cast<size_t>(len) * sizeof(uint16_t));
                continue;
            }

            // Read strip indices
            std::vector<uint16_t> strip(len);
            for (uint16_t i = 0; i < len; i++) {
                strip[i] = read<uint16_t>(data, size, offset);
                if (strip[i] >= numVertices) return false;
            }

            // Convert strip to triangle list
            for (uint16_t i = 0; i + 2 < len; i++) {
                uint16_t a = strip[i];
                uint16_t b = strip[i + 1];
                uint16_t c = strip[i + 2];

                // Skip degenerate triangles
                if (a == b || b == c || a == c)
                    continue;

                // Alternate winding for even/odd triangles
                if (i & 1) {
                    m_mesh.indices.push_back(a + baseVertex);
                    m_mesh.indices.push_back(c + baseVertex);
                    m_mesh.indices.push_back(b + baseVertex);
                } else {
                    m_mesh.indices.push_back(a + baseVertex);
                    m_mesh.indices.push_back(b + baseVertex);
                    m_mesh.indices.push_back(c + baseVertex);
                }
            }
        }
    }

    logMsg("[NIF] NiTriStripsData: %u verts, %u strips (base offset %u)",
           numVertices, numStrips, baseVertex);
    return true;
}
