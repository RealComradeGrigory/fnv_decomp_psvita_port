/**
 * BSA Archive Reader - Implementation
 *
 * Reads Bethesda's BSA archive format version 105 (Fallout 3 / New Vegas).
 * Uses Vita sceIo* calls for file I/O and zlib for decompression.
 */

#include "bsa_reader.h"

#include <psp2/io/fcntl.h>
#include <zlib.h>

#include <cstring>
#include <cctype>
#include <algorithm>

extern void logMsg(const char* fmt, ...);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Convert a string to lowercase in-place
static void toLowerInPlace(std::string& s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

/// Return a lowercase copy
static std::string toLowerCopy(const char* s) {
    std::string out(s);
    toLowerInPlace(out);
    return out;
}

// ---------------------------------------------------------------------------
// Bethesda hash functions
// ---------------------------------------------------------------------------

/*
 * Bethesda BSA hash for a file NAME (without folder path).
 *
 * The hash is split into two 32-bit halves:
 *   hash1 (low 32 bits):
 *       Built from the last character, the length, the first character, and
 *       the extension's first three bytes.
 *   hash2 (high 32 bits):
 *       Accumulated from characters NOT already used in hash1.
 *
 * The name must already be lowercased and use backslash separators.
 */
uint64_t BSAReader::hashFileName(const char* name) {
    std::string nm(name);
    toLowerInPlace(nm);
    // Replace forward slashes with backslashes
    for (auto& c : nm) {
        if (c == '/') c = '\\';
    }

    size_t len = nm.size();
    if (len == 0) return 0;

    // Find extension (last '.')
    const char* ext = "";
    size_t dotPos = nm.rfind('.');
    if (dotPos != std::string::npos) {
        ext = nm.c_str() + dotPos;
    }

    // --- hash1 (low 32 bits) ---
    uint32_t hash1 = 0;
    // Last char
    hash1 = static_cast<uint8_t>(nm[len - 1]);
    // Length shifted into bits 16..23
    hash1 |= (len > 2) ? (static_cast<uint32_t>(len) << 16) : 0;
    // First char shifted into bits 8..15
    hash1 |= static_cast<uint32_t>(static_cast<uint8_t>(nm[0])) << 8;

    // Extension component
    if (std::strcmp(ext, ".nif") == 0)       hash1 |= 0x8000;
    else if (std::strcmp(ext, ".kf") == 0)   hash1 |= 0x8000;  // same bucket
    else if (std::strcmp(ext, ".dds") == 0)  hash1 |= 0x8080;
    else if (std::strcmp(ext, ".wav") == 0)  hash1 |= 0x80;

    // --- hash2 (high 32 bits) ---
    uint32_t hash2 = 0;
    // Accumulate from chars [1..len-3] (the "middle" characters)
    for (size_t i = 1; i + 2 < len; ++i) {
        hash2 = hash2 * 0x1003F + static_cast<uint8_t>(nm[i]);
    }

    // --- hash3: contribution from extension characters ---
    uint32_t hash3 = 0;
    if (dotPos != std::string::npos) {
        for (size_t i = dotPos; i < len; ++i) {
            hash3 = hash3 * 0x1003F + static_cast<uint8_t>(nm[i]);
        }
    }

    hash2 += hash3;

    return (static_cast<uint64_t>(hash2) << 32) | hash1;
}

/*
 * Bethesda BSA hash for a folder PATH.
 * Same algorithm as file name hash, but applied to the folder path
 * (which has no extension).
 */
uint64_t BSAReader::hashFilePath(const char* path) {
    // Folder paths have no extension, so we just call hashFileName
    // with the path.  The extension lookup will be "" and have no effect.
    return hashFileName(path);
}

// ---------------------------------------------------------------------------
// File I/O helpers (Vita sceIo*)
// ---------------------------------------------------------------------------

bool BSAReader::readBytes(uint32_t offset, void* buffer, uint32_t size) {
    if (m_fileHandle < 0) return false;

    SceOff pos = sceIoLseek(m_fileHandle, offset, SCE_SEEK_SET);
    if (pos < 0) {
        logMsg("BSA: sceIoLseek failed to offset %u (err 0x%08X)", offset, (uint32_t)pos);
        return false;
    }

    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint32_t remaining = size;
    while (remaining > 0) {
        int read = sceIoRead(m_fileHandle, dst, remaining);
        if (read <= 0) {
            logMsg("BSA: sceIoRead failed (err 0x%08X), %u bytes remaining",
                   (uint32_t)read, remaining);
            return false;
        }
        dst       += read;
        remaining -= read;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

BSAReader::~BSAReader() {
    close();
}

void BSAReader::close() {
    if (m_fileHandle >= 0) {
        sceIoClose(m_fileHandle);
        m_fileHandle = -1;
    }
    m_folders.clear();
    m_folderLookup.clear();
    m_header = {};
    m_defaultCompressed = false;
    m_override = nullptr;
}

bool BSAReader::open(const char* path) {
    close();

    // --- Open the BSA file ---
    m_fileHandle = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (m_fileHandle < 0) {
        logMsg("BSA: failed to open '%s' (err 0x%08X)", path, (uint32_t)m_fileHandle);
        return false;
    }

    // --- Read the 36-byte header ---
    if (!readBytes(0, &m_header, sizeof(BSAHeader))) {
        logMsg("BSA: failed to read header from '%s'", path);
        close();
        return false;
    }

    // Validate magic
    if (std::memcmp(m_header.magic, "BSA\0", 4) != 0) {
        logMsg("BSA: invalid magic in '%s'", path);
        close();
        return false;
    }

    // Validate version (103 = Oblivion, 104 = FO3 / FNV, 105 = Skyrim)
    if (m_header.version != 104 && m_header.version != 105 && m_header.version != 103) {
        logMsg("BSA: unsupported version %u in '%s' (expected 103, 104, or 105)",
               m_header.version, path);
        close();
        return false;
    }

    m_defaultCompressed = (m_header.archiveFlags & BSA_COMPRESSED_DEFAULT) != 0;

    logMsg("BSA: opened '%s' — %u folders, %u files, flags 0x%04X",
           path, m_header.folderCount, m_header.fileCount, m_header.archiveFlags);

    // --- Read folder records ---
    uint32_t folderRecordSize = m_header.folderCount * sizeof(BSAFolderRecord);
    std::vector<BSAFolderRecord> folderRecords(m_header.folderCount);

    // Folder records start immediately after the header (offset = folderRecordOffset, usually 36)
    if (!readBytes(m_header.folderRecordOffset, folderRecords.data(), folderRecordSize)) {
        logMsg("BSA: failed to read folder records");
        close();
        return false;
    }

    // --- Read file record blocks and folder names ---
    // After the folder records come (for each folder in order):
    //   1. A byte-length-prefixed folder name string (len byte + chars + null terminator)
    //   2. fileCount FileRecords (each 16 bytes)
    //
    // The total size of all these blocks:
    //   = totalFolderNameLen + folderCount  (name bytes + length bytes)
    //   + fileCount * sizeof(BSAFileRecord) (all file records)
    uint32_t fileRecordBlockOffset = m_header.folderRecordOffset + folderRecordSize;
    uint32_t fileRecordBlockSize = m_header.totalFolderNameLen + m_header.folderCount
                                   + m_header.fileCount * sizeof(BSAFileRecord);

    std::vector<uint8_t> fileRecordBlock(fileRecordBlockSize);
    if (!readBytes(fileRecordBlockOffset, fileRecordBlock.data(), fileRecordBlockSize)) {
        logMsg("BSA: failed to read file record blocks");
        close();
        return false;
    }

    // --- Read the file names block ---
    uint32_t fileNamesOffset = fileRecordBlockOffset + fileRecordBlockSize;
    std::vector<char> fileNamesBlock(m_header.totalFileNameLen);
    if (m_header.totalFileNameLen > 0) {
        if (!readBytes(fileNamesOffset, fileNamesBlock.data(), m_header.totalFileNameLen)) {
            logMsg("BSA: failed to read file names block");
            close();
            return false;
        }
    }

    // --- Parse the file record blocks into m_folders ---
    m_folders.resize(m_header.folderCount);

    const uint8_t* ptr = fileRecordBlock.data();
    const uint8_t* blockEnd = ptr + fileRecordBlock.size();

    for (uint32_t fi = 0; fi < m_header.folderCount; ++fi) {
        BSAFolderEntry& folder = m_folders[fi];

        // Read the byte-prefixed folder name
        if (ptr >= blockEnd) {
            logMsg("BSA: unexpected end of file record block at folder %u", fi);
            close();
            return false;
        }
        uint8_t nameLen = *ptr++;
        if (ptr + nameLen > blockEnd) {
            logMsg("BSA: folder name overflows block at folder %u", fi);
            close();
            return false;
        }
        // nameLen includes the null terminator
        folder.name.assign(reinterpret_cast<const char*>(ptr), nameLen > 0 ? nameLen - 1 : 0);
        toLowerInPlace(folder.name);
        ptr += nameLen; // skip past name + null terminator

        // Read file records for this folder
        uint32_t numFiles = folderRecords[fi].fileCount;
        folder.files.resize(numFiles);

        for (uint32_t fj = 0; fj < numFiles; ++fj) {
            if (ptr + sizeof(BSAFileRecord) > blockEnd) {
                logMsg("BSA: file record overflows block at folder %u, file %u", fi, fj);
                close();
                return false;
            }
            const BSAFileRecord* rec = reinterpret_cast<const BSAFileRecord*>(ptr);
            ptr += sizeof(BSAFileRecord);

            BSAFileEntry& entry = folder.files[fj];
            uint32_t rawSize = rec->size;
            // Bit 30 (0x40000000) toggles compression relative to the archive default
            bool compressionToggle = (rawSize & 0x40000000) != 0;
            entry.size       = rawSize & 0x3FFFFFFF;
            entry.offset     = rec->offset;
            entry.compressed = m_defaultCompressed ^ compressionToggle;
        }

        // Build lookup table
        m_folderLookup[folder.name] = fi;
    }

    // --- Assign file names from the file names block ---
    const char* namePtr = fileNamesBlock.data();
    const char* namesEnd = namePtr + fileNamesBlock.size();

    for (auto& folder : m_folders) {
        for (auto& file : folder.files) {
            if (namePtr >= namesEnd) {
                logMsg("BSA: ran out of file names");
                file.name = "???";
                continue;
            }
            // Names are null-terminated, packed sequentially
            size_t maxLen = static_cast<size_t>(namesEnd - namePtr);
            size_t slen = strnlen(namePtr, maxLen);
            file.name.assign(namePtr, slen);
            toLowerInPlace(file.name);
            namePtr += slen + 1; // skip past null terminator
        }
    }

    logMsg("BSA: directory parsed successfully");
    return true;
}

// ---------------------------------------------------------------------------
// File extraction
// ---------------------------------------------------------------------------

bool BSAReader::extractFile(const char* folderName, const char* fileName,
                            std::vector<uint8_t>& outData) {
    if(m_override && m_override->containsFile(folderName,fileName))
        return m_override->extractFile(folderName,fileName,outData);
    if (m_fileHandle < 0) {
        logMsg("BSA: extractFile called but no archive is open");
        return false;
    }

    // Normalize inputs
    std::string folderKey = toLowerCopy(folderName);
    std::string fileKey   = toLowerCopy(fileName);

    // Replace forward slashes with backslashes in folder path
    for (auto& c : folderKey) {
        if (c == '/') c = '\\';
    }

    // Strip trailing backslash if present
    while (!folderKey.empty() && folderKey.back() == '\\') {
        folderKey.pop_back();
    }

    // Look up folder (use the map for O(1) lookup)
    auto it = m_folderLookup.find(folderKey);
    if (it == m_folderLookup.end()) {
        logMsg("BSA: folder not found: '%s'", folderKey.c_str());
        return false;
    }

    const BSAFolderEntry& folder = m_folders[it->second];

    // Linear search for the file by name (case-insensitive, already lowered)
    const BSAFileEntry* found = nullptr;
    for (const auto& f : folder.files) {
        if (f.name == fileKey) {
            found = &f;
            break;
        }
    }

    if (!found) {
        logMsg("BSA: file not found: '%s' in folder '%s'", fileKey.c_str(), folderKey.c_str());
        return false;
    }

    uint32_t dataOffset = found->offset;
    uint32_t dataSize   = found->size;

    // If BSA_EMBED_FILENAMES flag is set, the raw data starts with a
    // byte-length-prefixed filename string that we need to skip.
    if (m_header.archiveFlags & BSA_EMBED_FILENAMES) {
        uint8_t embedNameLen = 0;
        if (!readBytes(dataOffset, &embedNameLen, 1)) {
            logMsg("BSA: failed to read embedded filename length");
            return false;
        }
        uint32_t skip = 1 + embedNameLen;
        dataOffset += skip;
        if (dataSize >= skip) {
            dataSize -= skip;
        } else {
            logMsg("BSA: embedded filename length exceeds data size");
            return false;
        }
    }

    if (found->compressed) {
        // --- Decompress with zlib ---
        // First 4 bytes of compressed data = original uncompressed size (uint32_t LE)
        if (dataSize < 4) {
            logMsg("BSA: compressed data too small (%u bytes)", dataSize);
            return false;
        }

        uint32_t originalSize = 0;
        if (!readBytes(dataOffset, &originalSize, 4)) {
            logMsg("BSA: failed to read original size for compressed file");
            return false;
        }

        uint32_t compressedSize = dataSize - 4;
        uint32_t compOffset     = dataOffset + 4;

        // Sanity check on decompressed size (256 MB limit)
        if (originalSize > 256 * 1024 * 1024) {
            logMsg("BSA: decompressed size suspiciously large: %u bytes", originalSize);
            return false;
        }

        // Read compressed data
        std::vector<uint8_t> compData(compressedSize);
        if (!readBytes(compOffset, compData.data(), compressedSize)) {
            logMsg("BSA: failed to read compressed data (%u bytes at offset %u)",
                   compressedSize, compOffset);
            return false;
        }

        // Decompress
        outData.resize(originalSize);
        uLongf destLen = originalSize;
        int zResult = uncompress(outData.data(), &destLen, compData.data(), compressedSize);
        if (zResult != Z_OK) {
            logMsg("BSA: zlib uncompress failed (error %d) for '%s/%s'",
                   zResult, folderKey.c_str(), fileKey.c_str());
            outData.clear();
            return false;
        }

        outData.resize(static_cast<size_t>(destLen));
        logMsg("BSA: decompressed '%s/%s' (%u -> %u bytes)",
               folderKey.c_str(), fileKey.c_str(), compressedSize, (uint32_t)destLen);
    } else {
        // --- Uncompressed: just copy the raw bytes ---
        outData.resize(dataSize);
        if (!readBytes(dataOffset, outData.data(), dataSize)) {
            logMsg("BSA: failed to read file data (%u bytes at offset %u)",
                   dataSize, dataOffset);
            outData.clear();
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Directory listing
// ---------------------------------------------------------------------------

std::vector<std::string> BSAReader::listFolders() const {
    std::vector<std::string> result=m_override?m_override->listFolders():std::vector<std::string>{};
    result.reserve(m_folders.size());
    for (const auto& folder : m_folders) {
        result.push_back(folder.name);
    }
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}

bool BSAReader::containsFile(const char* folderName,const char* fileName) const {
    std::string key=toLowerCopy(folderName),name=toLowerCopy(fileName);
    for(auto& c:key)if(c=='/')c='\\';
    while(!key.empty() && key.back()=='\\')key.pop_back();
    auto found=m_folderLookup.find(key);if(found==m_folderLookup.end())return false;
    for(const auto& f:m_folders[found->second].files)if(f.name==name)return true;
    return false;
}

std::vector<std::string> BSAReader::listFiles(const char* folderName) const {
    std::vector<std::string> result=m_override?m_override->listFiles(folderName):std::vector<std::string>{};

    std::string key = toLowerCopy(folderName);
    for (auto& c : key) {
        if (c == '/') c = '\\';
    }
    while (!key.empty() && key.back() == '\\') {
        key.pop_back();
    }

    auto it = m_folderLookup.find(key);
    if (it == m_folderLookup.end()) {
        return result;
    }

    const BSAFolderEntry& folder = m_folders[it->second];
    result.reserve(folder.files.size());
    for (const auto& f : folder.files) {
        result.push_back(f.name);
    }
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
