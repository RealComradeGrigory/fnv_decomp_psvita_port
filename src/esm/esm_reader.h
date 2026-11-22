/**
 * ESM/ESP Reader - Header
 *
 * Minimal parser for Bethesda's Elder Scrolls Master/Plugin file format
 * used by Fallout: New Vegas (.esm/.esp files).
 *
 * These files contain ALL game world data: cells, objects, NPCs, quests,
 * dialogue, scripts, etc. For Phase 1 we only care about:
 * - TES4 header (file info)
 * - CELL records (interior/exterior cells)
 * - REFR records (placed object references in cells)
 * - STAT/MSTT records (static objects with mesh paths)
 *
 * Format reference: https://en.uesp.net/wiki/Fallout_3:Record_Format
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// Record header (20 bytes for regular records, 16 for GRUP)
struct RecordHeader {
    char     type[4];       // "TES4", "GRUP", "CELL", "REFR", etc.
    uint32_t dataSize;      // size of record data (not including header)
    uint32_t flags;         // record flags (compressed, etc.)
    uint32_t formID;        // unique ID of this record
    uint32_t versionControl;
    // Note: GRUP headers have a different layout after type+size
};

// A placed object reference (REFR record)
struct ObjectReference {
    uint32_t formID;        // this ref's form ID
    uint32_t baseFormID;    // what object this is an instance of
    float    posX, posY, posZ;
    float    rotX, rotY, rotZ;
    float    scale;
};

// A static object definition (STAT record)
struct StaticObject {
    uint32_t    formID;
    std::string meshPath;   // path to NIF file inside BSA (e.g. "meshes\\architecture\\...")
    std::string editorID;
};

// A cell (interior or exterior)
struct CellData {
    uint32_t    formID;
    std::string editorID;
    std::string name;
    bool        isInterior;
    int32_t     gridX, gridY;   // exterior grid coords
    std::vector<ObjectReference> references;
};

// Record flags
enum RecordFlags : uint32_t {
    RECORD_COMPRESSED = 0x00040000,
};

/**
 * ESM file reader.
 * Opens an .esm/.esp and parses record structure.
 */
class ESMReader {
public:
    ESMReader() = default;
    ~ESMReader();

    // Open an ESM/ESP file
    bool open(const char* path);
    void close();

    // Scan the file for records (builds index)
    bool buildIndex();

    // Get a cell by editor ID
    const CellData* getCell(const char* editorID) const;

    // Get a static object by form ID
    const StaticObject* getStaticObject(uint32_t formID) const;

    // List all cells found
    std::vector<std::string> listCells() const;

    // Get object references for a cell
    const std::vector<ObjectReference>& getCellRefs(const char* editorID) const;

private:
    bool readRecordHeader(RecordHeader& header);
    bool skipBytes(uint32_t count);
    bool readBytes(void* buffer, uint32_t size);

    // Parse specific record types
    bool parseSTAT(uint32_t dataSize);
    bool parseCELL(uint32_t dataSize, uint32_t formID, uint32_t flags);
    bool parseREFR(uint32_t dataSize, uint32_t formID);

    int m_fileHandle = -1;

    // Indexed data
    std::unordered_map<std::string, CellData>       m_cells;        // by editorID
    std::unordered_map<uint32_t, StaticObject>       m_statics;     // by formID
    std::unordered_map<uint32_t, std::string>        m_formToEditor; // formID -> editorID

    // Currently parsing cell (for associating REFRs)
    std::string m_currentCellEditorID;

    static const std::vector<ObjectReference> s_emptyRefs;
};
