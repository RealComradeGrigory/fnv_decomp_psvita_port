#include "weapon_system.h"
#include "nif/packaged_asset.h"
#include "physics/physics_engine.h"
#include "pipboy/pipboy_font.h"
#include "nif/nif_loader.h"
#include "dds/dds_loader.h"
#include "audio/audio_system.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#ifdef FNV_VITA_TARGET
#include <psp2/kernel/processmgr.h>
#else
#include <chrono>
#endif
static uint32_t poseClockUs() {
#ifdef FNV_VITA_TARGET
    return sceKernelGetProcessTimeLow();
#else
    return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

extern void logMsg(const char* fmt, ...);

// Colors
static const float COLOR_HUD_AMBER[4]    = { 0.88f, 0.62f, 0.12f, 1.0f };
static const float COLOR_HUD_AMBER_DIM[4]= { 0.50f, 0.32f, 0.06f, 0.85f };
static const float COLOR_HUD_AMBER_BG[4] = { 0.08f, 0.05f, 0.01f, 0.80f };
static const float COLOR_LASER_RED[4]    = { 1.00f, 0.20f, 0.10f, 1.0f };
static const float COLOR_SPARK_GOLD[4]   = { 1.00f, 0.85f, 0.30f, 1.0f };

// Authentic Weapon Presets
static const WeaponConfig WEAPON_CONFIGS[] = {
    // 0: 10mm Pistol
    {
        WeaponID::Pistol10mm,
        "10mm Pistol",
        "meshes\\weapons\\1handpistol",
        "10mmpistol.nif",
        "textures\\weapons\\1handpistol\\10mmpistol.dds",
        12,   // Mag capacity
        22,   // Damage
        3.8f, // Fire rate (shots/s)
        1.4f, // Reload time
        2.8f, // Recoil kick
        0.045f, // Recoil pitch
        48.0f,  // ADS FOV
        10.5f, 4.0f, -3.8f,  // Hip: Fwd, Right, Up
        7.5f, 0.0f, -7.97f,  // ADS: Fwd, Right, Up (Iron sights aligned with reticle)
        1.0f,  // Scale
        15.0f, // Barrel length
        0,     // Ballistic
        140,   // Default reserve
        WeaponAxisType::Standard_XFwd_YUp,
        -1.5f, -2.2f, 0.0f,  // Right Hand grip offset
        -1.5f, -2.5f, -0.6f, // Left Hand grip offset
        true
    },
    // 1: Service Rifle
    {
        WeaponID::ServiceRifle,
        "Service Rifle",
        "meshes\\weapons\\2handautomatic",
        "servicerifle.nif",
        "textures\\weapons\\2handautomatic\\servicerifle.dds",
        20,
        18,
        5.2f,
        1.8f,
        2.2f,
        0.038f,
        42.0f,
        9.0f, 3.8f, -3.8f,
        6.5f, 0.0f, -4.80f,
        0.95f,
        25.0f,
        0,
        85,
        WeaponAxisType::ServiceRifle_XFwd_ZUp,
        -2.5f, 0.0f, -4.0f,
        14.0f, 0.0f, -3.2f,
        true
    },
    // 2: Laser Pistol
    {
        WeaponID::LaserPistol,
        "Laser Pistol",
        "meshes\\weapons\\1handpistol",
        "laserpistol.nif",
        "textures\\weapons\\1handpistol\\laserpistol.dds",
        30,
        12,
        3.5f,
        1.5f,
        1.5f,
        0.025f,
        48.0f,
        10.5f, 4.0f, -3.8f,
        7.5f, 0.0f, -7.50f,
        1.0f,
        15.0f,
        1, // Laser energy
        65,
        WeaponAxisType::Standard_XFwd_YUp,
        -1.5f, -2.2f, 0.0f,
        -1.5f, -2.5f, -0.6f,
        true
    },
    // 3: Caravan Shotgun
    {
        WeaponID::CaravanShotgun,
        "Caravan Shotgun",
        "meshes\\weapons\\2handrifle",
        "caravanshotgun.nif",
        "textures\\weapons\\2handrifle\\caravanshotgun.dds",
        2,
        45,
        2.0f,
        2.0f,
        4.8f,
        0.080f,
        50.0f,
        9.5f, 3.8f, -3.8f,
        6.5f, 0.0f, -8.31f,
        0.95f,
        20.0f,
        2, // Heavy shotgun
        32,
        WeaponAxisType::Standard_XFwd_YUp,
        -13.0f, -2.5f, 0.0f,
        8.0f, -2.0f, 0.0f,
        true
    },
    // 4: Varmint Rifle
    {
        WeaponID::VarmintRifle,
        "Varmint Rifle",
        "meshes\\weapons\\2handrifle",
        "varmintrifle.nif",
        "textures\\weapons\\2handrifle\\varmintrifle.dds",
        5,
        18,
        1.8f,
        2.2f,
        2.8f,
        0.050f,
        40.0f,
        9.0f, 3.8f, -3.8f,
        6.0f, 0.0f, -8.18f,
        0.95f,
        25.0f,
        0,
        50,
        WeaponAxisType::Standard_XFwd_YUp,
        -13.0f, -2.5f, 0.0f,
        12.0f, -2.0f, 0.0f,
        true
    }
};
static const size_t WEAPON_CONFIG_COUNT = sizeof(WEAPON_CONFIGS) / sizeof(WEAPON_CONFIGS[0]);

static bool extractDdsFromBsa(BSAReader& texBsa1, BSAReader* texBsa2, const std::string& path, std::vector<uint8_t>& outData) {
    if (path.empty()) return false;
    size_t slash = path.find_last_of("/\\");
    std::string folder = (slash != std::string::npos) ? path.substr(0, slash) : "";
    std::string file   = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    auto tryExtract = [&](BSAReader& bsa) -> bool {
        if (bsa.extractFile(folder.c_str(), file.c_str(), outData)) return true;
        if (folder.rfind("textures", 0) != 0) {
            std::string withTex = "textures\\" + folder;
            if (bsa.extractFile(withTex.c_str(), file.c_str(), outData)) return true;
        } else {
            size_t afterTex = folder.find_first_of("/\\");
            if (afterTex != std::string::npos) {
                std::string withoutTex = folder.substr(afterTex + 1);
                if (bsa.extractFile(withoutTex.c_str(), file.c_str(), outData)) return true;
            }
        }
        return false;
    };

    if (tryExtract(texBsa1)) return true;
    if (texBsa2 && texBsa2->isOpen() && tryExtract(*texBsa2)) return true;
    return false;
}

WeaponSystem::WeaponSystem() {
    m_currentConfig = &WEAPON_CONFIGS[0];
    m_currentMag = m_currentConfig->magCapacity;
    m_reserveAmmo = m_currentConfig->defaultReserve;
}

WeaponSystem::~WeaponSystem() {
}

bool WeaponSystem::init(GxmRenderer& renderer) {
    if (!renderer.allocateDynamicMesh(MAX_HUD_VERTICES, MAX_HUD_INDICES, m_hudMesh)) {
        return false;
    }
    if (!renderer.allocateDynamicMesh(MAX_FX_VERTICES, MAX_FX_INDICES, m_fxMesh)) {
        renderer.destroyGxmMesh(m_hudMesh);
        return false;
    }
    return true;
}

bool WeaponSystem::initHands(BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2, GxmRenderer& renderer) {
    std::vector<uint8_t> pipboyBytes;NifLoader pipboyNif;
    if(meshBsa.extractFile("meshes\\pipboy3000","pipboyarm.nif",pipboyBytes) && pipboyNif.load(pipboyBytes.data(),pipboyBytes.size())) renderer.createGxmMesh(pipboyNif.getMesh(),m_pipboyMesh);
    m_font.init(renderer, &texBsa1, texBsa2);
    std::vector<uint8_t> reticleData;
    if(extractDdsFromBsa(texBsa1,texBsa2,"textures\\interface\\hud\\glow_crosshair.dds",reticleData)) {
        DDSLoader dds;
        if(dds.load(reticleData.data(),reticleData.size())) renderer.createGxmTexture(dds.getTexture(),m_reticle);
    }
    // 3. Load Hand Texture from textures\characters\male\HandMale.dds
    std::vector<uint8_t> ddsData;
    if (extractDdsFromBsa(texBsa1, texBsa2, "textures\\characters\\male\\handmale.dds", ddsData)) {
        DDSLoader dds;
        if (dds.load(ddsData.data(), ddsData.size())) {
            if (renderer.createGxmTexture(dds.getTexture(), m_handTex)) {
                m_hasHandTex = true;
                logMsg("WeaponSystem: loaded 1st-person hand texture (HandMale.dds)");
            }
        }
    }

    return true;
}

void WeaponSystem::cleanup(GxmRenderer& renderer) {
    m_animation.clear();for(auto& pose:m_cpuPose)pose.clear();
    renderer.destroyGxmMesh(m_pipboyMesh);
    renderer.destroyGxmMesh(m_armsMesh);
    m_font.cleanup(renderer);
    renderer.destroyGxmTexture(m_reticle);
    if (m_hasTexture) {
        renderer.destroyGxmTexture(m_weaponTex);
        m_hasTexture = false;
    }
    if (m_hasMesh) {
        renderer.destroyGxmMesh(m_weaponMesh);
        m_hasMesh = false;
    }
    if (m_hasHandTex) {
        renderer.destroyGxmTexture(m_handTex);
        m_hasHandTex = false;
    }
    if (m_hasRightHandMesh) {
        renderer.destroyGxmMesh(m_rightHandMesh);
        m_hasRightHandMesh = false;
    }
    if (m_hasLeftHandMesh) {
        renderer.destroyGxmMesh(m_leftHandMesh);
        m_hasLeftHandMesh = false;
    }
    renderer.destroyGxmMesh(m_hudMesh);
    renderer.destroyGxmMesh(m_fxMesh);
}

bool WeaponSystem::loadWeapon(WeaponID id, BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2, GxmRenderer& renderer) {
    if (m_hasMesh && id == m_currentWeaponId) return true;
    const WeaponConfig* found = nullptr;
    for (size_t i = 0; i < WEAPON_CONFIG_COUNT; ++i) {
        if (WEAPON_CONFIGS[i].id == id) {
            found = &WEAPON_CONFIGS[i];
            break;
        }
    }
    if (!found) return false;

    // Extract NIF from Meshes BSA
    std::vector<uint8_t> nifData;
    if (!meshBsa.extractFile(found->nifFolder, found->nifFile, nifData)) {
        return false;
    }

    NifLoader nifLoader;
    if (!nifLoader.load(nifData.data(), nifData.size())) {
        return false;
    }

    NifMesh mesh = nifLoader.getMesh();
    if (mesh.vertexCount == 0 || mesh.indices.empty()) {
        return false;
    }

    GxmMesh newMesh;
    GxmTexture newTexture;
    bool hasNewTexture = false;
    if (!renderer.createGxmMesh(mesh, newMesh)) {
        return false;
    }

    // Load diffuse texture
    std::string texPath = !mesh.diffuseTexture.empty() ? mesh.diffuseTexture : found->texPath;
    std::vector<uint8_t> ddsData;
    if (mesh.parts.empty() && extractDdsFromBsa(texBsa1, texBsa2, texPath, ddsData)) {
        DDSLoader ddsLoader;
        if (ddsLoader.load(ddsData.data(), ddsData.size())) {
            if (renderer.createGxmTexture(ddsLoader.getTexture(), newTexture)) {
                hasNewTexture = true;
            }
        }
    }

    GxmMesh newRight, newLeft, newArms;
    auto loadHand=[&](const char* side,GxmMesh& result) {
        char name[64];snprintf(name,sizeof(name),"hands%d_%s.nif",(int)id,side);
        std::vector<uint8_t> bytes;NifLoader nif;
        return readPackagedAsset(name,bytes) && nif.load(bytes.data(),bytes.size()) && renderer.createGxmMesh(nif.getMesh(),result);
    };
    bool right=loadHand("right",newRight),left=loadHand("left",newLeft);

    std::vector<uint8_t> armBytes;NifLoader armNif;
    if(readPackagedAsset("player_arms.nif",armBytes) && armNif.load(armBytes.data(),armBytes.size())) renderer.createGxmMesh(armNif.getMesh(),newArms);

    // Commit only after the replacement model has loaded successfully.
    if (m_currentConfig) {
        m_magazines[static_cast<int>(m_currentWeaponId)] = m_currentMag;
        m_ammoPools[ammoPool(m_currentWeaponId)] = m_reserveAmmo;
    }
    renderer.destroyGxmTexture(m_weaponTex);
    renderer.destroyGxmMesh(m_weaponMesh);
    renderer.destroyGxmMesh(m_rightHandMesh); renderer.destroyGxmMesh(m_leftHandMesh);
    renderer.destroyGxmMesh(m_armsMesh);m_armsMesh=std::move(newArms);
    m_rightHandMesh=std::move(newRight);m_leftHandMesh=std::move(newLeft);
    m_hasRightHandMesh=right;m_hasLeftHandMesh=left;
    m_weaponMesh = newMesh;
    m_weaponTex = newTexture;
    m_hasMesh = true;
    m_hasTexture = hasNewTexture;
    m_currentConfig = found;
    m_currentWeaponId = id;
    uint32_t counts[]={m_weaponMesh.vertexCount,m_rightHandMesh.vertexCount,m_leftHandMesh.vertexCount,m_armsMesh.vertexCount};
    const GxmMesh* sources[]={&m_weaponMesh,&m_rightHandMesh,&m_leftHandMesh,&m_armsMesh};
    for(unsigned i=0;i<4;++i)m_cpuPose[i].init(*sources[i]);
    char animationPath[512];snprintf(animationPath,sizeof(animationPath),FNV_APP_ROOT "assets/viewmodel%d.vma",(int)id);
    m_reloadDuration=found->reloadTime;m_poseTime=0;m_attackAge=1000;
    if(m_animation.load(animationPath,counts)) {
        m_reloadDuration=m_animation.duration(4);
        logMsg("WeaponSystem: original KF animation loaded for %s; reload %.3fs",found->displayName,m_reloadDuration);
    } else logMsg("WeaponSystem: animation load failed for %s",animationPath);

    m_currentMag = m_magazines[static_cast<int>(id)];
    m_reserveAmmo = m_ammoPools[ammoPool(id)];
    m_fireCooldown = 0.0f;
    m_recoilAnim = 0.0f;
    m_recoilSpringPos = m_recoilSpringVel = 0.0f;
    m_muzzleFlashTimer = 0.0f;
    m_isReloading = false;
    m_reloadTimer = 0.0f;
    return true;
}

int WeaponSystem::ammoPool(WeaponID id) {
    switch (id) {
        case WeaponID::ServiceRifle:
        case WeaponID::VarmintRifle: return 1;
        case WeaponID::LaserPistol: return 2;
        case WeaponID::CaravanShotgun: return 3;
        default: return 0;
    }
}

void WeaponSystem::setWeaponByIndex(int index, BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2, GxmRenderer& renderer) {
    if (index >= 0 && index < (int)WEAPON_CONFIG_COUNT) {
        loadWeapon(WEAPON_CONFIGS[index].id, meshBsa, texBsa1, texBsa2, renderer);
    }
}

void WeaponSystem::startReload(AudioSystem* audio) {
    if (m_isReloading) return;
    if (m_currentMag >= m_currentConfig->magCapacity) return;
    if (m_reserveAmmo <= 0) return;

    m_isReloading = true;
    m_reloadTimer = m_reloadDuration;
    if (audio) audio->play(SoundID::GunReloadIn, 0.85f);
}

void WeaponSystem::fireShot(float eyeX, float eyeY, float eyeZ, float pitchRad, float yawRad,
                            AudioSystem* audio) {
    if (m_currentMag <= 0) {
        if (audio) audio->play(SoundID::GunDryFire, 0.7f);
        startReload(audio);
        return;
    }

    m_currentMag--;
    m_attackAge=0;
    m_fireCooldown = 1.0f / m_currentConfig->fireRate;
    m_recoilAnim = 1.0f;
    m_recoilSpringVel += 28.0f * m_currentConfig->recoilKick;
    m_recoilPitchImpulse = m_currentConfig->recoilPitch;
    m_muzzleFlashTimer = 0.065f;
    m_crosshairSpread += 14.0f;

    if (audio) {
        if (m_currentConfig->fxType == 1) {
            audio->play(SoundID::GunFireLaser, 1.0f);
        } else if (m_currentConfig->fxType == 2) {
            audio->play(SoundID::GunFireShotgun, 1.0f);
        } else if (m_currentConfig->id == WeaponID::ServiceRifle || m_currentConfig->id == WeaponID::VarmintRifle) {
            audio->play(SoundID::GunFireRifle, 1.0f);
        } else {
            audio->play(SoundID::GunFire10mm, 1.0f);
        }
    }

    // Raycast forward from camera
    float fx = cosf(pitchRad) * sinf(yawRad);
    float fy = cosf(pitchRad) * cosf(yawRad);
    float fz = sinf(pitchRad);

    float hitX, hitY, hitZ, normalX, normalY, normalZ;
    if (!m_physics || !m_physics->raycast(eyeX, eyeY, eyeZ, fx, fy, fz, 1500.0f,
                                        hitX, hitY, hitZ, normalX, normalY, normalZ)) return;

    // Spawn 10 impact spark particles
    int sparkCount = (m_currentConfig->fxType == 2) ? 18 : 10;
    for (int i = 0; i < sparkCount; ++i) {
        ImpactParticle p;
        p.x = hitX;
        p.y = hitY;
        p.z = hitZ;
        // Bounce back slightly along -F with random burst
        float randX = ((float)(rand() % 100) / 50.0f - 1.0f);
        float randY = ((float)(rand() % 100) / 50.0f - 1.0f);
        float randZ = ((float)(rand() % 100) / 50.0f - 1.0f);

        p.vx = -fx * 4.0f + randX * 12.0f;
        p.vy = -fy * 4.0f + randY * 12.0f;
        p.vz = -fz * 4.0f + randZ * 12.0f + 6.0f;
        p.life = 0.40f;
        p.maxLife = 0.40f;

        if (m_currentConfig->fxType == 1) { // Laser
            p.r = COLOR_LASER_RED[0]; p.g = COLOR_LASER_RED[1]; p.b = COLOR_LASER_RED[2];
        } else {
            p.r = COLOR_SPARK_GOLD[0]; p.g = COLOR_SPARK_GOLD[1]; p.b = COLOR_SPARK_GOLD[2];
        }
        m_particles.push_back(p);
    }
}

void WeaponSystem::update(const SceCtrlData& pad, uint32_t pressed, float deltaTime,
                          float eyeX, float eyeY, float eyeZ, float pitchRad, float yawRad,
                          bool isWalking, bool isSprinting, AudioSystem* audio) {
    if (!m_currentConfig) return;

    m_poseTime+=deltaTime;m_attackAge+=deltaTime;

    // Timers & cooldowns
    if (m_fireCooldown > 0.0f) {
        m_fireCooldown -= deltaTime;
        if (m_fireCooldown < 0.0f) m_fireCooldown = 0.0f;
    }
    if (m_muzzleFlashTimer > 0.0f) {
        m_muzzleFlashTimer -= deltaTime;
        if (m_muzzleFlashTimer < 0.0f) m_muzzleFlashTimer = 0.0f;
    }
    if (m_recoilAnim > 0.0f) {
        m_recoilAnim = std::max(0.0f, m_recoilAnim - deltaTime * 9.0f);
    }

    // Spring-damper recoil simulation
    float kSpring = 260.0f;
    float cDamper = 26.0f;
    float springForce = -kSpring * m_recoilSpringPos - cDamper * m_recoilSpringVel;
    m_recoilSpringVel += springForce * deltaTime;
    m_recoilSpringPos += m_recoilSpringVel * deltaTime;
    if (m_recoilSpringPos < 0.0f && fabsf(m_recoilSpringVel) < 0.08f) {
        m_recoilSpringPos = 0.0f;
        m_recoilSpringVel = 0.0f;
    }

    // Reload logic
    if (m_isReloading) {
        m_reloadTimer -= deltaTime;
        if (m_reloadTimer <= 0.0f) {
            m_isReloading = false;
            m_reloadTimer = 0.0f;
            int needed = m_currentConfig->magCapacity - m_currentMag;
            int take = std::min(needed, m_reserveAmmo);
            m_currentMag += take;
            m_reserveAmmo -= take;
            if (audio) audio->play(SoundID::GunReloadOut, 0.85f);
        }
    }

    // Aim Down Sights (L-Trigger hold)
    m_isAiming = (pad.buttons & SCE_CTRL_LTRIGGER) != 0;
    float targetAds = m_isAiming ? 1.0f : 0.0f;
    m_adsProgress += (targetAds - m_adsProgress) * std::min(1.0f, deltaTime * 12.0f);

    float targetFov = m_isAiming ? m_currentConfig->adsFov : 65.0f;
    m_currentFov += (targetFov - m_currentFov) * std::min(1.0f, deltaTime * 12.0f);

    // Reload trigger (Square)
    if ((pressed & SCE_CTRL_SQUARE) && !m_isReloading) {
        startReload(audio);
    }

    // Fire trigger (R-Trigger)
    bool fireRequested = ((m_currentWeaponId == WeaponID::ServiceRifle ? pad.buttons : pressed) & SCE_CTRL_RTRIGGER) != 0;
    if (fireRequested && m_fireCooldown <= 0.0f && !m_isReloading) {
        fireShot(eyeX, eyeY, eyeZ, pitchRad, yawRad, audio);
    }

    // Authentic viewmodel breathing and walk bobbing animation
    m_idleBreathTimer += deltaTime;
    float breathY = sinf(m_idleBreathTimer * 2.0f) * 0.35f;
    float breathX = cosf(m_idleBreathTimer * 1.0f) * 0.22f;

    float baseSpread = 10.0f;
    if (isSprinting || isWalking) {
        float bobSpeed = isSprinting ? 13.0f : 7.8f;
        m_walkBobTimer += deltaTime * bobSpeed;
        float bobScale = isSprinting ? 1.0f : 0.65f;
        m_walkBobX = cosf(m_walkBobTimer) * (0.80f * bobScale);
        m_walkBobY = sinf(m_walkBobTimer * 2.0f) * (0.60f * bobScale);
        m_walkBobRoll = cosf(m_walkBobTimer) * (0.065f * bobScale);
        baseSpread = isSprinting ? 32.0f : 20.0f;
    } else {
        m_walkBobX *= std::max(0.0f, 1.0f - deltaTime * 8.0f);
        m_walkBobY *= std::max(0.0f, 1.0f - deltaTime * 8.0f);
        m_walkBobRoll *= std::max(0.0f, 1.0f - deltaTime * 8.0f);
        baseSpread = 10.0f;
    }

    m_swayAmountX = breathX + m_walkBobX;
    m_swayAmountY = breathY + m_walkBobY;

    if (m_isAiming) {
        baseSpread = 4.0f;
        m_swayAmountX *= 0.15f;
        m_swayAmountY *= 0.15f;
        m_walkBobRoll *= 0.15f;
    }

    m_crosshairSpread += (baseSpread - m_crosshairSpread) * std::min(1.0f, deltaTime * 10.0f);

    // Update impact particles
    for (size_t i = 0; i < m_particles.size(); ) {
        auto& p = m_particles[i];
        p.life -= deltaTime;
        if (p.life <= 0.0f) {
            m_particles.erase(m_particles.begin() + i);
        } else {
            p.x += p.vx * deltaTime;
            p.y += p.vy * deltaTime;
            p.z += p.vz * deltaTime;
            p.vz -= 35.0f * deltaTime; // gravity
            i++;
        }
    }
}

void WeaponSystem::renderViewmodel(GxmRenderer& renderer,
                                   float eyeX, float eyeY, float eyeZ,
                                   float pitchRad, float yawRad) {
    m_poseCpuUs=0;
    if (!m_hasMesh || !m_currentConfig) return;

    // View direction vector (Gamebryo Z-up)
    float fx = cosf(pitchRad) * sinf(yawRad);
    float fy = cosf(pitchRad) * cosf(yawRad);
    float fz = sinf(pitchRad);

    // Right vector
    float rx = cosf(yawRad);
    float ry = -sinf(yawRad);
    float rz = 0.0f;

    // Up vector: R x F
    float ux = -sinf(yawRad) * sinf(pitchRad);
    float uy = -cosf(yawRad) * sinf(pitchRad);
    float uz = cosf(pitchRad);

    float originalMuzzle[3]{};Mat4 pipboyAttachment=Mat4::identity();
    GxmMesh* animatedMeshes[]={&m_cpuPose[0].view,&m_cpuPose[1].view,&m_cpuPose[2].view,&m_cpuPose[3].view};
    uint32_t poseStart=poseClockUs();
    bool originalPose=m_animation.apply(animatedMeshes,m_poseTime,m_adsProgress,m_attackAge,
                                       m_isReloading,1.f-m_reloadTimer/std::max(.001f,m_reloadDuration),originalMuzzle,&pipboyAttachment,renderer.cpuPreparationPool());
    m_poseCpuUs=poseClockUs()-poseStart;
    if(!originalPose) return;
    // Imported vertices are Camera1st-relative: X right, Y forward, Z up.
    Mat4 vm=Mat4::identity();
    vm.m[0][0]=rx;vm.m[0][1]=ry;vm.m[0][2]=rz;
    vm.m[1][0]=fx;vm.m[1][1]=fy;vm.m[1][2]=fz;
    vm.m[2][0]=ux;vm.m[2][1]=uy;vm.m[2][2]=uz;
    vm.m[3][0]=eyeX;vm.m[3][1]=eyeY;vm.m[3][2]=eyeZ;

    renderer.drawMeshInstance(m_cpuPose[0].view, m_hasTexture ? &m_weaponTex : nullptr, vm);
    if(m_hasRightHandMesh) renderer.drawMeshInstance(m_cpuPose[1].view,m_hasHandTex?&m_handTex:nullptr,vm);
    if(m_hasLeftHandMesh) renderer.drawMeshInstance(m_cpuPose[2].view,m_hasHandTex?&m_handTex:nullptr,vm);

    if(originalPose) {
        renderer.drawMeshInstance(m_cpuPose[3].view,nullptr,vm);
        // Original PipBoyArm Prn extra data attaches the device to L ForeTwist.
        if(m_pipboyMesh.isValid()) renderer.drawMeshInstance(m_pipboyMesh,nullptr,vm*pipboyAttachment);
    }

    // Muzzle Flash Billboard (if firing)
    if (m_muzzleFlashTimer > 0.0f) {
        float muzX=eyeX+rx*originalMuzzle[0]+fx*originalMuzzle[1]+ux*originalMuzzle[2];
        float muzY=eyeY+ry*originalMuzzle[0]+fy*originalMuzzle[1]+uy*originalMuzzle[2];
        float muzZ=eyeZ+rz*originalMuzzle[0]+fz*originalMuzzle[1]+uz*originalMuzzle[2];

        float fsize = (m_currentConfig->fxType == 2) ? 6.5f : 4.0f;
        float mr = (m_currentConfig->fxType == 1) ? COLOR_LASER_RED[0] : 1.0f;
        float mg = (m_currentConfig->fxType == 1) ? COLOR_LASER_RED[1] : 0.85f;
        float mb = (m_currentConfig->fxType == 1) ? COLOR_LASER_RED[2] : 0.25f;

        m_fxVertexCount = 0;
        m_fxIndexCount = 0;
        BasicVertex* v = m_fxMesh.vertices;
        uint16_t* idx = m_fxMesh.indices;

        // Quad 1: cross star
        v[0].position[0] = muzX - rx * fsize; v[0].position[1] = muzY - ry * fsize; v[0].position[2] = muzZ;
        v[1].position[0] = muzX + rx * fsize; v[1].position[1] = muzY + ry * fsize; v[1].position[2] = muzZ;
        v[2].position[0] = muzX - ux * fsize; v[2].position[1] = muzY - uy * fsize; v[2].position[2] = muzZ - uz * fsize;
        v[3].position[0] = muzX + ux * fsize; v[3].position[1] = muzY + uy * fsize; v[3].position[2] = muzZ + uz * fsize;

        for (int k = 0; k < 4; ++k) {
            v[k].color[0] = mr; v[k].color[1] = mg; v[k].color[2] = mb; v[k].color[3] = 1.0f;
            v[k].normal[0] = 0; v[k].normal[1] = 0; v[k].normal[2] = 1.0f;
            v[k].uv[0] = 0; v[k].uv[1] = 0;
        }

        idx[0] = 0; idx[1] = 1; idx[2] = 2;
        idx[3] = 1; idx[4] = 3; idx[5] = 2;

        GxmMesh flashSubMesh;
        flashSubMesh.vertices = v;
        flashSubMesh.indices = idx;
        flashSubMesh.indexCount = 6;
        flashSubMesh.vertexCount = 4;
        flashSubMesh.dynamic = true;
        renderer.drawMeshInstance(flashSubMesh, nullptr, Mat4::identity());
    }
}

void WeaponSystem::renderWorldEffects(GxmRenderer& renderer) {
    if (m_particles.empty()) return;

    m_fxVertexCount = 0;
    m_fxIndexCount = 0;
    BasicVertex* v = m_fxMesh.vertices;
    uint16_t* idx = m_fxMesh.indices;

    for (const auto& p : m_particles) {
        if (m_fxVertexCount + 4 > MAX_FX_VERTICES || m_fxIndexCount + 6 > MAX_FX_INDICES) break;
        uint16_t base = (uint16_t)m_fxVertexCount;
        float alpha = p.life / p.maxLife;
        float sz = 1.2f * alpha;

        v[m_fxVertexCount].position[0] = p.x - sz; v[m_fxVertexCount].position[1] = p.y - sz; v[m_fxVertexCount].position[2] = p.z;
        v[m_fxVertexCount].color[0] = p.r; v[m_fxVertexCount].color[1] = p.g; v[m_fxVertexCount].color[2] = p.b; v[m_fxVertexCount].color[3] = alpha;
        m_fxVertexCount++;

        v[m_fxVertexCount].position[0] = p.x + sz; v[m_fxVertexCount].position[1] = p.y - sz; v[m_fxVertexCount].position[2] = p.z;
        v[m_fxVertexCount].color[0] = p.r; v[m_fxVertexCount].color[1] = p.g; v[m_fxVertexCount].color[2] = p.b; v[m_fxVertexCount].color[3] = alpha;
        m_fxVertexCount++;

        v[m_fxVertexCount].position[0] = p.x - sz; v[m_fxVertexCount].position[1] = p.y + sz; v[m_fxVertexCount].position[2] = p.z + sz;
        v[m_fxVertexCount].color[0] = p.r; v[m_fxVertexCount].color[1] = p.g; v[m_fxVertexCount].color[2] = p.b; v[m_fxVertexCount].color[3] = alpha;
        m_fxVertexCount++;

        v[m_fxVertexCount].position[0] = p.x + sz; v[m_fxVertexCount].position[1] = p.y + sz; v[m_fxVertexCount].position[2] = p.z + sz;
        v[m_fxVertexCount].color[0] = p.r; v[m_fxVertexCount].color[1] = p.g; v[m_fxVertexCount].color[2] = p.b; v[m_fxVertexCount].color[3] = alpha;
        m_fxVertexCount++;

        idx[m_fxIndexCount++] = base + 0;
        idx[m_fxIndexCount++] = base + 1;
        idx[m_fxIndexCount++] = base + 2;
        idx[m_fxIndexCount++] = base + 2;
        idx[m_fxIndexCount++] = base + 1;
        idx[m_fxIndexCount++] = base + 3;
    }

    if (m_fxIndexCount > 0) {
        GxmMesh sparkSubMesh;
        sparkSubMesh.vertices = v;
        sparkSubMesh.indices = idx;
        sparkSubMesh.indexCount = m_fxIndexCount;
        sparkSubMesh.vertexCount = m_fxVertexCount;
        sparkSubMesh.dynamic = true;
        renderer.drawMeshInstance(sparkSubMesh, nullptr, Mat4::identity());
    }
}

// ============================================================================
// HUD Rendering
// ============================================================================

void WeaponSystem::addHudQuad(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (m_hudVertexCount + 4 > MAX_HUD_VERTICES || m_hudIndexCount + 6 > MAX_HUD_INDICES) return;
    BasicVertex* verts = m_hudMesh.vertices;
    uint16_t* idxs     = m_hudMesh.indices;
    uint16_t base      = (uint16_t)m_hudVertexCount;

    verts[m_hudVertexCount].position[0] = x;     verts[m_hudVertexCount].position[1] = y;     verts[m_hudVertexCount].position[2] = 0;
    verts[m_hudVertexCount].color[0] = r; verts[m_hudVertexCount].color[1] = g; verts[m_hudVertexCount].color[2] = b; verts[m_hudVertexCount].color[3] = a;
    m_hudVertexCount++;

    verts[m_hudVertexCount].position[0] = x + w; verts[m_hudVertexCount].position[1] = y;     verts[m_hudVertexCount].position[2] = 0;
    verts[m_hudVertexCount].color[0] = r; verts[m_hudVertexCount].color[1] = g; verts[m_hudVertexCount].color[2] = b; verts[m_hudVertexCount].color[3] = a;
    m_hudVertexCount++;

    verts[m_hudVertexCount].position[0] = x;     verts[m_hudVertexCount].position[1] = y + h; verts[m_hudVertexCount].position[2] = 0;
    verts[m_hudVertexCount].color[0] = r; verts[m_hudVertexCount].color[1] = g; verts[m_hudVertexCount].color[2] = b; verts[m_hudVertexCount].color[3] = a;
    m_hudVertexCount++;

    verts[m_hudVertexCount].position[0] = x + w; verts[m_hudVertexCount].position[1] = y + h; verts[m_hudVertexCount].position[2] = 0;
    verts[m_hudVertexCount].color[0] = r; verts[m_hudVertexCount].color[1] = g; verts[m_hudVertexCount].color[2] = b; verts[m_hudVertexCount].color[3] = a;
    m_hudVertexCount++;

    idxs[m_hudIndexCount++] = base + 0;
    idxs[m_hudIndexCount++] = base + 1;
    idxs[m_hudIndexCount++] = base + 2;
    idxs[m_hudIndexCount++] = base + 2;
    idxs[m_hudIndexCount++] = base + 1;
    idxs[m_hudIndexCount++] = base + 3;
}

void WeaponSystem::addHudFrame(float x, float y, float w, float h, float thickness, float r, float g, float b, float a) {
    addHudQuad(x, y, w, thickness, r, g, b, a);
    addHudQuad(x, y + h - thickness, w, thickness, r, g, b, a);
    addHudQuad(x, y + thickness, thickness, h - 2.0f * thickness, r, g, b, a);
    addHudQuad(x + w - thickness, y + thickness, thickness, h - 2.0f * thickness, r, g, b, a);
}

void WeaponSystem::addHudChar(char c, float x, float y, float scale, float r, float g, float b, float a) {
    if (c < 32 || c > 126) c = ' ';
    int idx = c - 32;
    const uint8_t* glyph = g_pipboyFont8x8[idx];

    for (int row = 0; row < 8; ++row) {
        uint8_t bits = glyph[row];
        if (bits == 0) continue;
        int col = 0;
        while (col < 8) {
            if ((bits >> (7 - col)) & 1) {
                int startCol = col;
                while (col < 8 && ((bits >> (7 - col)) & 1)) col++;
                int span = col - startCol;
                addHudQuad(x + startCol * scale, y + row * scale, span * scale, scale, r, g, b, a);
            } else {
                col++;
            }
        }
    }
}

void WeaponSystem::addHudString(const char* text, float x, float y, float scale, float r, float g, float b, float a) {
    if (m_font.valid()) { m_font.text(text, x, y, scale*14.0f, r,g,b,a); return; }
    if (!text) return;
    float curX = x;
    while (*text) {
        if (*text == '\n') {
            y += (8.0f * scale) + 4.0f;
            curX = x;
        } else {
            addHudChar(*text, curX, y, scale, r, g, b, a);
            curX += (8.0f * scale) + (1.0f * scale);
        }
        text++;
    }
}

void WeaponSystem::addHudBar(float x, float y, float w, float h, float fillFraction, float r, float g, float b, float a) {
    if (fillFraction < 0.0f) fillFraction = 0.0f;
    if (fillFraction > 1.0f) fillFraction = 1.0f;
    addHudFrame(x, y, w, h, 1.5f, r * 0.7f, g * 0.7f, b * 0.7f, a);
    float innerW = (w - 4.0f) * fillFraction;
    if (innerW > 1.0f) {
        addHudQuad(x + 2.0f, y + 2.0f, innerW, h - 4.0f, r, g, b, a);
    }
}

void WeaponSystem::renderHud(GxmRenderer& renderer) {
    m_hudVertexCount = 0;
    m_hudIndexCount = 0;

    float alpha=1-m_adsProgress;
    if(alpha>.02f && m_reticle.isValid()) {
        BasicVertex vertices[4]{}; uint16_t indices[]={0,1,2,2,1,3};
        for(int i=0;i<4;++i) {
            auto& v=vertices[i];v.position[0]=468+(i&1)*24;v.position[1]=260+(i>>1)*24;
            v.uv[0]=i&1;v.uv[1]=i>>1;v.color[0]=1;v.color[1]=.72f;v.color[2]=.18f;v.color[3]=alpha;
        }
        renderer.drawTextured2D(vertices,4,indices,6,&m_reticle);
    }
    if(m_currentConfig) {
        char ammo[32];snprintf(ammo,sizeof(ammo),"%d / %d",m_currentMag,m_reserveAmmo);
        addHudString(ammo,794,483,1.5f,1,.72f,.18f,1);
        if(m_isReloading) addHudString("Reloading",794,507,1,1,.72f,.18f,1);
    }
    m_font.flush(renderer);
}
