/**
 * ESM/ESP Reader - Implementation
 *
 * Parses Bethesda's plugin files to extract cell/object data.
 * This is a minimal reader for Phase 1 - just enough to find
 * mesh paths and object placements.
 */

#include "esm_reader.h"
#include <psp2/io/fcntl.h>
#include <cstring>
#include <algorithm>
#include <zlib.h>

extern void logMsg(const char* fmt, ...);

const std::vector<ObjectReference> ESMReader::s_emptyRefs;

ESMReader::~ESMReader() {
    close();
}

bool ESMReader::open(const char* path) {
    m_fileHandle = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (m_fileHandle < 0) {
        logMsg("ESM: Failed to open: %s (err=0x%08X)", path, m_fileHandle);
        return false;
    }
    logMsg("ESM: Opened %s", path);
    return true;
}

void ESMReader::close() {
    if (m_fileHandle >= 0) {
        sceIoClose(m_fileHandle);
        m_fileHandle = -1;
    }
}

bool ESMReader::readBytes(void* buffer, uint32_t size) {
    int read = sceIoRead(m_fileHandle, buffer, size);
    return read == (int)size;
}

bool ESMReader::skipBytes(uint32_t count) {
    SceOff result = sceIoLseek(m_fileHandle, count, SCE_SEEK_CUR);
    return result >= 0;
}

bool ESMReader::readRecordHeader(RecordHeader& header) {
    return readBytes(&header, sizeof(RecordHeader));
}

bool ESMReader::buildIndex() {
    if (m_fileHandle < 0) return false;

    // Seek to start
    sceIoLseek(m_fileHandle, 0, SCE_SEEK_SET);

    uint32_t recordCount = 0;
    uint32_t cellCount = 0;
    uint32_t statCount = 0;

    while (true) {
        RecordHeader header;
        if (!readRecordHeader(header)) {
            break; // EOF or read error
        }

        char typeStr[5] = {};
        memcpy(typeStr, header.type, 4);

        // Handle GRUP (group) records
        if (memcmp(header.type, "GRUP", 4) == 0) {
            // GRUP header is different: dataSize includes the header itself
            // Skip the remaining GRUP header fields (we already read 20 bytes,
            // GRUP has 24-byte header total but we read recordheader format)
            // Actually for GRUP, after type(4)+size(4) comes: label(4), groupType(4), stamp(4), unk(4)
            // We already read 20 bytes as RecordHeader. GRUP data follows immediately.
            // Don't skip - just continue reading sub-records within the group.
            continue;
        }

        // Parse records we care about
        if (memcmp(header.type, "STAT", 4) == 0) {
            if (!(header.flags & RECORD_COMPRESSED)) {
                parseSTAT(header.dataSize);
                statCount++;
            } else {
                skipBytes(header.dataSize);
            }
        } else if (memcmp(header.type, "CELL", 4) == 0) {
            parseCELL(header.dataSize, header.formID, header.flags);
            cellCount++;
        } else if (memcmp(header.type, "REFR", 4) == 0) {
            if (!(header.flags & RECORD_COMPRESSED)) {
                parseREFR(header.dataSize, header.formID);
            } else {
                skipBytes(header.dataSize);
            }
        } else if (memcmp(header.type, "TES4", 4) == 0) {
            // Master header - just skip for now
            skipBytes(header.dataSize);
        } else {
            // Skip unknown record types
            skipBytes(header.dataSize);
        }

        recordCount++;

        // Progress logging every 50000 records
        if (recordCount % 50000 == 0) {
            logMsg("ESM: Scanned %u records (%u cells, %u statics)...",
                   recordCount, cellCount, statCount);
        }
    }

    logMsg("ESM: Index built - %u records, %u cells, %u statics",
           recordCount, cellCount, statCount);
    return true;
}

bool ESMReader::parseSTAT(uint32_t dataSize) {
    // Read all subrecords within this STAT record
    StaticObject obj;
    uint32_t bytesRead = 0;

    while (bytesRead < dataSize) {
        // Subrecord header: type(4) + size(2)
        char subType[4];
        uint16_t subSize;

        if (!readBytes(subType, 4)) return false;
        if (!readBytes(&subSize, 2)) return false;
        bytesRead += 6;

        if (memcmp(subType, "EDID", 4) == 0) {
            // Editor ID (null-terminated string)
            std::vector<char> buf(subSize);
            readBytes(buf.data(), subSize);
            obj.editorID = std::string(buf.data(), strnlen(buf.data(), subSize));
        } else if (memcmp(subType, "MODL", 4) == 0) {
            // Model path (null-terminated string)
            std::vector<char> buf(subSize);
            readBytes(buf.data(), subSize);
            obj.meshPath = std::string(buf.data(), strnlen(buf.data(), subSize));
        } else {
            skipBytes(subSize);
        }
        bytesRead += subSize;
    }

    if (!obj.meshPath.empty()) {
        obj.formID = 0; // set by caller context
        m_statics[obj.formID] = obj;
    }

    return true;
}

bool ESMReader::parseCELL(uint32_t dataSize, uint32_t formID, uint32_t /*flags*/) {
    CellData cell;
    cell.formID = formID;
    cell.isInterior = false;
    cell.gridX = 0;
    cell.gridY = 0;

    uint32_t bytesRead = 0;

    while (bytesRead < dataSize) {
        char subType[4];
        uint16_t subSize;

        if (!readBytes(subType, 4)) return false;
        if (!readBytes(&subSize, 2)) return false;
        bytesRead += 6;

        if (memcmp(subType, "EDID", 4) == 0) {
            std::vector<char> buf(subSize);
            readBytes(buf.data(), subSize);
            cell.editorID = std::string(buf.data(), strnlen(buf.data(), subSize));
        } else if (memcmp(subType, "FULL", 4) == 0) {
            std::vector<char> buf(subSize);
            readBytes(buf.data(), subSize);
            cell.name = std::string(buf.data(), strnlen(buf.data(), subSize));
        } else if (memcmp(subType, "DATA", 4) == 0 && subSize >= 1) {
            uint8_t flags;
            readBytes(&flags, 1);
            cell.isInterior = (flags & 0x01) != 0;
            if (subSize > 1) skipBytes(subSize - 1);
        } else if (memcmp(subType, "XCLC", 4) == 0 && subSize >= 8) {
            readBytes(&cell.gridX, 4);
            readBytes(&cell.gridY, 4);
            if (subSize > 8) skipBytes(subSize - 8);
        } else {
            skipBytes(subSize);
        }
        bytesRead += subSize;
    }

    if (!cell.editorID.empty()) {
        m_currentCellEditorID = cell.editorID;
        m_cells[cell.editorID] = cell;
        m_formToEditor[formID] = cell.editorID;
    }

    return true;
}

bool ESMReader::parseREFR(uint32_t dataSize, uint32_t formID) {
    ObjectReference ref;
    ref.formID = formID;
    ref.baseFormID = 0;
    ref.posX = ref.posY = ref.posZ = 0.0f;
    ref.rotX = ref.rotY = ref.rotZ = 0.0f;
    ref.scale = 1.0f;

    uint32_t bytesRead = 0;

    while (bytesRead < dataSize) {
        char subType[4];
        uint16_t subSize;

        if (!readBytes(subType, 4)) return false;
        if (!readBytes(&subSize, 2)) return false;
        bytesRead += 6;

        if (memcmp(subType, "NAME", 4) == 0 && subSize >= 4) {
            readBytes(&ref.baseFormID, 4);
            if (subSize > 4) skipBytes(subSize - 4);
        } else if (memcmp(subType, "DATA", 4) == 0 && subSize >= 24) {
            // Position and rotation
            readBytes(&ref.posX, 4);
            readBytes(&ref.posY, 4);
            readBytes(&ref.posZ, 4);
            readBytes(&ref.rotX, 4);
            readBytes(&ref.rotY, 4);
            readBytes(&ref.rotZ, 4);
            if (subSize > 24) skipBytes(subSize - 24);
        } else if (memcmp(subType, "XSCL", 4) == 0 && subSize >= 4) {
            readBytes(&ref.scale, 4);
            if (subSize > 4) skipBytes(subSize - 4);
        } else {
            skipBytes(subSize);
        }
        bytesRead += subSize;
    }

    // Add to current cell
    if (!m_currentCellEditorID.empty() && ref.baseFormID != 0) {
        auto it = m_cells.find(m_currentCellEditorID);
        if (it != m_cells.end()) {
            it->second.references.push_back(ref);
        }
    }

    return true;
}

const CellData* ESMReader::getCell(const char* editorID) const {
    auto it = m_cells.find(editorID);
    return (it != m_cells.end()) ? &it->second : nullptr;
}

const StaticObject* ESMReader::getStaticObject(uint32_t formID) const {
    auto it = m_statics.find(formID);
    return (it != m_statics.end()) ? &it->second : nullptr;
}

std::vector<std::string> ESMReader::listCells() const {
    std::vector<std::string> result;
    result.reserve(m_cells.size());
    for (const auto& pair : m_cells) {
        result.push_back(pair.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

const std::vector<ObjectReference>& ESMReader::getCellRefs(const char* editorID) const {
    auto it = m_cells.find(editorID);
    return (it != m_cells.end()) ? it->second.references : s_emptyRefs;
}
