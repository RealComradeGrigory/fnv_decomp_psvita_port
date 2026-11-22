/**
 * FNV Vita - Main Entry Point
 *
 * Fallout: New Vegas 3D Data Viewer for PlayStation Vita.
 * Clean-room implementation using VitaSDK and hardware GXM rendering.
 *
 * Game data path: ux0:data/fnv_vita/Data/
 * Required:
 *   - FalloutNV.esm
 *   - Fallout - Meshes.bsa
 *   - Fallout - Textures.bsa
 */

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include <vector>

#include "renderer/gxm_renderer.h"
#include "bsa/bsa_reader.h"
#include "nif/nif_loader.h"
#include "dds/dds_loader.h"
#include "scene/cell_scene.h"
#include "scene/world_scene.h"
#include "nif/packaged_asset.h"
#include "pipboy/pipboy.h"
#include "weapon/weapon_system.h"
#include "audio/audio_system.h"
#include "physics/physics_engine.h"
#include "npc/npc_system.h"
#include "ui/hud.h"
#include "ui/graphics_settings.h"
#include "script/script_engine.h"

// Where the user places their FNV Data/ files on the Vita
static const char* FNV_DATA_PATH = "ux0:data/fnv_vita/Data/";
static const char* FNV_CELLS_PATH = "app0:cells/";

// Application modes
enum class AppMode {
    Cell,      // Phase 2: Interior cell world with freecam
    Showcase   // Phase 1: 3D model showcase with orbit camera
};

struct CellEntry {
    const char* displayName;
    const char* fileName;
};

static const CellEntry CELL_CATALOG[] = {
    { "Doc Mitchell's House",        "GSDocMitchellHouse.cell" },
    { "Prospector Saloon",          "GSProspectorSaloonInterior.cell" },
    { "Goodsprings General Store",  "GSGeneralStore.cell" },
    { "Goodsprings Schoolhouse",    "GSSchoolHouse.cell" },
    { "Goodsprings Gas Station",    "GSGasStation.cell" }
};
static const size_t CELL_COUNT = sizeof(CELL_CATALOG) / sizeof(CELL_CATALOG[0]);

// Debug logging to file
static FILE* g_logFile = nullptr;

void logInit() {
    g_logFile = fopen("ux0:data/fnv_vita/fnv_vita.log", "w");
}

void logMsg(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}

void logShutdown() {
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

/**
 * Check that the FNV data directory exists and contains expected files.
 */
bool validateDataPath() {
    SceUID dir = sceIoDopen(FNV_DATA_PATH);
    if (dir < 0) {
        logMsg("ERROR: Data path not found: %s", FNV_DATA_PATH);
        logMsg("Please copy your Fallout New Vegas 'Data' folder to:");
        logMsg("  ux0:data/fnv_vita/Data/");
        return false;
    }
    sceIoDclose(dir);

    const char* requiredFiles[] = {
        "FalloutNV.esm",
        "Fallout - Meshes.bsa",
        "Fallout - Textures.bsa",
    };

    bool allFound = true;
    for (const char* file : requiredFiles) {
        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s%s", FNV_DATA_PATH, file);

        SceIoStat stat;
        if (sceIoGetstat(fullPath, &stat) < 0) {
            logMsg("WARNING: Missing required file: %s", file);
            allFound = false;
        } else {
            logMsg("Found: %s (%.2f MB)", file,
                   (double)stat.st_size / (1024.0 * 1024.0));
        }
    }

    // Check optional secondary texture archive
    char tex2Path[512];
    snprintf(tex2Path, sizeof(tex2Path), "%s%s", FNV_DATA_PATH, "Fallout - Textures2.bsa");
    SceIoStat stat2;
    if (sceIoGetstat(tex2Path, &stat2) >= 0) {
        logMsg("Found optional: Fallout - Textures2.bsa (%.2f MB)",
               (double)stat2.st_size / (1024.0 * 1024.0));
    } else {
        logMsg("Notice: Fallout - Textures2.bsa not present");
    }

    // Check optional sound archive (Fallout - Sound.bsa)
    char soundPath[512];
    snprintf(soundPath, sizeof(soundPath), "%s%s", FNV_DATA_PATH, "Fallout - Sound.bsa");
    SceIoStat statSound;
    if (sceIoGetstat(soundPath, &statSound) >= 0) {
        logMsg("Found: Fallout - Sound.bsa (%.2f MB)",
               (double)statSound.st_size / (1024.0 * 1024.0));
    } else {
        logMsg("Notice: Fallout - Sound.bsa not present (will use synth audio fallback)");
    }

    return allFound;
}

// ============================================================================
// Model Showcase Catalog
// ============================================================================

struct ModelEntry {
    const char* displayName;
    const char* folder;
    const char* file;
};

static const ModelEntry MODEL_CATALOG[] = {
    { "Deathclaw Warning Sign", "meshes\\clutter\\deathclaw_sign",           "nv_deathclawsign.nif" },
    { "NCR Ranger Helmet",      "meshes\\armor\\headgear\\combatrangerhelmet", "combatrangerhelmet.nif" },
    { "Securitron",             "meshes\\creatures\\nvsecuritron",            "nvsecuritron.nif" },
    { "ED-E / Eyebot Antique",  "meshes\\clutter\\robotsantique",             "eyebot_antique.nif" },
    { "Protectron",             "meshes\\creatures\\protectron",              "protectron.nif" },
    { "Mister Handy",           "meshes\\creatures\\mistergutsy",             "mrhandyrhq.nif" },
    { "Deathclaw Creature",     "meshes\\creatures\\deathclaw",               "deathclaw.nif" },
    { "10mm Pistol",            "meshes\\weapons\\1handpistol",               "10mmpistol.nif" },
    { "Laser Pistol",           "meshes\\weapons\\1handpistol",               "laserpistol.nif" },
    { "Service Rifle",          "meshes\\weapons\\2handautomatic",            "servicerifle.nif" },
    { "Varmint Rifle",          "meshes\\weapons\\2handrifle",                "varmintrifle.nif" },
    { "Hunting Rifle",          "meshes\\weapons\\2handrifle",                "huntingrifle.nif" },
    { "Laser Rifle",            "meshes\\weapons\\2handrifle",                "laserrifle.nif" },
    { "Plasma Rifle",           "meshes\\weapons\\2handrifle",                "plasmarifle.nif" },
    { "Caravan Shotgun",        "meshes\\weapons\\2handrifle",                "caravanshotgun.nif" },
    { "Super Sledge",           "meshes\\weapons\\2handmelee",                "supersledge.nif" },
    { "Mini Nuke",              "meshes\\weapons\\2handlauncher",             "mininuke.nif" },
    { "Pip-Boy 3000",           "meshes\\pipboy3000",                         "pipboyarm.nif" },
    { "Stimpak",                "meshes\\clutter\\health",                    "stimpak01.nif" },
    { "Sunset Sarsaparilla",    "meshes\\clutter\\junk",                      "ssbottle01.nif" },
    { "Nuka-Cola Victory",      "meshes\\clutter\\junk\\nv",                  "nvnukacolavictory.nif" },
    { "Snowglobe (The Strip)",  "meshes\\clutter\\snowglobes",                "snowglobes_thestrip.nif" }
};
static const size_t MODEL_COUNT = sizeof(MODEL_CATALOG) / sizeof(MODEL_CATALOG[0]);

/**
 * Extract a DDS texture from textures BSA(s) handling path normalization.
 */
static bool extractTexture(BSAReader& texBsa1, BSAReader* texBsa2, const std::string& path, std::vector<uint8_t>& outData) {
    if (path.empty()) return false;

    size_t slash = path.find_last_of("/\\");
    std::string folder = (slash != std::string::npos) ? path.substr(0, slash) : "";
    std::string file   = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    auto tryExtract = [&](BSAReader& bsa) -> bool {
        // 1. Try exact folder & filename
        if (bsa.extractFile(folder.c_str(), file.c_str(), outData)) {
            return true;
        }

        // 2. Try prefixing "textures\" if missing
        if (folder.rfind("textures", 0) != 0) {
            std::string withTex = "textures\\" + folder;
            if (bsa.extractFile(withTex.c_str(), file.c_str(), outData)) {
                return true;
            }
        } else {
            // 3. Try stripping leading "textures\"
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

/**
 * Load a model from meshes BSA and its diffuse texture from textures BSA(s).
 */
static bool loadModel(size_t index, BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2,
                      GxmRenderer& renderer, NifMesh& outMesh) {
    if (index >= MODEL_COUNT) return false;

    const ModelEntry& entry = MODEL_CATALOG[index];
    logMsg("==================================================");
    logMsg("Loading Model [%u/%u]: %s", (unsigned int)(index + 1), (unsigned int)MODEL_COUNT, entry.displayName);
    logMsg("NIF path: %s\\%s", entry.folder, entry.file);

    std::vector<uint8_t> nifData;
    if (!meshBsa.extractFile(entry.folder, entry.file, nifData)) {
        logMsg("ERROR: Failed to extract NIF: %s\\%s", entry.folder, entry.file);
        return false;
    }
    logMsg("Extracted NIF data (%u bytes)", (unsigned int)nifData.size());

    NifLoader nifLoader;
    if (!nifLoader.load(nifData.data(), nifData.size())) {
        logMsg("ERROR: Failed to parse NIF: %s", entry.file);
        return false;
    }

    outMesh = nifLoader.getMesh();
    if (outMesh.vertexCount == 0 || outMesh.indices.empty()) {
        logMsg("WARNING: Mesh has no vertices or indices");
        return false;
    }

    // Upload geometry to GXM
    if (!renderer.uploadMesh(outMesh)) {
        logMsg("ERROR: Failed to upload mesh to GXM");
        return false;
    }

    // Load diffuse texture if specified
    if (outMesh.parts.empty() && !outMesh.diffuseTexture.empty()) {
        logMsg("Searching for diffuse texture: %s", outMesh.diffuseTexture.c_str());
        std::vector<uint8_t> ddsData;
        if (extractTexture(texBsa1, texBsa2, outMesh.diffuseTexture, ddsData)) {
            logMsg("Extracted DDS (%u bytes)", (unsigned int)ddsData.size());
            DDSLoader ddsLoader;
            if (ddsLoader.load(ddsData.data(), ddsData.size())) {
                const TextureData& tex = ddsLoader.getTexture();
                renderer.uploadTexture(tex);
                logMsg("Texture uploaded to GXM (%ux%u)", tex.width, tex.height);
            } else {
                logMsg("WARNING: Failed to decompress DDS %s", outMesh.diffuseTexture.c_str());
                renderer.freeTexture();
            }
        } else {
            logMsg("WARNING: Texture %s not found in Textures BSA", outMesh.diffuseTexture.c_str());
            renderer.freeTexture();
        }
    } else {
        logMsg("Mesh has no diffuse texture; using directional vertex shading");
        renderer.freeTexture();
    }

    logMsg("Model loaded: %u vertices, %u triangles, radius=%.1f",
           outMesh.vertexCount, outMesh.triangleCount, outMesh.radius);
    return true;
}

// ============================================================================
// First-Person Player Controller (Phase 3: Custom Physics & Footstep Audio)
// ============================================================================

struct PlayerController {
    // World coordinates & Look angles
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;  // Feet position in world space
    float yaw  = 0.0f;  // Radians
    float pitch = 0.0f; // Radians

    // Physics state
    float velZ = 0.0f;
    bool isGrounded = true;
    bool isNoclip = false; // False = Walking with custom capsule physics; True = 6-DOF flycam

    // Head bobbing & footstep timer
    float bobTimer = 0.0f;
    float currentBob = 0.0f;
    float footstepTimer = 0.0f;

    // Movement tuning (Fallout: New Vegas scale: ~70 units = 1 meter)
    const float WALK_SPEED     = 240.0f; // ~3.4 m/s (jogging)
    const float SPRINT_SPEED   = 420.0f; // ~6.0 m/s (sprint)
    const float FLY_SPEED      = 400.0f; // Noclip fly speed
    const float FAST_FLY_SPEED = 800.0f; // Fast noclip speed
    const float EYE_HEIGHT     = 124.0f; // Standing eye level (~1.77m above feet)

    void reset(float spawnX, float spawnY, float spawnZ, float spawnYaw, float spawnPitch, float floorZ) {
        posX = spawnX;
        posY = spawnY;
        posZ = (floorZ > 0.0f) ? floorZ : (spawnZ - EYE_HEIGHT);
        yaw  = spawnYaw;
        pitch = spawnPitch;
        velZ = 0.0f;
        isGrounded = true;
        bobTimer = 0.0f;
        currentBob = 0.0f;
        footstepTimer = 0.0f;
    }

    void update(const SceCtrlData& pad, uint32_t pressed, float deltaTime,
                PhysicsEngine& physics, AudioSystem& audio, float floorZ,
                float minX, float maxX, float minY, float maxY) {
        // Toggle Walk / Noclip mode via D-Pad Down
        if (pressed & SCE_CTRL_DOWN) {
            isNoclip = !isNoclip;
            if (!isNoclip) {
                if (posZ < floorZ) posZ = floorZ;
                velZ = 0.0f;
            }
            logMsg("Player Mode: %s", isNoclip ? "NOCLIP FLYCAM" : "FIRST-PERSON WALK");
        }

        // Stick axes normalized (-1.0 to 1.0)
        float lx = ((float)pad.lx - 128.0f) / 128.0f;
        float ly = ((float)pad.ly - 128.0f) / 128.0f;
        float rx = ((float)pad.rx - 128.0f) / 128.0f;
        float ry = ((float)pad.ry - 128.0f) / 128.0f;

        // Deadzones
        if (fabsf(lx) < 0.15f) lx = 0.0f;
        if (fabsf(ly) < 0.15f) ly = 0.0f;
        if (fabsf(rx) < 0.15f) rx = 0.0f;
        if (fabsf(ry) < 0.15f) ry = 0.0f;

        // Right stick: Look around (yaw & pitch)
        yaw   += rx * 2.7f * deltaTime;
        pitch -= ry * 2.4f * deltaTime;
        if (pitch > 1.45f)  pitch = 1.45f;
        if (pitch < -1.45f) pitch = -1.45f;

        // View direction vectors on horizontal XY plane
        float fwdX = sinf(yaw);
        float fwdY = cosf(yaw);
        float rightX = cosf(yaw);
        float rightY = -sinf(yaw);

        bool sprint = (pad.buttons & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS)) != 0;

        if (isNoclip) {
            // ========================================================
            // Noclip Flycam Mode
            // ========================================================
            float speed = sprint ? FAST_FLY_SPEED : FLY_SPEED;

            if (fabsf(ly) > 0.0f) {
                posX -= fwdX * ly * speed * deltaTime;
                posY -= fwdY * ly * speed * deltaTime;
            }
            if (fabsf(lx) > 0.0f) {
                posX += rightX * lx * speed * deltaTime;
                posY += rightY * lx * speed * deltaTime;
            }

            if (pad.buttons & SCE_CTRL_LTRIGGER) posZ -= speed * deltaTime;
            if (pad.buttons & SCE_CTRL_RTRIGGER) posZ += speed * deltaTime;

            currentBob = 0.0f;
        } else {
            // ========================================================
            // Walk Mode (Capsule Physics, Wall Sliding & Step Climbing)
            // ========================================================
            float moveSpeed = (pad.buttons & SCE_CTRL_CIRCLE) ? SPRINT_SPEED : WALK_SPEED;
            float moveX = (rightX * lx - fwdX * ly);
            float moveY = (rightY * lx - fwdY * ly);
            float moveLen = sqrtf(moveX * moveX + moveY * moveY);
            if (moveLen > 1.0f) {
                moveX /= moveLen;
                moveY /= moveLen;
            } else if (moveLen <= 0.001f) {
                moveX = 0.0f;
                moveY = 0.0f;
                moveSpeed = 0.0f;
            }

            bool jumpReq = (pressed & SCE_CTRL_CROSS) != 0;

            // Step capsule physics engine
            physics.stepPlayer(posX, posY, posZ, velZ, moveX, moveY, moveSpeed, isGrounded, jumpReq, deltaTime);

            if (moveSpeed > 0.0f && isGrounded) {
                bobTimer += (moveSpeed / WALK_SPEED) * 8.0f * deltaTime;
                currentBob = sinf(bobTimer) * 1.5f;

                footstepTimer += deltaTime;
                float stepInterval = (pad.buttons & SCE_CTRL_CIRCLE) ? 0.32f : 0.48f;
                if (footstepTimer >= stepInterval) {
                    audio.play(SoundID::FootstepWood, 0.45f);
                    footstepTimer = 0.0f;
                }
            } else {
                currentBob *= 0.85f;
                if (fabsf(currentBob) < 0.05f) currentBob = 0.0f;
                footstepTimer = 0.0f;
            }

            // Cell boundary safety check
            if (!physics.hasWorldGeometry()) {
                if (posX < minX + 25.0f) posX = minX + 25.0f;
                if (posX > maxX - 25.0f) posX = maxX - 25.0f;
                if (posY < minY + 25.0f) posY = minY + 25.0f;
                if (posY > maxY - 25.0f) posY = maxY - 25.0f;
            }
        }
    }

    float getCameraZ() const {
        return posZ + EYE_HEIGHT + currentBob;
    }
};

// ============================================================================
// Main Application
// ============================================================================

int main() {
    // Boost CPU/GPU clocks for maximum performance
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    logInit();
    logMsg("==================================================");
    logMsg("=== FNV Vita 00.02 - UI stability repairs 2026-11-22 ===");
    logMsg("=== Clean-room Fallout: New Vegas Vita Viewer  ===");
    logMsg("==================================================");

    // Validate game data
    if (!validateDataPath()) {
        logMsg("Cannot proceed without game data files.");
        logMsg("Copy your Steam FNV 'Data' folder to ux0:data/fnv_vita/Data/");
        logShutdown();
        sceKernelExitProcess(0);
        return 1;
    }

    // Initialize GXM renderer
    GxmRenderer renderer;
    if (!renderer.init()) {
        logMsg("ERROR: Failed to initialize GXM renderer!");
        logShutdown();
        sceKernelExitProcess(0);
        return 1;
    }
    logMsg("GXM renderer initialized (960x544)");

    // Open Meshes BSA
    char meshesBsaPath[512];
    snprintf(meshesBsaPath, sizeof(meshesBsaPath), "%s%s", FNV_DATA_PATH, "Fallout - Meshes.bsa");
    BSAReader meshBsa;
    if (!meshBsa.open(meshesBsaPath)) {
        logMsg("ERROR: Failed to open meshes BSA: %s", meshesBsaPath);
        renderer.shutdown();
        logShutdown();
        sceKernelExitProcess(0);
        return 1;
    }

    // Open Primary Textures BSA
    char texturesBsaPath[512];
    snprintf(texturesBsaPath, sizeof(texturesBsaPath), "%s%s", FNV_DATA_PATH, "Fallout - Textures.bsa");
    BSAReader texBsa;
    if (!texBsa.open(texturesBsaPath)) {
        logMsg("ERROR: Failed to open textures BSA: %s", texturesBsaPath);
        meshBsa.close();
        renderer.shutdown();
        logShutdown();
        sceKernelExitProcess(0);
        return 1;
    }

    // Open Secondary Textures BSA (optional/fallback for weapons, armor, pipboy)
    char textures2BsaPath[512];
    snprintf(textures2BsaPath, sizeof(textures2BsaPath), "%s%s", FNV_DATA_PATH, "Fallout - Textures2.bsa");
    BSAReader texBsa2;
    BSAReader* pTexBsa2 = nullptr;
    if (texBsa2.open(textures2BsaPath)) {
        logMsg("Found and opened secondary texture archive: Fallout - Textures2.bsa");
        pTexBsa2 = &texBsa2;
    } else {
        logMsg("Notice: Fallout - Textures2.bsa not opened (some textures will fallback to untextured)");
    }

    BSAReader updateBsa;char updatePath[512];
    snprintf(updatePath,sizeof(updatePath),"%sUpdate.bsa",FNV_DATA_PATH);
    if(updateBsa.open(updatePath)) {
        meshBsa.setOverrideArchive(&updateBsa);texBsa.setOverrideArchive(&updateBsa);texBsa2.setOverrideArchive(&updateBsa);
        logMsg("Original Update.bsa overrides enabled");
    } else logMsg("Update.bsa missing: updated original buildings may be absent");

    // Open Sound BSA (Fallout - Sound.bsa)
    char soundBsaPath[512];
    snprintf(soundBsaPath, sizeof(soundBsaPath), "%s%s", FNV_DATA_PATH, "Fallout - Sound.bsa");
    BSAReader soundBsa;
    BSAReader* pSoundBsa = nullptr;
    if (soundBsa.open(soundBsaPath)) {
        logMsg("Found and opened sound archive: Fallout - Sound.bsa");
        pSoundBsa = &soundBsa;
    } else {
        logMsg("Notice: Fallout - Sound.bsa not opened (using procedural synthesis audio fallback)");
    }

    // Initialize Audio System (Phase 3: 16-channel 48kHz hardware stereo mixer)
    renderer.setTextureArchives(&texBsa, pTexBsa2);
    AudioSystem audioSystem;
    if (audioSystem.init(pSoundBsa)) {
        logMsg("Audio System initialized (48kHz stereo SceAudio output ready, authentic FNV soundbank loaded)");
    }

    // Initialize Physics Engine (Phase 3: Capsule-vs-AABB, step climbing & wall sliding)
    PhysicsEngine physics;
    physics.init();

    // Initialize NPC System (Phase 3: Doc Mitchell, AI look-at & interactive dialogue)
    NpcSystem npcSystem;
    if (!npcSystem.init(renderer)) {
        logMsg("WARNING: Failed to allocate GPU buffers for NPC Dialogue UI");
    }

    // Initialize HUD System (Phase 3: FNV Compass, HP/AP gauges & interaction reticle)
    HUD hud;
    if (!hud.init(&texBsa, pTexBsa2, renderer)) {
        logMsg("WARNING: Failed to allocate GPU buffers for HUD");
    }

    // Initialize Script Engine (Phase 3: Quest stages & event triggers)
    ScriptEngine scriptEngine;
    scriptEngine.init();

    // Set up application state
    AppMode currentMode = AppMode::Cell;
    size_t currentCellIndex = 0;
    size_t currentModelIndex = 0;

    CellScene cellScene;
    MapDatabase maps;
    bool mapsReady=maps.open(FNV_APP_ROOT "world");
    logMsg("Original world catalog: %s",mapsReady?"loaded":"unavailable");
    WorldScene worldScene;
    GraphicsSettings graphics;
    const char* graphicsPath="ux0:data/fnv_vita/graphics.cfg";
    bool graphicsSaved=graphics.load(graphicsPath);
    if(!graphicsSaved)graphicsSaved=graphics.save(graphicsPath);
    worldScene.setGraphics(graphics);renderer.setParallelPreparation(graphics.parallelCpu!=0);
    GraphicsMenu graphicsMenu;graphicsMenu.init(renderer,&texBsa,pTexBsa2);
    float measuredFps=0;uint32_t fpsStart=0,fpsFrames=0;
    bool exterior=false;
    auto loadCatalogInterior=[&](size_t idx) {
        std::string editor=CELL_CATALOG[idx].fileName;
        if(editor.size()>5)editor.resize(editor.size()-5);
        uint32_t id=maps.interiorID(editor.c_str());
        if(id)return cellScene.load(maps.interiorPath(id).c_str(),meshBsa,texBsa,pTexBsa2,renderer);
        std::string path=std::string(FNV_CELLS_PATH)+CELL_CATALOG[idx].fileName;
        return cellScene.load(path.c_str(),meshBsa,texBsa,pTexBsa2,renderer);
    };
    NifMesh currentMesh;
    bool hasMesh = false;

    // Player state (Phase 2 & 3)
    PlayerController player;
    float spawnX = 1298.0f, spawnY = 1850.0f, spawnZ = 7420.0f;
    float spawnYaw = 0.0f, spawnPitch = 0.0f;

    // Helper to configure physics and NPCs when a cell is loaded
    auto setupCell = [&](size_t idx) {
        worldScene.cleanup(renderer);exterior=false;
        float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
        cellScene.getBounds(minX, maxX, minY, maxY);
        physics.buildCollidersFromCell(cellScene.getFloorZ(), minX, maxX, minY, maxY);
        cellScene.buildCollision(physics);
        cellScene.getSpawnPoint(spawnX, spawnY, spawnZ, spawnYaw, spawnPitch);
        player.reset(spawnX, spawnY, spawnZ, spawnYaw, spawnPitch, cellScene.getFloorZ());
        float standingX = spawnX, standingY = spawnY, standingZ = spawnZ;
        if (physics.findStandingPosition(standingX, standingY, standingZ)) {
            player.posX = standingX; player.posY = standingY; player.posZ = standingZ;
        } else {
            player.isNoclip = true;
            logMsg("No clear standing position near spawn; starting in noclip");
        }

        npcSystem.cleanup(renderer);
        npcSystem.init(renderer);
        if (idx == 0) { // Doc Mitchell's House
            npcSystem.spawnDocMitchell(meshBsa, texBsa, pTexBsa2, renderer);
            hud.showNotification("AIN'T THAT A KICK IN THE HEAD", "Wake up in Doc Mitchell's House");
            scriptEngine.onCellEnter(CELL_CATALOG[idx].fileName);
            audioSystem.play(SoundID::QuestNew, 0.9f);
        } else {
            hud.showNotification("LOCATION DISCOVERED", CELL_CATALOG[idx].displayName);
            audioSystem.play(SoundID::QuestNew, 0.8f);
        }
    };

    // Load initial interior cell (Doc Mitchell's House)
    char cellPath[512];
    snprintf(cellPath, sizeof(cellPath), "%s%s", FNV_CELLS_PATH, CELL_CATALOG[currentCellIndex].fileName);
    bool hasCell = loadCatalogInterior(currentCellIndex);

    if (hasCell) {
        setupCell(currentCellIndex);
    } else {
        logMsg("Failed to load cell '%s'; falling back to 3D Model Showcase", cellPath);
        currentMode = AppMode::Showcase;
        hasMesh = loadModel(currentModelIndex, meshBsa, texBsa, pTexBsa2, renderer, currentMesh);
    }

    // Initialize Pip-Boy 3000 Interface (Phase 3)
    PipBoy pipboy;
    if (!pipboy.init(renderer, &texBsa, pTexBsa2)) {
        logMsg("WARNING: Failed to allocate dynamic GPU buffers for Pip-Boy 3000");
    } else {
        logMsg("Pip-Boy 3000 initialized successfully (Amber CRT HUD and authentic textures ready)");
    }

    // Initialize Weapon System (Viewmodel, ADS, Firing, Impact Sparks & Reticle)
    WeaponSystem weaponSystem;
    weaponSystem.setPhysics(&physics);
    if (!weaponSystem.init(renderer)) {
        logMsg("WARNING: Failed to allocate dynamic GPU buffers for Weapon System");
    } else {
        logMsg("Weapon System initialized successfully");
        weaponSystem.initHands(meshBsa, texBsa, pTexBsa2, renderer);
        if (weaponSystem.loadWeapon(WeaponID::Pistol10mm, meshBsa, texBsa, pTexBsa2, renderer)) {
            logMsg("10mm Pistol 3D viewmodel loaded and ready");
        }
    }

    // Set up controls
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    // Orbit Camera state (Phase 1)
    float camDist = hasMesh ? (currentMesh.radius * 2.2f) : 100.0f;
    float camPitch = 20.0f;  // 20 deg pitch up
    float camYaw   = 0.0f;   // yaw
    float targetX  = hasMesh ? currentMesh.center.x : 0.0f;
    float targetY  = hasMesh ? currentMesh.center.y : 0.0f;
    float targetZ  = hasMesh ? currentMesh.center.z : 0.0f;

    uint32_t lastButtons = 0;
    uint32_t blockedGameplayButtons = 0;
    bool running = true;

    logMsg("==================================================");
    logMsg("FNV Vita stability build ready");
    logMsg("Controls in Interior Cell Mode:");
    logMsg("  First-Person Movement & Combat:");
    logMsg("    Left Stick:     Walk / Strafe (Capsule Physics, Step Climbing)");
    logMsg("    Right Stick:    Look Yaw / Pitch");
    logMsg("    R-Trigger:      FIRE WEAPON (Muzzle Flash, Authentic Audio & Impact Sparks)");
    logMsg("    L-Trigger:      AIM DOWN SIGHTS (Iron Sights Alignment, FOV Zoom)");
    logMsg("    Cross:          Jump / Talk to Doc Mitchell / Interact");
    logMsg("    Square:         RELOAD WEAPON");
    logMsg("    D-Pad Up:       QUICK SWITCH WEAPON (10mm -> Service Rifle -> Laser -> Shotgun)");
    logMsg("    Circle (Hold):  Sprint");
    logMsg("    D-Pad Down:     Toggle Walk Mode / Noclip Flycam");
    logMsg("    D-Pad Right/Left: Switch Interior Cell");
    logMsg("  Pip-Boy 3000 Interface:");
    logMsg("    Triangle (Tap): Toggle Pip-Boy Screen Open / Closed");
    logMsg("    Triangle (Hold):Toggle Flashlight On / Off");
    logMsg("    L / R Triggers: Switch Main Tabs (STAT <-> ITEMS <-> DATA)");
    logMsg("    D-Pad L / R:    Switch Sub-Tabs");
    logMsg("    D-Pad U / D:    Navigate Inventory / Radio Lists");
    logMsg("    Cross:          Equip Weapon/Armor, Use Stimpak, Tune Radio");
    logMsg("    Circle:         Close Pip-Boy");
    logMsg("  System:");
    logMsg("    SELECT:         Toggle Interior Cell / Model Showcase");
    logMsg("    START:          Pause / graphics settings (Exit is in the menu)");
    logMsg("==================================================");

    uint32_t lastTime = sceKernelGetProcessTimeLow();
    uint32_t perfStart=lastTime,perfFrames=0,perfWorst=0;
    uint64_t perfPresent=0,perfState=0,perfUniform=0,perfWvp=0,perfTriangleTests=0,perfGroupInside=0,perfGroupOutside=0,perfUpdate=0,perfPrepare=0,perfSubmit=0,perfJobs[3]{},perfWait=0,perfScene=0,perfWeapon=0,perfPose=0,perfViewmodel=0,perfUi=0,perfDraws=0,perfTriangles=0,perfCulled=0;

    // Main render loop
    while (running) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t pressed = pad.buttons & ~lastButtons;
        lastButtons = pad.buttons;

        uint32_t nowTime = sceKernelGetProcessTimeLow();
        float deltaTime = (float)(nowTime - lastTime) / 1000000.0f;
        if (deltaTime <= 0.0f || deltaTime > 0.1f) deltaTime = 1.0f / 60.0f;
        lastTime = nowTime;

        bool menuWasOpen=graphicsMenu.isOpen();
        if (pressed & SCE_CTRL_START) {
            graphicsMenu.toggle();
        }
        if(menuWasOpen || graphicsMenu.isOpen()) {
            bool quit=false;
            if(graphicsMenu.update(pressed & ~SCE_CTRL_START,graphics,quit)) {
                worldScene.setGraphics(graphics);renderer.setParallelPreparation(graphics.parallelCpu!=0);graphicsSaved=graphics.save(graphicsPath);
                logMsg("GRAPHICS distance=%.0f props=%.0f plants=%.0f lod=%d distant=%d fog=%d cpu_workers=%d saved=%d",graphics.viewDistance(),graphics.propDistance(),graphics.plantDistance(),graphics.lod,graphics.distantObjects,graphics.fog,graphics.parallelCpu,graphicsSaved);
            }
            if(quit){running=false;continue;}
            blockedGameplayButtons|=pad.buttons;
            if(graphicsMenu.isOpen()) {
                renderer.beginFrame();renderer.clear(.02f,.015f,.01f,1);graphicsMenu.render(renderer,graphics,measuredFps,graphicsSaved);renderer.endFrame();
            }
            perfStart=nowTime;perfFrames=perfWorst=0;perfWait=perfScene=perfWeapon=perfPose=perfViewmodel=perfUi=perfDraws=perfTriangles=perfCulled=0;perfPresent=perfState=perfUniform=perfWvp=perfTriangleTests=perfGroupInside=perfGroupOutside=0;perfUpdate=perfPrepare=perfSubmit=0;for(auto& jobs:perfJobs)jobs=0;
            fpsStart=0;fpsFrames=0;continue;
        }

        // SELECT = Switch Mode (Cell <-> Showcase)
        if (pressed & SCE_CTRL_SELECT) {
            perfStart=nowTime;perfFrames=perfWorst=0;perfWait=perfScene=perfWeapon=perfPose=perfViewmodel=perfUi=perfDraws=perfTriangles=perfCulled=0;perfPresent=perfState=perfUniform=perfWvp=perfTriangleTests=perfGroupInside=perfGroupOutside=0;perfUpdate=perfPrepare=perfSubmit=0;for(auto& jobs:perfJobs)jobs=0;
            if (currentMode == AppMode::Cell) {
                currentMode = AppMode::Showcase;
                logMsg(">>> SWITCHED TO: 3D Model Showcase Mode <<<");
                if (!hasMesh) {
                    hasMesh = loadModel(currentModelIndex, meshBsa, texBsa, pTexBsa2, renderer, currentMesh);
                    if (hasMesh) {
                        camDist  = currentMesh.radius * 2.2f;
                        camPitch = 20.0f;
                        camYaw   = 0.0f;
                        targetX  = currentMesh.center.x;
                        targetY  = currentMesh.center.y;
                        targetZ  = currentMesh.center.z;
                    }
                }
            } else {
                currentMode = AppMode::Cell;
                logMsg(">>> SWITCHED TO: Interior Cell World Mode <<<");
                if (!exterior && cellScene.getObjectCount() == 0) {
                    snprintf(cellPath, sizeof(cellPath), "%s%s", FNV_CELLS_PATH, CELL_CATALOG[currentCellIndex].fileName);
                    hasCell = loadCatalogInterior(currentCellIndex);
                    if (hasCell) {
                        setupCell(currentCellIndex);
                    }
                }
            }
        }

        // Pip-Boy 3000 Interface Update (with authentic UI sound effects)
        bool pipboyWasOpen = pipboy.isOpen();
        const char* currentCellDisplayName = (currentMode == AppMode::Cell)
            ? (exterior?worldScene.name().c_str():cellScene.getCellName().c_str())
            : MODEL_CATALOG[currentModelIndex].displayName;
        pipboy.update(pad, pressed, deltaTime, currentCellDisplayName,
                      player.posX, player.posY, player.getCameraZ(), player.yaw, &audioSystem);
        if (pipboyWasOpen != pipboy.isOpen()) logMsg("Pip-Boy: %s", pipboy.isOpen() ? "opened" : "closed");
        blockedGameplayButtons &= pad.buttons;
        if (pipboyWasOpen || pipboy.isOpen() || npcSystem.isInDialogue() || (pressed & SCE_CTRL_SELECT)) {
            blockedGameplayButtons |= pad.buttons;
        }
        SceCtrlData gameplayPad = pad;
        gameplayPad.buttons &= ~blockedGameplayButtons;
        uint32_t gameplayPressed = pressed & ~blockedGameplayButtons;

        if (currentMode == AppMode::Cell) {
            // ========================================================
            // Cell Mode Controls (First-Person Player Movement)
            // ========================================================

            if (!pipboy.isOpen()) {
                // Target interaction prompt
                const char* targetPrompt = nullptr;
                if (npcSystem.hasNearbyNpc() && !npcSystem.isInDialogue()) {
                    targetPrompt = "[X] Talk to Doc Mitchell";
                }

                // Sync weapon if changed in Pip-Boy
                int equippedWep = pipboy.getEquippedWeaponIndex();
                if (equippedWep != (int)weaponSystem.getCurrentWeaponID()) {
                    weaponSystem.setWeaponByIndex(equippedWep, meshBsa, texBsa, pTexBsa2, renderer);
                    pipboy.setEquippedWeaponIndex((int)weaponSystem.getCurrentWeaponID());
                }

                // D-Pad Up: Quick cycle equipped weapon
                if ((gameplayPressed & SCE_CTRL_UP) && !npcSystem.isInDialogue()) {
                    int nextWep = ((int)weaponSystem.getCurrentWeaponID() + 1) % 5;
                    weaponSystem.setWeaponByIndex(nextWep, meshBsa, texBsa, pTexBsa2, renderer);
                    pipboy.setEquippedWeaponIndex((int)weaponSystem.getCurrentWeaponID());
                    audioSystem.play(SoundID::PipBoySelect, 0.7f);
                }

                // D-Pad: Switch between cells (only when Pip-Boy is closed and not in dialogue)
                if ((gameplayPressed & (SCE_CTRL_RIGHT | SCE_CTRL_LEFT)) && !npcSystem.isInDialogue()) {
                    size_t nextCellIndex = (currentCellIndex + ((gameplayPressed & SCE_CTRL_RIGHT) ? 1 : CELL_COUNT - 1)) % CELL_COUNT;
                    logMsg("Loading Cell [%u/%u]: %s",
                           (unsigned int)(nextCellIndex + 1), (unsigned int)CELL_COUNT,
                           CELL_CATALOG[nextCellIndex].displayName);
                    snprintf(cellPath, sizeof(cellPath), "%s%s", FNV_CELLS_PATH, CELL_CATALOG[nextCellIndex].fileName);
                    if (loadCatalogInterior(nextCellIndex)) {
                        currentCellIndex = nextCellIndex;
                        hasCell = true;
                        setupCell(currentCellIndex);
                    } else {
                        logMsg("Cell load failed; keeping the current cell");
                        hud.showNotification("CELL LOAD FAILED", "Previous cell retained");
                    }
                }

                // Follow original REFR XTEL door destinations, including the
                // actual exit from Doc's house into the Mojave worldspace.
                std::string doorPrompt;
                const MapDoor* nearDoor=exterior?worldScene.nearbyDoor(player.posX,player.posY,player.posZ+60):cellScene.nearbyDoor(player.posX,player.posY,player.posZ+60);
                bool usedDoor=false;
                if(nearDoor && !npcSystem.isInDialogue() && mapsReady) {
                    const auto* world=maps.world(nearDoor->targetWorld);
                    const auto* interiorName=maps.interiorName(nearDoor->targetCell);
                    doorPrompt="[X] Open ";doorPrompt+=world?world->name:(interiorName?*interiorName:"Door");targetPrompt=doorPrompt.c_str();
                    if(gameplayPressed&SCE_CTRL_CROSS) {
                        MapDoor destination=*nearDoor;bool loaded=false;
                        if(destination.targetWorld) {
                            uint32_t previousWorld=worldScene.worldID();
                            loaded=worldScene.load(maps,destination.targetWorld,destination.target[0],destination.target[1],meshBsa,renderer,physics);
                            if(loaded) {cellScene.cleanup(renderer);exterior=true;}
                            else if(previousWorld)worldScene.load(maps,previousWorld,player.posX,player.posY,meshBsa,renderer,physics);
                        } else {
                            renderer.setMaterialTextureLimit(512);
                            loaded=cellScene.load(maps.interiorPath(destination.targetCell).c_str(),meshBsa,texBsa,pTexBsa2,renderer);
                            if(loaded) {
                                worldScene.cleanup(renderer);exterior=false;
                                float minX,maxX,minY,maxY;cellScene.getBounds(minX,maxX,minY,maxY);
                                physics.buildCollidersFromCell(cellScene.getFloorZ(),minX,maxX,minY,maxY);cellScene.buildCollision(physics);
                            }
                        }
                        if(loaded) {
                            hasCell=true;npcSystem.cleanup(renderer);npcSystem.init(renderer);
                            if(!exterior && destination.targetCell==0x00103DF9)npcSystem.spawnDocMitchell(meshBsa,texBsa,pTexBsa2,renderer);
                            player.reset(destination.target[0],destination.target[1],destination.target[2],destination.yaw,0,destination.target[2]);
                            player.posZ=destination.target[2];player.isNoclip=false;
                            if(!physics.findStandingPosition(player.posX,player.posY,player.posZ)) {
                                player.isNoclip=true;logMsg("Door destination has no verified standing surface; noclip enabled");
                            }
                            logMsg("Original door %08X -> world %08X cell %08X at %.1f %.1f %.1f",destination.sourceRef,destination.targetWorld,destination.targetCell,player.posX,player.posY,player.posZ);
                        } else hud.showNotification("LOAD FAILED","Previous location retained");
                        gameplayPressed&=~SCE_CTRL_CROSS;gameplayPad.buttons&=~SCE_CTRL_CROSS;usedDoor=true;
                    }
                }

                // Update NPC system (handles AI look-at, proximity, and dialogue input)
                bool wasInDialogue = npcSystem.isInDialogue();
                npcSystem.update(deltaTime, player.posX, player.posY, player.getCameraZ(),
                                 pad.buttons, usedDoor?(pressed&~SCE_CTRL_CROSS):pressed, audioSystem);

                // Dialogue display alone does not execute original quest conditions/scripts.

                // If NOT in dialogue, allow player movement and weapon actions
                if (!npcSystem.isInDialogue() && !wasInDialogue) {
                    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
                    if(exterior) {minX=minY=-1000000.f;maxX=maxY=1000000.f;worldScene.update(player.posX,player.posY,renderer,physics);}
                    else cellScene.getBounds(minX, maxX, minY, maxY);

                    // Update player movement and physics
                    player.update(gameplayPad, gameplayPressed, deltaTime, physics, audioSystem,
                                  exterior?player.posZ:cellScene.getFloorZ(), minX, maxX, minY, maxY);
                    if(exterior && !player.isNoclip)worldScene.constrainToLoaded(player.posX,player.posY);

                    // Update weapon system (ADS, firing, reload, recoil)
                    float lx = ((float)pad.lx - 128.0f) / 128.0f;
                    float ly = ((float)pad.ly - 128.0f) / 128.0f;
                    bool isMoving = (fabsf(lx) > 0.15f || fabsf(ly) > 0.15f);
                    bool isSprinting = isMoving && ((pad.buttons & SCE_CTRL_CIRCLE) != 0);

                    weaponSystem.update(gameplayPad, gameplayPressed, deltaTime,
                                        player.posX, player.posY, player.getCameraZ(), player.pitch, player.yaw,
                                        isMoving, isSprinting, &audioSystem);
                }

                // Update HUD (compass yaw, HP/AP, interaction prompt)
                hud.update(deltaTime, player.yaw, pipboy.getPlayerHp(), pipboy.getMaxHp(), 85, 85, targetPrompt);
            }

            // Render frame
            uint32_t waitStart=sceKernelGetProcessTimeLow();
            renderer.beginFrame();
            uint32_t waited=sceKernelGetProcessTimeLow()-waitStart;
            if(exterior && maps.world(worldScene.worldID())) {
                const auto* w=maps.world(worldScene.worldID());renderer.clear(w->sky[0],w->sky[1],w->sky[2],1);
            } else if (weaponSystem.hasMuzzleFlash()) {
                renderer.clear(0.35f, 0.30f, 0.18f, 1.0f);  // Bright gunshot muzzle flash illumination!
            } else if (pipboy.isFlashlightOn()) {
                renderer.clear(0.24f, 0.21f, 0.14f, 1.0f);  // Warm halogen amber Pip-Boy flashlight
            } else {
                renderer.clear(0.08f, 0.08f, 0.10f, 1.0f);  // Atmospheric dark interior
            }

            uint32_t sceneStart=sceKernelGetProcessTimeLow();
            if (hasCell) {
                float fov = weaponSystem.getAdsFov();
                renderer.setCameraFreecam(player.posX, player.posY, player.getCameraZ(), player.pitch, player.yaw, fov,exterior?graphics.viewDistance():15000.f);
                if(exterior)worldScene.render(renderer);else cellScene.render(renderer);
                npcSystem.render(renderer);
                if(exterior && graphics.fog && maps.world(worldScene.worldID()))renderer.distanceFog(graphics.viewDistance()*.5f,graphics.viewDistance()*.98f,maps.world(worldScene.worldID())->sky);
            }

            // Render 3D bullet impact sparks in world space
            weaponSystem.renderWorldEffects(renderer);
            uint32_t sceneTime=sceKernelGetProcessTimeLow()-sceneStart;

            // Render First-Person 3D Weapon Viewmodel, HUD & Dialogue
            uint32_t weaponStart=sceKernelGetProcessTimeLow();
            uint32_t viewmodelTime=0,poseTime=0;
            if (!pipboy.isOpen()) {
                weaponSystem.renderViewmodel(renderer, player.posX, player.posY, player.getCameraZ(), player.pitch, player.yaw);
                viewmodelTime=sceKernelGetProcessTimeLow()-weaponStart;poseTime=weaponSystem.poseCpuUs();
                weaponSystem.renderHud(renderer);
                hud.render(renderer);
                if (npcSystem.isInDialogue()) {
                    npcSystem.renderDialogue(renderer);
                }
            }

            // Draw Pip-Boy 3000 CRT HUD
            if (pipboy.isOpen()) {
                pipboy.render(renderer);
            }

            if(graphics.showFps)graphicsMenu.fps(renderer,measuredFps,graphics.showFps);
            uint32_t weaponTime=sceKernelGetProcessTimeLow()-weaponStart;

            renderer.endFrame();
            const auto& stats=renderer.frameStats();
            graphicsMenu.setTimings((waitStart-nowTime)*.001f,sceneTime*.001f,weaponTime*.001f,waited*.001f,stats.presentUs*.001f,poseTime*.001f,(viewmodelTime-std::min(viewmodelTime,poseTime))*.001f,(weaponTime-viewmodelTime)*.001f);
            ++perfFrames;perfPresent+=stats.presentUs;perfState+=stats.stateCalls;perfUniform+=stats.uniformWrites;perfWvp+=stats.wvpBuilds;perfTriangleTests+=stats.triangleTests;perfGroupInside+=stats.clusterInside;perfGroupOutside+=stats.clusterOutside;perfUpdate+=waitStart-nowTime;perfPrepare+=stats.prepareUs;perfSubmit+=stats.submitUs;for(unsigned lane=0;lane<3;++lane)perfJobs[lane]+=stats.workerJobs[lane];perfWait+=waited;perfScene+=sceneTime;perfWeapon+=weaponTime;perfPose+=poseTime;perfViewmodel+=viewmodelTime-std::min(viewmodelTime,poseTime);perfUi+=weaponTime-viewmodelTime;
            perfDraws+=stats.draws;perfTriangles+=stats.triangles;perfCulled+=stats.culled;
            uint32_t perfNow=sceKernelGetProcessTimeLow();
            if(!fpsStart)fpsStart=nowTime;
            ++fpsFrames;
            if(perfNow-fpsStart>=500000u){measuredFps=fpsFrames*1000000.f/(perfNow-fpsStart);fpsStart=perfNow;fpsFrames=0;}
            perfWorst=std::max(perfWorst,perfNow-nowTime);
            if(perfNow-perfStart>=5000000u) {
                float frames=(float)perfFrames;
                logMsg("PERF cell=%s pos=(%.1f,%.1f,%.1f) yaw=%.2f pitch=%.2f fps=%.1f worst_work_ms=%.1f avg_wait_ms=%.1f scene_cpu_ms=%.1f weapon_ui_cpu_ms=%.1f draws=%.0f triangles=%.0f culled=%.0f",
                    exterior?worldScene.name().c_str():cellScene.getCellName().c_str(),player.posX,player.posY,player.getCameraZ(),player.yaw,player.pitch,
                    frames*1000000.f/(perfNow-perfStart),perfWorst*.001f,perfWait*.001f/frames,perfScene*.001f/frames,perfWeapon*.001f/frames,
                    perfDraws/frames,perfTriangles/frames,perfCulled/frames);
                logMsg("VIEWMODEL pose_ms=%.2f draw_ms=%.2f hud_ui_ms=%.2f streaming=%d",perfPose*.001f/frames,perfViewmodel*.001f/frames,perfUi*.001f/frames,exterior && worldScene.streaming());
                logMsg("CPU update_ms=%.2f prepare_ms=%.2f submit_ms=%.2f jobs_main=%.0f jobs_core1=%.0f jobs_core2=%.0f workers=%u enabled=%d scratch_mib=%.2f",
                    perfUpdate*.001f/frames,perfPrepare*.001f/frames,perfSubmit*.001f/frames,
                    perfJobs[0]/frames,perfJobs[1]/frames,perfJobs[2]/frames,renderer.preparationWorkers(),graphics.parallelCpu,renderer.preparationScratchBytes()/1048576.);
                logMsg("RENDER present_ms=%.2f state_calls=%.0f uniform_writes=%.0f wvp_builds=%.0f triangle_tests=%.0f group_inside_triangles=%.0f group_outside_triangles=%.0f",
                    perfPresent*.001f/frames,perfState/frames,perfUniform/frames,perfWvp/frames,perfTriangleTests/frames,perfGroupInside/frames,perfGroupOutside/frames);
                perfStart=perfNow;perfFrames=perfWorst=0;perfWait=perfScene=perfWeapon=perfPose=perfViewmodel=perfUi=perfDraws=perfTriangles=perfCulled=0;perfPresent=perfState=perfUniform=perfWvp=perfTriangleTests=perfGroupInside=perfGroupOutside=0;perfUpdate=perfPrepare=perfSubmit=0;for(auto& jobs:perfJobs)jobs=0;
            }

        } else {
            // ========================================================
            // Showcase Mode Controls (Orbit Camera)
            // ========================================================

            if (!pipboy.isOpen()) {
                // D-Pad Right: Next Model
                if (pressed & SCE_CTRL_RIGHT) {
                    currentModelIndex = (currentModelIndex + 1) % MODEL_COUNT;
                    if (loadModel(currentModelIndex, meshBsa, texBsa, pTexBsa2, renderer, currentMesh)) {
                        hasMesh = true;
                        camDist  = currentMesh.radius * 2.2f;
                        camPitch = 20.0f;
                        camYaw   = 0.0f;
                        targetX  = currentMesh.center.x;
                        targetY  = currentMesh.center.y;
                        targetZ  = currentMesh.center.z;
                    }
                }

                // D-Pad Left: Previous Model
                if (pressed & SCE_CTRL_LEFT) {
                    currentModelIndex = (currentModelIndex + MODEL_COUNT - 1) % MODEL_COUNT;
                    if (loadModel(currentModelIndex, meshBsa, texBsa, pTexBsa2, renderer, currentMesh)) {
                        hasMesh = true;
                        camDist  = currentMesh.radius * 2.2f;
                        camPitch = 20.0f;
                        camYaw   = 0.0f;
                        targetX  = currentMesh.center.x;
                        targetY  = currentMesh.center.y;
                        targetZ  = currentMesh.center.z;
                    }
                }

                // Square: Reset Camera
                if (pressed & SCE_CTRL_SQUARE) {
                    if (hasMesh) {
                        camDist  = currentMesh.radius * 2.2f;
                        camPitch = 20.0f;
                        camYaw   = 0.0f;
                        targetX  = currentMesh.center.x;
                        targetY  = currentMesh.center.y;
                        targetZ  = currentMesh.center.z;
                    }
                }

                // Analog camera controls (Orbit)
                float rx = ((float)pad.rx - 128.0f) / 128.0f;
                float ry = ((float)pad.ry - 128.0f) / 128.0f;
                float lx = ((float)pad.lx - 128.0f) / 128.0f;
                float ly = ((float)pad.ly - 128.0f) / 128.0f;

                float rotX = (fabsf(rx) > 0.15f) ? rx : ((fabsf(lx) > 0.15f) ? lx : 0.0f);
                float rotY = (fabsf(ry) > 0.15f) ? ry : ((fabsf(ly) > 0.15f) ? ly : 0.0f);

                if (fabsf(rotX) > 0.0f) camYaw += rotX * 2.2f;
                if (fabsf(rotY) > 0.0f) {
                    camPitch += rotY * 2.2f;
                    if (camPitch > 85.0f)  camPitch = 85.0f;
                    if (camPitch < -85.0f) camPitch = -85.0f;
                }

                // L/R Triggers: Zoom
                if (pad.buttons & SCE_CTRL_LTRIGGER) camDist += (camDist * 0.025f + 1.0f);
                if (pad.buttons & SCE_CTRL_RTRIGGER) camDist -= (camDist * 0.025f + 1.0f);
                if (camDist < 2.0f) camDist = 2.0f;
                if (camDist > 5000.0f) camDist = 5000.0f;
            }

            // Render frame
            renderer.beginFrame();
            renderer.clear(0.10f, 0.18f, 0.28f, 1.0f);  // Vault-Tec slate blue

            if (hasMesh) {
                renderer.setCamera(camDist, camPitch, camYaw, targetX, targetY, targetZ);
                renderer.drawMesh();
            }

            // Draw Pip-Boy 3000 CRT HUD if opened
            if (pipboy.isOpen()) {
                pipboy.render(renderer);
            }

            renderer.endFrame();
        }
    }

    logMsg("Exiting application, cleaning up resources...");
    graphicsMenu.cleanup(renderer);
    hud.cleanup(renderer);
    npcSystem.cleanup(renderer);
    weaponSystem.cleanup(renderer);
    pipboy.cleanup(renderer);
    cellScene.cleanup(renderer);
    worldScene.cleanup(renderer);
    audioSystem.shutdown();
    if (soundBsa.isOpen()) soundBsa.close();
    if (texBsa2.isOpen()) texBsa2.close();
    texBsa.close();
    meshBsa.close();
    renderer.shutdown();
    logMsg("FNV Vita shutdown complete.");
    logShutdown();

    sceKernelExitProcess(0);
    return 0;
}
