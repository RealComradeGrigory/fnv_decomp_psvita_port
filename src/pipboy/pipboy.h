#pragma once
#include "ui/original_font.h"

#include <psp2/ctrl.h>
#include <vector>
#include <string>
#include "renderer/gxm_renderer.h"

enum class PipBoyMainTab {
    Stat = 0,
    Items = 1,
    Data = 2
};

struct InventoryItem {
    std::string name;
    int count;
    float weight;
    int value;
    int damageOrDt;
    int condition;
    bool isEquipped;
    int healHp;
    int weaponId = -1;
};

class AudioSystem;
class BSAReader;

class PipBoy {
public:
    PipBoy();
    ~PipBoy();

    bool init(GxmRenderer& renderer, BSAReader* texBsa1 = nullptr, BSAReader* texBsa2 = nullptr);
    void cleanup(GxmRenderer& renderer);

    void update(const SceCtrlData& pad, uint32_t pressed, float deltaTime,
                const char* currentCellName, float posX, float posY, float posZ, float yaw,
                AudioSystem* audio = nullptr);
    void render(GxmRenderer& renderer);

    bool isOpen() const { return m_isOpen; }
    void toggleOpen() { m_isOpen = !m_isOpen; }
    void close() { m_isOpen = false; }

    bool isFlashlightOn() const { return m_flashlightOn; }

    int getPlayerHp() const { return m_playerHp; }
    int getMaxHp() const { return m_maxHp; }

    int getEquippedWeaponIndex() const {
        for (size_t i = 0; i < m_weapons.size(); ++i) {
            if (m_weapons[i].isEquipped) return m_weapons[i].weaponId;
        }
        return 0;
    }
    void setEquippedWeaponIndex(int index) {
        for (size_t i = 0; i < m_weapons.size(); ++i) {
            m_weapons[i].isEquipped = (m_weapons[i].weaponId == index);
        }
    }

private:
    OriginalFont m_font;
    bool m_isOpen = false;
    bool m_flashlightOn = false;
    float m_triangleHoldTime = 0.0f;
    bool m_flashlightToggledOnHold = false;
    std::string m_currentCellName;
    float m_posX = 0.0f, m_posY = 0.0f, m_posZ = 0.0f, m_yaw = 0.0f;

    PipBoyMainTab m_mainTab = PipBoyMainTab::Stat;
    int m_subTab = 0; // index in current main tab's sub-tabs
    int m_cursorIndex = 0;

    // Player vitals & stats
    int m_playerHp = 185;
    int m_maxHp = 210;
    int m_playerAp = 85;
    int m_maxAp = 85;
    int m_playerLevel = 1;
    int m_playerXp = 450;
    int m_nextLevelXp = 1000;
    int m_caps = 340;
    float m_weight = 74.5f;
    float m_maxWeight = 210.0f;

    // Inventory lists
    std::vector<InventoryItem> m_weapons;
    std::vector<InventoryItem> m_apparel;
    std::vector<InventoryItem> m_aid;
    std::vector<InventoryItem> m_ammo;

    // Radio station state
    int m_activeRadio = 0; // 0 = RNV, 1 = MMR, 2 = BMR, -1 = OFF

    // Feedback notification banner
    char m_feedbackMsg[64] = "";
    float m_feedbackTimer = 0.0f;

    GxmTexture m_gaugeTextures[3];
    bool m_hasGauge[3]{};
    GxmTexture m_specialTextures[7];
    bool m_hasSpecial[7]{};
    GxmTexture m_statusTextures[7];
    bool m_hasStatus[7]{};

    // Authentic Bethesda Pip-Boy 3000 textures
    GxmTexture m_texBg;
    bool m_hasTexBg = false;

    GxmTexture m_texScanlines;
    bool m_hasTexScanlines = false;

    GxmTexture m_texGlare;
    bool m_hasTexGlare = false;

    GxmTexture m_texVaultBoyHappy;
    bool m_hasTexVaultBoyHappy = false;

    GxmTexture m_texVaultBoyPain;
    bool m_hasTexVaultBoyPain = false;

    GxmTexture m_texWeaponIcon;
    bool m_hasTexWeaponIcon = false;

    GxmTexture m_texFillbar;
    bool m_hasTexFillbar = false;

    // Dynamic UI GPU geometry buffers
    GxmMesh m_uiMesh;
    static const uint32_t MAX_UI_VERTICES = 16384;
    static const uint32_t MAX_UI_INDICES  = 24576;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;

    // UI builder primitives
    void resetBatch();
    void addQuad(float x, float y, float w, float h, float r, float g, float b, float a);
    void addTexturedQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, float r, float g, float b, float a);
    void drawTextured(GxmRenderer& renderer, const GxmTexture& tex, float x, float y, float w, float h,
                      float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    void addFrame(float x, float y, float w, float h, float thickness, float r, float g, float b, float a);
    void addChar(char c, float x, float y, float scale, float r, float g, float b, float a);
    void addString(const char* text, float x, float y, float scale, float r, float g, float b, float a);
    void addBar(float x, float y, float w, float h, float fillFraction, float r, float g, float b, float a);

    // Screen drawers
    void renderBezelAndScanlines();
    void renderHeader();
    void renderFooter();
    void renderStatScreen();
    void renderItemsScreen();
    void renderDataScreen(const char* currentCellName, float posX, float posY, float posZ, float yaw);

    void showFeedback(const char* msg);
};
