#pragma once
#include "ui/original_font.h"
#include "viewmodel_animation.h"
#include "cpu_viewmodel.h"

#include <psp2/ctrl.h>
#include <string>
#include <vector>
#include "renderer/gxm_renderer.h"
#include "bsa/bsa_reader.h"

class AudioSystem;
class PhysicsEngine;

enum class WeaponID {
    Pistol10mm = 0,
    ServiceRifle = 1,
    LaserPistol = 2,
    CaravanShotgun = 3,
    VarmintRifle = 4,
    Count = 5
};

enum class WeaponAxisType {
    Standard_XFwd_YUp = 0,   // 10mm Pistol, Laser Pistol, Caravan Shotgun, Varmint Rifle (X=Forward, Y=Up, Z=Right)
    ServiceRifle_XFwd_ZUp = 1 // Service Rifle (X=Forward, Z=Up, Y=-Right)
};

struct WeaponConfig {
    WeaponID id;
    const char* displayName;
    const char* nifFolder;
    const char* nifFile;
    const char* texPath;
    int magCapacity;
    int damage;
    float fireRate;        // shots/sec
    float reloadTime;      // seconds
    float recoilKick;      // units back
    float recoilPitch;     // camera pitch kick (radians)
    float adsFov;          // degrees FOV in ADS (e.g. 45.0)
    // Hip-fire viewmodel offsets (forward, right, up)
    float hipOffsetFwd;
    float hipOffsetRight;
    float hipOffsetUp;
    // ADS viewmodel offsets (forward, right, up)
    float adsOffsetFwd;
    float adsOffsetRight;
    float adsOffsetUp;
    // Model scale and barrel length for muzzle flash
    float scale;
    float barrelLength;
    int fxType;            // 0 = Ballistic, 1 = Laser energy, 2 = Heavy shotgun
    int defaultReserve;
    WeaponAxisType axisType;
    // Authentic First-person Hand Grip offsets (in weapon local coordinate space)
    float rightGripX, rightGripY, rightGripZ;
    float leftGripX, leftGripY, leftGripZ;
    bool hasLeftHandGrip;
};

struct ImpactParticle {
    float x, y, z;
    float vx, vy, vz;
    float life;
    float maxLife;
    float r, g, b;
};

class WeaponSystem {
public:
    WeaponSystem();
    ~WeaponSystem();

    bool init(GxmRenderer& renderer);
    void setPhysics(PhysicsEngine* physics) { m_physics = physics; }
    void cleanup(GxmRenderer& renderer);
    bool initHands(BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2, GxmRenderer& renderer);

    bool loadWeapon(WeaponID id, BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2, GxmRenderer& renderer);

    void update(const SceCtrlData& pad, uint32_t pressed, float deltaTime,
                float eyeX, float eyeY, float eyeZ, float pitchRad, float yawRad,
                bool isWalking, bool isSprinting, AudioSystem* audio = nullptr);

    void renderViewmodel(GxmRenderer& renderer,
                         float eyeX, float eyeY, float eyeZ,
                         float pitchRad, float yawRad);

    void renderHud(GxmRenderer& renderer);
    void renderWorldEffects(GxmRenderer& renderer);

    uint32_t poseCpuUs() const {return m_poseCpuUs;}
    WeaponID getCurrentWeaponID() const { return m_currentWeaponId; }
    int getCurrentMag() const { return m_currentMag; }
    int getReserveAmmo() const { return m_reserveAmmo; }
    bool isAiming() const { return m_isAiming; }
    float getAdsFov() const { return m_currentFov; }
    bool hasMuzzleFlash() const { return m_muzzleFlashTimer > 0.0f; }

    void setWeaponByIndex(int index, BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2, GxmRenderer& renderer);

private:
    uint32_t m_poseCpuUs=0;
    ViewmodelAnimation m_animation;
    CpuViewmodelMesh m_cpuPose[4];
    float m_poseTime=0, m_attackAge=1000, m_reloadDuration=1.4f;
    OriginalFont m_font;
    GxmTexture m_reticle;
    PhysicsEngine* m_physics = nullptr;
    WeaponID m_currentWeaponId = WeaponID::Pistol10mm;
    const WeaponConfig* m_currentConfig = nullptr;

    // Viewmodel GPU mesh and texture
    GxmMesh m_weaponMesh;
    GxmTexture m_weaponTex;
    bool m_hasMesh = false;
    bool m_hasTexture = false;

    // Ammo state
    int m_currentMag = 12;
    int m_reserveAmmo = 140;
    int m_magazines[5] = {12, 20, 30, 2, 5};
    int m_ammoPools[4] = {140, 85, 65, 32};
    static int ammoPool(WeaponID id);

    // ADS (Aim Down Sights) state
    bool m_isAiming = false;
    float m_adsProgress = 0.0f; // 0.0 = hip, 1.0 = ADS iron sights
    float m_currentFov = 65.0f;

    // Firing, Recoil & Reload state
    float m_fireCooldown = 0.0f;
    float m_recoilAnim = 0.0f;         // 0..1 displacement
    float m_recoilPitchImpulse = 0.0f; // pitch kick
    float m_muzzleFlashTimer = 0.0f;
    bool m_isReloading = false;
    float m_reloadTimer = 0.0f;

    // Sway & Movement animations
    float m_swayTimer = 0.0f;
    float m_swayAmountX = 0.0f;
    float m_swayAmountY = 0.0f;
    float m_walkBobTimer = 0.0f;
    float m_idleBreathTimer = 0.0f;
    float m_walkBobX = 0.0f;
    float m_walkBobY = 0.0f;
    float m_walkBobRoll = 0.0f;
    float m_recoilSpringPos = 0.0f;
    float m_recoilSpringVel = 0.0f;
    float m_crosshairSpread = 12.0f;

    // 3D Impact spark particles
    std::vector<ImpactParticle> m_particles;

    // Dynamic 2D HUD geometry buffer
    GxmMesh m_hudMesh;
    static const uint32_t MAX_HUD_VERTICES = 4096;
    static const uint32_t MAX_HUD_INDICES  = 6144;
    uint32_t m_hudVertexCount = 0;
    uint32_t m_hudIndexCount = 0;

    // 3D Particles / Muzzle flash geometry buffer
    GxmMesh m_fxMesh;
    static const uint32_t MAX_FX_VERTICES = 1024;
    static const uint32_t MAX_FX_INDICES  = 1536;
    uint32_t m_fxVertexCount = 0;
    uint32_t m_fxIndexCount = 0;

    // Authentic 3D Bethesda First-Person Hands & Pip-Boy Glove
    GxmMesh m_pipboyMesh;
    GxmMesh m_armsMesh;
    GxmMesh m_rightHandMesh;
    bool m_hasRightHandMesh = false;

    GxmMesh m_leftHandMesh;
    bool m_hasLeftHandMesh = false;

    GxmTexture m_handTex;
    bool m_hasHandTex = false;

    void addHudQuad(float x, float y, float w, float h, float r, float g, float b, float a);
    void addHudChar(char c, float x, float y, float scale, float r, float g, float b, float a);
    void addHudString(const char* text, float x, float y, float scale, float r, float g, float b, float a);
    void addHudFrame(float x, float y, float w, float h, float thickness, float r, float g, float b, float a);
    void addHudBar(float x, float y, float w, float h, float fillFraction, float r, float g, float b, float a);

    void fireShot(float eyeX, float eyeY, float eyeZ, float pitchRad, float yawRad,
                  AudioSystem* audio = nullptr);
    void startReload(AudioSystem* audio = nullptr);
};
