/**
 * NIF Mesh Loader - Header
 *
 * Parses Gamebryo NIF (NetImmerse File) format version 20.2.0.7
 * used by Fallout 3 and Fallout: New Vegas.
 *
 * This is a minimal loader - it extracts vertex positions, normals,
 * UV coordinates, and triangle indices from NiTriShape/NiTriStrips blocks.
 * It does NOT handle the full NIF spec (animation, particles, etc.)
 *
 * Format reference: https://en.uesp.net/wiki/Nif_File_Format
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>

// A simple 3D vector
struct Vec3 {
    float x, y, z;
};

// A 2D UV coordinate
struct Vec2 {
    float u, v;
};

struct NifColor { float r=1,g=1,b=1,a=1; };

struct NifMeshPart {
    bool transparent = false;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    std::string diffuseTexture;
};

// Output mesh data - what we send to the renderer
struct NifMesh {
    std::vector<Vec3>     vertices;
    std::vector<Vec3>     normals;
    std::vector<Vec2>     uvs;
    std::vector<NifColor> colors;
    std::vector<uint16_t> indices;      // triangle indices

    uint32_t    vertexCount = 0;
    uint32_t    triangleCount = 0;
    Vec3        center = { 0.0f, 0.0f, 0.0f };
    float       radius = 50.0f;
    std::string diffuseTexture;
    std::vector<NifMeshPart> parts;
};

// NIF block types we care about
enum class NifBlockType {
    Unknown,
    NiNode,
    NiTriShape,
    NiTriShapeData,
    NiTriStrips,
    NiTriStripsData,
    BSFadeNode,
    BSShaderTextureSet,
    BSShaderPPLightingProperty,
    BSShaderNoLightingProperty,
    NiAlphaProperty,
    // We'll add more as needed
};

// Minimal NIF header info
struct NifHeader {
    char     versionString[64];
    uint32_t version;           // 0x14020007 = 20.2.0.7
    uint8_t  endianness;        // 1 = little endian
    uint32_t userVersion;
    uint32_t numBlocks;
    uint32_t userVersion2;

    std::vector<std::string>  blockTypeNames;
    std::vector<uint16_t>     blockTypeIndices;
    std::vector<uint32_t>     blockSizes;
};

/**
 * NIF file loader.
 * Reads a NIF file from memory and extracts mesh geometry.
 */
class NifLoader {
public:
    NifLoader() = default;

    // Load a NIF from a memory buffer
    // Placed REFRs replace the NIF root transform with their own transform;
    // child transforms still belong to the mesh. Standalone assets retain it.
    bool load(const uint8_t* data, size_t size, bool placedReference = false);

    // Get the combined mesh data (all NiTriShape blocks merged)
    const NifMesh& getMesh() const { return m_mesh; }

private:
    // Parse the NIF header
    bool parseHeader(const uint8_t* data, size_t size, size_t& offset);

    // Parse a single block
    bool parseBlock(const uint8_t* data, size_t size, size_t& offset,
                    uint32_t blockIndex);

    // Parse NiTriShapeData - the actual geometry
    bool parseTriShapeData(const uint8_t* data, size_t size, size_t& offset);

    // Parse NiTriStripsData
    bool parseTriStripsData(const uint8_t* data, size_t size, size_t& offset);
    bool parseSceneObject(const uint8_t* data, size_t size, size_t& offset,
                          uint32_t blockIndex, bool isNode);
    bool skipObjectNet(const uint8_t* data, size_t size, size_t& offset);
    bool assembleScene();

    struct Transform {
        float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        Vec3 translation{0, 0, 0};
        float scale = 1.0f;
    };
    struct SceneObject {
        bool present = false;
        bool hidden = false;
        int32_t parent = -1;
        int32_t data = -1;
        Transform local;
        std::vector<int32_t> children;
        std::vector<int32_t> properties;
    };
    struct GeometryRange {
        uint32_t firstVertex = 0, vertexCount = 0;
        uint32_t firstIndex = 0, indexCount = 0;
    };
    std::vector<SceneObject> m_sceneObjects;
    std::vector<GeometryRange> m_geometry;
    std::vector<std::string> m_textureSets;
    std::vector<int32_t> m_shaderTextures;
    std::vector<uint16_t> m_alphaFlags;

    // Utility: read typed value from buffer
    template<typename T>
    T read(const uint8_t* data, size_t size, size_t& offset);

    // Read a NIF-format string (uint32 length + chars)
    std::string readSizedString(const uint8_t* data, size_t size, size_t& offset);

    // Read a short string (uint8 length + chars)
    std::string readShortString(const uint8_t* data, size_t size, size_t& offset);

    // Skip N bytes
    void skip(size_t& offset, size_t count) {
        if (offset > m_readLimit || count > m_readLimit - offset) {
            m_readFailed = true;
            offset = m_readLimit;
        } else {
            offset += count;
        }
    }

    NifHeader m_header;
    NifMesh   m_mesh;
    bool m_readFailed = false;
    bool m_placedReference = false;
    size_t m_readLimit = 0;

    // Block type resolution
    NifBlockType resolveBlockType(uint32_t blockIndex) const;
};
