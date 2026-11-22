/**
 * DDS Texture Loader - Implementation
 *
 * Decompresses DXT1/DXT5 textures to RGBA8888 for use with GXM.
 */

#include "dds_loader.h"
#include <cstring>
#include <algorithm>

extern void logMsg(const char* fmt, ...);

static constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "

bool DDSLoader::load(const uint8_t* data, size_t size, uint32_t maxDimension) {
    m_texture = {};
    if (!data || maxDimension == 0 || size < 4 + sizeof(DDSHeader)) {
        logMsg("DDS: File too small (%u bytes)", (unsigned int)size);
        return false;
    }

    // Check magic
    uint32_t magic;
    memcpy(&magic, data, 4);
    if (magic != DDS_MAGIC) {
        logMsg("DDS: Invalid magic (0x%08X)", magic);
        return false;
    }

    // Read header
    DDSHeader header;
    memcpy(&header, data + 4, sizeof(DDSHeader));
    if (header.size != 124 || header.pixelFormat.size != 32 ||
        header.width == 0 || header.height == 0 || header.width > 4096 ||
        header.height > 4096 || header.depth > 1 || header.caps2 != 0) {
        logMsg("DDS: invalid header or unsupported texture dimensions/type");
        return false;
    }

    m_texture.width = header.width;
    m_texture.height = header.height;
    m_texture.mipLevels = header.mipMapCount > 0 ? header.mipMapCount : 1;
    m_texture.hasAlpha = false;

    logMsg("DDS: %ux%u, %u mips", header.width, header.height, m_texture.mipLevels);

    const uint8_t* pixelData = data + 4 + sizeof(DDSHeader);
    size_t pixelDataSize = size - 4 - sizeof(DDSHeader);

    // Determine format and decompress
    if (header.pixelFormat.flags & DDPF_FOURCC) {
        uint32_t blockBytes = (header.pixelFormat.fourCC == FOURCC_DXT1) ? 8 : 16;
        uint32_t mip = 0;
        while ((header.width > maxDimension || header.height > maxDimension) &&
               mip + 1 < header.mipMapCount) {
            size_t levelBytes = static_cast<size_t>((header.width + 3) / 4) *
                                ((header.height + 3) / 4) * blockBytes;
            if (levelBytes > pixelDataSize) return false;
            pixelData += levelBytes;
            pixelDataSize -= levelBytes;
            header.width = std::max(1u, header.width / 2);
            header.height = std::max(1u, header.height / 2);
            ++mip;
        }
        m_texture.width = header.width;
        m_texture.height = header.height;
        size_t requiredBytes = static_cast<size_t>((header.width + 3) / 4) *
                               ((header.height + 3) / 4) * blockBytes;
        if (requiredBytes > pixelDataSize) {
            logMsg("DDS: truncated compressed texture");
            return false;
        }
        switch (header.pixelFormat.fourCC) {
            case FOURCC_DXT1:
                logMsg("DDS: Format DXT1");
                return decompressDXT1(pixelData, header.width, header.height) && finishTexture(maxDimension);

            case FOURCC_DXT3:
                logMsg("DDS: Format DXT3");
                m_texture.hasAlpha = true;
                return decompressDXT3(pixelData, header.width, header.height) && finishTexture(maxDimension);

            case FOURCC_DXT5:
                logMsg("DDS: Format DXT5");
                m_texture.hasAlpha = true;
                return decompressDXT5(pixelData, header.width, header.height) && finishTexture(maxDimension);

            default:
                logMsg("DDS: Unsupported FourCC format: 0x%08X", header.pixelFormat.fourCC);
                return false;
        }
    } else if (header.pixelFormat.flags & DDPF_RGB) {
        // Uncompressed RGB(A)
        logMsg("DDS: Uncompressed %u-bit RGB", header.pixelFormat.rgbBitCount);
        uint32_t numPixels = header.width * header.height;
        uint32_t bits = header.pixelFormat.rgbBitCount;
        if (bits != 16 && bits != 24 && bits != 32) return false;
        uint32_t bytesPerPixel = bits / 8;
        size_t rowBytes = static_cast<size_t>(header.width) * bytesPerPixel;
        size_t pitch = (header.flags & 0x8) ? header.pitchOrLinearSize : rowBytes;
        if (pitch < rowBytes || pitch > pixelDataSize / header.height) {
            logMsg("DDS: invalid or truncated RGB texture");
            return false;
        }
        m_texture.pixels.resize(numPixels * 4);

        uint32_t masks[] = {header.pixelFormat.rBitMask, header.pixelFormat.gBitMask,
                            header.pixelFormat.bBitMask, header.pixelFormat.aBitMask};
        m_texture.hasAlpha = (header.pixelFormat.flags & DDPF_ALPHAPIXELS) && masks[3];
        for (uint32_t i = 0; i < 4; ++i) {
            if (i == 3 && !m_texture.hasAlpha) continue;
            uint32_t mask = masks[i];
            if (!mask || (bits < 32 && (mask >> bits))) return false;
            while (!(mask & 1)) mask >>= 1;
            if ((mask & (mask + 1)) != 0) return false;
            for (uint32_t j = 0; j < i; ++j) if (masks[i] & masks[j]) return false;
        }
        for (uint32_t y = 0; y < header.height; ++y) {
            for (uint32_t x = 0; x < header.width; ++x) {
                uint32_t value = 0;
                memcpy(&value, pixelData + y * pitch + x * bytesPerPixel, bytesPerPixel);
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    uint8_t decoded = 255;
                    if (channel != 3 || m_texture.hasAlpha) {
                        uint32_t mask = masks[channel], shift = 0;
                        while (!(mask & 1)) { mask >>= 1; ++shift; }
                        decoded = (static_cast<uint64_t>((value >> shift) & mask) * 255 + mask / 2) / mask;
                    }
                    m_texture.pixels[(y * header.width + x) * 4 + channel] = decoded;
                }
            }
        }
        return finishTexture(maxDimension);
    }

    logMsg("DDS: Unrecognized format");
    return false;
}

bool DDSLoader::finishTexture(uint32_t maxDimension) {
    while (m_texture.width > maxDimension || m_texture.height > maxDimension) {
        uint32_t width = std::max(1u, m_texture.width / 2);
        uint32_t height = std::max(1u, m_texture.height / 2);
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    uint32_t sum = 0;
                    for (uint32_t dy = 0; dy < 2; ++dy) {
                        for (uint32_t dx = 0; dx < 2; ++dx) {
                            uint32_t sx = std::min(m_texture.width - 1, x * 2 + dx);
                            uint32_t sy = std::min(m_texture.height - 1, y * 2 + dy);
                            sum += m_texture.pixels[(sy * m_texture.width + sx) * 4 + channel];
                        }
                    }
                    pixels[(y * width + x) * 4 + channel] = (sum + 2) / 4;
                }
            }
        }
        m_texture.pixels = std::move(pixels);
        m_texture.width = width;
        m_texture.height = height;
    }
    m_texture.mipLevels = 1;
    return true;
}

void DDSLoader::decodeColorBlock(const uint8_t* block, uint8_t* outPixels,
                                  uint32_t stride, bool isDXT1,
                                  uint32_t maxRows, uint32_t maxCols) {
    // First 4 bytes: two 16-bit RGB565 colors
    uint16_t c0 = block[0] | (block[1] << 8);
    uint16_t c1 = block[2] | (block[3] << 8);

    // Expand RGB565 to RGB888
    uint8_t colors[4][4]; // [index][R,G,B,A]

    colors[0][0] = ((c0 >> 11) & 0x1F) * 255 / 31;
    colors[0][1] = ((c0 >> 5)  & 0x3F) * 255 / 63;
    colors[0][2] = (c0         & 0x1F) * 255 / 31;
    colors[0][3] = 255;

    colors[1][0] = ((c1 >> 11) & 0x1F) * 255 / 31;
    colors[1][1] = ((c1 >> 5)  & 0x3F) * 255 / 63;
    colors[1][2] = (c1         & 0x1F) * 255 / 31;
    colors[1][3] = 255;

    if (c0 > c1 || !isDXT1) {
        // 4-color block: interpolate 1/3 and 2/3
        for (int i = 0; i < 3; i++) {
            colors[2][i] = (2 * colors[0][i] + colors[1][i] + 1) / 3;
            colors[3][i] = (colors[0][i] + 2 * colors[1][i] + 1) / 3;
        }
        colors[2][3] = 255;
        colors[3][3] = 255;
    } else {
        // 3-color + transparent (DXT1 only)
        for (int i = 0; i < 3; i++) {
            colors[2][i] = (colors[0][i] + colors[1][i]) / 2;
            colors[3][i] = 0;
        }
        colors[2][3] = 255;
        colors[3][3] = 0;  // transparent
    }

    // 4 bytes of 2-bit indices (16 pixels in a 4x4 block)
    for (uint32_t row = 0; row < 4 && row < maxRows; row++) {
        uint8_t bits = block[4 + row];
        for (uint32_t col = 0; col < 4 && col < maxCols; col++) {
            uint8_t idx = (bits >> (col * 2)) & 0x3;
            uint8_t* dst = outPixels + row * stride + col * 4;
            dst[0] = colors[idx][0];
            dst[1] = colors[idx][1];
            dst[2] = colors[idx][2];
            dst[3] = colors[idx][3];
        }
    }
}

void DDSLoader::decodeAlphaBlock(const uint8_t* block, uint8_t* outAlpha,
                                  uint32_t stride,
                                  uint32_t maxRows, uint32_t maxCols) {
    uint8_t a0 = block[0];
    uint8_t a1 = block[1];

    uint8_t alphas[8];
    alphas[0] = a0;
    alphas[1] = a1;

    if (a0 > a1) {
        for (int i = 0; i < 6; i++) {
            alphas[2 + i] = ((6 - i) * a0 + (1 + i) * a1 + 3) / 7;
        }
    } else {
        for (int i = 0; i < 4; i++) {
            alphas[2 + i] = ((4 - i) * a0 + (1 + i) * a1 + 2) / 5;
        }
        alphas[6] = 0;
        alphas[7] = 255;
    }

    // 6 bytes = 48 bits = 16 x 3-bit indices
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) {
        bits |= (uint64_t)block[2 + i] << (8 * i);
    }

    for (uint32_t row = 0; row < 4 && row < maxRows; row++) {
        for (uint32_t col = 0; col < 4 && col < maxCols; col++) {
            int pixelIndex = row * 4 + col;
            uint8_t idx = (bits >> (3 * pixelIndex)) & 0x7;
            outAlpha[row * stride + col * 4 + 3] = alphas[idx];
        }
    }
}

bool DDSLoader::decompressDXT1(const uint8_t* src, uint32_t width, uint32_t height) {
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    uint32_t stride = width * 4; // bytes per row

    m_texture.pixels.resize(width * height * 4, 255);

    for (uint32_t by = 0; by < blocksY; by++) {
        uint32_t maxRows = std::min(4u, height - by * 4);
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            uint32_t maxCols = std::min(4u, width - bx * 4);
            uint32_t blockIndex = by * blocksX + bx;
            const uint8_t* block = src + blockIndex * 8; // DXT1 = 8 bytes/block

            uint8_t* dst = m_texture.pixels.data() + by * 4 * stride + bx * 4 * 4;
            decodeColorBlock(block, dst, stride, true, maxRows, maxCols);
        }
    }

    return true;
}

bool DDSLoader::decompressDXT3(const uint8_t* src, uint32_t width, uint32_t height) {
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    uint32_t stride = width * 4;

    m_texture.pixels.resize(width * height * 4, 255);

    for (uint32_t by = 0; by < blocksY; by++) {
        uint32_t maxRows = std::min(4u, height - by * 4);
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            uint32_t maxCols = std::min(4u, width - bx * 4);
            uint32_t blockIndex = by * blocksX + bx;
            const uint8_t* block = src + blockIndex * 16; // DXT3 = 16 bytes/block

            uint8_t* dst = m_texture.pixels.data() + by * 4 * stride + bx * 4 * 4;

            decodeColorBlock(block + 8, dst, stride, false, maxRows, maxCols);

            // First 8 bytes: 16 4-bit explicit alpha values (2 bytes per row)
            for (uint32_t row = 0; row < 4 && row < maxRows; row++) {
                uint16_t rowAlpha = block[row * 2] | (block[row * 2 + 1] << 8);
                for (uint32_t col = 0; col < 4 && col < maxCols; col++) {
                    uint8_t a4 = (rowAlpha >> (col * 4)) & 0x0F;
                    dst[row * stride + col * 4 + 3] = (a4 << 4) | a4;
                }
            }

        }
    }

    return true;
}

bool DDSLoader::decompressDXT5(const uint8_t* src, uint32_t width, uint32_t height) {
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    uint32_t stride = width * 4;

    m_texture.pixels.resize(width * height * 4, 255);

    for (uint32_t by = 0; by < blocksY; by++) {
        uint32_t maxRows = std::min(4u, height - by * 4);
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            uint32_t maxCols = std::min(4u, width - bx * 4);
            uint32_t blockIndex = by * blocksX + bx;
            const uint8_t* block = src + blockIndex * 16; // DXT5 = 16 bytes/block

            uint8_t* dst = m_texture.pixels.data() + by * 4 * stride + bx * 4 * 4;

            // Last 8 bytes: color block
            decodeColorBlock(block + 8, dst, stride, false, maxRows, maxCols);
            // Preserve alpha after the color decoder initializes the pixels.
            decodeAlphaBlock(block, dst, stride, maxRows, maxCols);
        }
    }

    return true;
}
