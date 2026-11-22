/**
 * BSA Archive Reader - Header
 *
 * Reads Bethesda's BSA archive format (version 105, used by FO3/FNV).
 * BSA files are essentially big ZIP-like containers holding all game assets
 * organized into folder/file hierarchies.
 *
 * Format reference: https://en.uesp.net/wiki/Oblivion_Mod:BSA_File_Format
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// BSA archive flags (from header)
enum BSAArchiveFlags : uint32_t {
    BSA_INCLUDES_DIRNAMES   = 0x0001,
    BSA_INCLUDES_FILENAMES  = 0x0002,
    BSA_COMPRESSED_DEFAULT  = 0x0004,
    BSA_RETAIN_DIRNAMES     = 0x0008,
    BSA_RETAIN_FILENAMES    = 0x0010,
    BSA_RETAIN_OFFSETS      = 0x0020,
    BSA_XBOX360             = 0x0040,
    BSA_RETAIN_STRINGS      = 0x0080,
    BSA_EMBED_FILENAMES     = 0x0100,
    BSA_XMEM_CODEC          = 0x0200,
};

// On-disk BSA header (36 bytes)
struct BSAHeader {
    char     magic[4];              // "BSA\0"
    uint32_t version;               // 105 for FO3/FNV
    uint32_t folderRecordOffset;    // typically 36
    uint32_t archiveFlags;
    uint32_t folderCount;
    uint32_t fileCount;
    uint32_t totalFolderNameLen;
    uint32_t totalFileNameLen;
    uint32_t fileFlags;
};

// On-disk folder record (16 bytes for 32-bit version)
struct BSAFolderRecord {
    uint64_t nameHash;
    uint32_t fileCount;
    uint32_t offset;    // offset to file records for this folder
};

// On-disk file record (16 bytes)
struct BSAFileRecord {
    uint64_t nameHash;
    uint32_t size;      // bit 30 = compression toggle (inverts default)
    uint32_t offset;    // absolute offset to raw data in BSA
};

// In-memory file entry
struct BSAFileEntry {
    std::string name;
    uint32_t    size;
    uint32_t    offset;
    bool        compressed;
};

// In-memory folder entry
struct BSAFolderEntry {
    std::string name;
    std::vector<BSAFileEntry> files;
};

/**
 * BSA archive reader.
 * Opens a .bsa file and allows extracting individual files by path.
 */
class BSAReader {
public:
    BSAReader() = default;
    ~BSAReader();

    // Open a BSA file and read its directory
    bool open(const char* path);

    // Close the BSA file
    void close();

    // Extract a file by folder name + file name into a buffer
    // folderName should use backslashes and be lowercase, e.g. "meshes\\weapons\\10mmpistol"
    // fileName should be lowercase, e.g. "10mmpistol.nif"
    bool extractFile(const char* folderName, const char* fileName,
                     std::vector<uint8_t>& outData);
    // Update.bsa overrides base-game files. The archive must outlive this reader.
    void setOverrideArchive(BSAReader* archive) {m_override=archive==this?nullptr:archive;}
    bool containsFile(const char* folderName,const char* fileName) const;

    // List all folder names
    std::vector<std::string> listFolders() const;

    // List all files in a folder
    std::vector<std::string> listFiles(const char* folderName) const;

    // Accessors
    uint32_t getFolderCount() const { return m_header.folderCount; }
    uint32_t getFileCount() const { return m_header.fileCount; }
    bool isOpen() const { return m_fileHandle >= 0; }

private:
    // Bethesda's hash function for BSA lookups
    static uint64_t hashFilePath(const char* path);
    static uint64_t hashFileName(const char* name);

    // Read raw bytes from the BSA at a given offset
    bool readBytes(uint32_t offset, void* buffer, uint32_t size);

    int          m_fileHandle = -1;
    BSAHeader    m_header = {};
    bool         m_defaultCompressed = false;
    BSAReader*   m_override = nullptr;

    // Directory structure
    std::vector<BSAFolderEntry> m_folders;

    // Quick lookup: lowercase folder name -> index in m_folders
    std::unordered_map<std::string, size_t> m_folderLookup;
};
