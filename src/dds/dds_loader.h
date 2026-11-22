/**
 * DDS Texture Loader - Header
 *
 * Loads DirectDraw Surface (.dds) texture files used by FNV.
 * FNV primarily uses DXT1 and DXT5 compressed textures.
 *
 * For the Vita, we'll need to convert DXT to a format GXM supports
 * or decompress to raw RGBA8888.
 */

#pragma once

#include <cstdint>
#include <vector>

// DDS pixel format flags
enum DDSPixelFormatFlags : uint32_t {
    DDPF_ALPHAPIXELS = 0x1,
    DDPF_FOURCC      = 0x4,
    DDPF_RGB         = 0x40,
};

// FourCC codes
constexpr uint32_t FOURCC_DXT1 = 0x31545844; // "DXT1"
constexpr uint32_t FOURCC_DXT3 = 0x33545844; // "DXT3"
constexpr uint32_t FOURCC_DXT5 = 0x35545844; // "DXT5"

// DDS pixel format (32 bytes)
struct DDSPixelFormat {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

// DDS header (124 bytes after magic)
struct DDSHeader {
    uint32_t       size;
    uint32_t       flags;
    uint32_t       height;
    uint32_t       width;
    uint32_t       pitchOrLinearSize;
    uint32_t       depth;
    uint32_t       mipMapCount;
    uint32_t       reserved1[11];
    DDSPixelFormat pixelFormat;
    uint32_t       caps;
    uint32_t       caps2;
    uint32_t       caps3;
    uint32_t       caps4;
    uint32_t       reserved2;
};

// Decompressed texture output
struct TextureData {
    uint32_t              width;
    uint32_t              height;
    uint32_t              mipLevels;
    std::vector<uint8_t>  pixels;   // RGBA8888
    bool                  hasAlpha;
};

/**
 * DDS texture loader.
 * Loads DDS files and decompresses DXT to RGBA8888 for GXM.
 */
class DDSLoader {
public:
    DDSLoader() = default;

    // Load a DDS texture from memory
    bool load(const uint8_t* data, size_t size, uint32_t maxDimension = 512);

    // Get the decompressed texture
    const TextureData& getTexture() const { return m_texture; }

private:
    bool finishTexture(uint32_t maxDimension);
    bool decompressDXT1(const uint8_t* src, uint32_t width, uint32_t height);
    bool decompressDXT3(const uint8_t* src, uint32_t width, uint32_t height);
    bool decompressDXT5(const uint8_t* src, uint32_t width, uint32_t height);

    // Decode a single DXT color block (shared by DXT1/DXT5)
    static void decodeColorBlock(const uint8_t* block, uint8_t* outPixels,
                                 uint32_t stride, bool isDXT1,
                                 uint32_t maxRows = 4, uint32_t maxCols = 4);

    // Decode a DXT5 alpha block
    static void decodeAlphaBlock(const uint8_t* block, uint8_t* outAlpha,
                                 uint32_t stride,
                                 uint32_t maxRows = 4, uint32_t maxCols = 4);

    TextureData m_texture;
};
