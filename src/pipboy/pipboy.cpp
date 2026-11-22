#include "pipboy.h"
#include "pipboy_font.h"
#include "audio/audio_system.h"
#include "bsa/bsa_reader.h"
#include "dds/dds_loader.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

extern void logMsg(const char* fmt, ...);

// Helper to extract DDS from BSA archives
static bool extractDds(BSAReader* texBsa1, BSAReader* texBsa2, const std::string& path, std::vector<uint8_t>& outData) {
    if (path.empty()) return false;
    size_t slash = path.find_last_of("/\\");
    std::string folder = (slash != std::string::npos) ? path.substr(0, slash) : "";
    std::string file   = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    auto tryExtract = [&](BSAReader& bsa) -> bool {
        if (!bsa.isOpen()) return false;
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

    if (texBsa1 && tryExtract(*texBsa1)) return true;
    if (texBsa2 && tryExtract(*texBsa2)) return true;
    return false;
}

// CRT Phosphor Amber Palette
static const float COLOR_AMBER_BRIGHT[4]   = { 1.00f, 0.82f, 0.18f, 1.0f }; // High-visibility text & active tab
static const float COLOR_AMBER_NORMAL[4]   = { 0.85f, 0.60f, 0.10f, 1.0f }; // Standard labels, borders, gauges
static const float COLOR_AMBER_DIM[4]      = { 0.40f, 0.26f, 0.04f, 1.0f }; // Inactive tabs, disabled options
static const float COLOR_AMBER_BG[4]       = { 0.04f, 0.03f, 0.01f, 0.94f }; // CRT screen background tint
static const float COLOR_AMBER_SEL_BAR[4]  = { 0.85f, 0.60f, 0.10f, 0.30f }; // Cursor selection row highlight
static const float COLOR_AMBER_SCANLINE[4] = { 0.01f, 0.01f, 0.00f, 0.38f }; // CRT cathode scanlines
static const float COLOR_BEZEL_BORDER[4]   = { 0.18f, 0.13f, 0.06f, 1.0f }; // Metallic bezel casing
static const float COLOR_GREEN_OK[4]       = { 0.25f, 0.92f, 0.30f, 1.0f }; // Condition OK status

PipBoy::PipBoy() {
    // Prototype inventory; not imported save-game or quest state.
    m_weapons = {
        { "10mm Pistol",           1,  3.0f, 150, 22,  92, true,   0, 0 },
        { "Service Rifle",         1,  8.5f, 280, 18,  85, false,  0, 1 },
        { "Varmint Rifle",         1,  5.5f,  75, 18, 100, false,  0, 4 },
        { "Laser Pistol",          1,  2.0f, 120, 12,  90, false,  0, 2 },
        { "Caravan Shotgun",       1,  3.0f, 200, 45,  75, false,  0, 3 },
        { "Super Sledge",          1, 20.0f, 380, 70,  80, false,  0 },
        { "Frag Grenade",          4,  2.0f, 200, 100, 100, false, 0 }
    };

    m_apparel = {
        { "Vault 21 Jumpsuit",     1,  1.0f,  50,  2, 100, true,   0 },
        { "Armored Vault Suit",    1,  8.0f, 220,  8,  88, false,  0 },
        { "NCR Ranger Armor",      1, 30.0f, 1200, 20, 95, false,  0 },
        { "NCR Ranger Helmet",     1,  3.0f, 350,  4,  90, false,  0 },
        { "Doc Mitchell Outfit",   1,  2.0f,  30,  2, 100, false,  0 }
    };

    m_aid = {
        { "Stimpak",               6,  0.0f,  75,  0, 100, false, 30 },
        { "Super Stimpak",         2,  0.5f, 150,  0, 100, false, 60 },
        { "Doctor's Bag",          1,  1.0f, 100,  0, 100, false, 50 },
        { "RadAway",               3,  0.1f,  80,  0, 100, false,  0 },
        { "Sunset Sarsaparilla",   4,  1.0f,  10,  0, 100, false, 25 },
        { "Nuka-Cola Victory",     2,  1.0f,  25,  0, 100, false, 35 },
        { "Purified Water",        5,  1.0f,  20,  0, 100, false, 20 }
    };

    m_ammo = {
        { "10mm Round",          140,  0.0f,   2,  0, 100, false,  0 },
        { "5.56mm Round",         85,  0.0f,   2,  0, 100, false,  0 },
        { "Energy Cell",          65,  0.0f,   3,  0, 100, false,  0 },
        { "20-Gauge Shotgun",     32,  0.0f,   3,  0, 100, false,  0 }
    };
}

PipBoy::~PipBoy() {
}

bool PipBoy::init(GxmRenderer& renderer, BSAReader* texBsa1, BSAReader* texBsa2) {
    if (!renderer.allocateDynamicMesh(MAX_UI_VERTICES, MAX_UI_INDICES, m_uiMesh)) {
        return false;
    }

    auto loadTexture = [&](const char* path, GxmTexture& tex, bool& hasTex) {
        std::vector<uint8_t> ddsData;
        if (extractDds(texBsa1, texBsa2, path, ddsData)) {
            DDSLoader dds;
            if (dds.load(ddsData.data(), ddsData.size())) {
                if (renderer.createGxmTexture(dds.getTexture(), tex)) {
                    hasTex = true;
                    logMsg("PipBoy: loaded authentic texture '%s'", path);
                }
            }
        }
    };

    m_font.init(renderer, texBsa1, texBsa2);

    // 1. Background Frame
    loadTexture("textures\\interface\\shared\\background\\pipboy.dds", m_texBg, m_hasTexBg);
    if (!m_hasTexBg) {
        loadTexture("textures\\pipboy3000\\pipboy.dds", m_texBg, m_hasTexBg);
    }

    // 2. CRT Screen Glare & Scanlines
    loadTexture("textures\\pipboy3000\\screenglare.dds", m_texGlare, m_hasTexGlare);
    loadTexture("textures\\pipboy3000\\pipboyscanlines.dds", m_texScanlines, m_hasTexScanlines);

    // 3. Authentic Vault Boy Status
    loadTexture("textures\\interface\\icons\\message icons\\glow_message_vaultboy_very_happy.dds", m_texVaultBoyHappy, m_hasTexVaultBoyHappy);
    loadTexture("textures\\interface\\icons\\message icons\\glow_message_vaultboy_in_pain.dds", m_texVaultBoyPain, m_hasTexVaultBoyPain);

    // 4. Authentic Inventory Weapon Icon
    loadTexture("textures\\interface\\icons\\pipboyimages_small\\weapons_small\\glow_weapons_10mm_pistol.dds", m_texWeaponIcon, m_hasTexWeaponIcon);

    // 5. Authentic Fillbar
    loadTexture("textures\\interface\\shared\\meter\\fillbar.dds", m_texFillbar, m_hasTexFillbar);

    const char* gauge[]={"vats_head_divider.dds","vats_left_divider.dds","vats_right_divider.dds"};
    for(int i=0;i<3;++i) {std::string path=std::string("textures\\interface\\vats\\")+gauge[i];loadTexture(path.c_str(),m_gaugeTextures[i],m_hasGauge[i]);}
    const char* attributes[]={"strength","perception","endurance","charisma","intelligence","agility","luck"};
    for(int i=0;i<7;++i) {std::string path=std::string("textures\\interface\\icons\\pipboyimages_small\\s.p.e.c.i.a.l_small\\glow_special_")+attributes[i]+".dds";loadTexture(path.c_str(),m_specialTextures[i],m_hasSpecial[i]);}
    const char* parts[] = { "head.dds", "face_00.dds", "torso.dds", "left_arm.dds", "right_arm.dds", "left_leg.dds", "right_leg.dds" };
    for (int i=0; i<7; ++i) {
        std::string path = std::string("textures\\interface\\stats\\") + parts[i];
        loadTexture(path.c_str(), m_statusTextures[i], m_hasStatus[i]);
    }
    return true;
}

void PipBoy::cleanup(GxmRenderer& renderer) {
    m_font.cleanup(renderer);
    for(int i=0;i<3;++i) {if(m_hasGauge[i]) renderer.destroyGxmTexture(m_gaugeTextures[i]);m_hasGauge[i]=false;}
    for(int i=0;i<7;++i) {if(m_hasSpecial[i]) renderer.destroyGxmTexture(m_specialTextures[i]);m_hasSpecial[i]=false;}
    for (int i=0;i<7;++i) { if (m_hasStatus[i]) renderer.destroyGxmTexture(m_statusTextures[i]); m_hasStatus[i]=false; }
    if (m_hasTexBg) { renderer.destroyGxmTexture(m_texBg); m_hasTexBg = false; }
    if (m_hasTexGlare) { renderer.destroyGxmTexture(m_texGlare); m_hasTexGlare = false; }
    if (m_hasTexScanlines) { renderer.destroyGxmTexture(m_texScanlines); m_hasTexScanlines = false; }
    if (m_hasTexVaultBoyHappy) { renderer.destroyGxmTexture(m_texVaultBoyHappy); m_hasTexVaultBoyHappy = false; }
    if (m_hasTexVaultBoyPain) { renderer.destroyGxmTexture(m_texVaultBoyPain); m_hasTexVaultBoyPain = false; }
    if (m_hasTexWeaponIcon) { renderer.destroyGxmTexture(m_texWeaponIcon); m_hasTexWeaponIcon = false; }
    if (m_hasTexFillbar) { renderer.destroyGxmTexture(m_texFillbar); m_hasTexFillbar = false; }

    renderer.destroyGxmMesh(m_uiMesh);
}

void PipBoy::showFeedback(const char* msg) {
    snprintf(m_feedbackMsg, sizeof(m_feedbackMsg), "%s", msg);
    m_feedbackTimer = 2.5f;
}

void PipBoy::update(const SceCtrlData& pad, uint32_t pressed, float deltaTime,
                    const char* currentCellName, float posX, float posY, float posZ, float yaw,
                    AudioSystem* audio) {
    m_currentCellName = currentCellName ? currentCellName : "Unknown";
    m_posX = posX; m_posY = posY; m_posZ = posZ; m_yaw = yaw;

    if (m_feedbackTimer > 0.0f) {
        m_feedbackTimer -= deltaTime;
        if (m_feedbackTimer < 0.0f) m_feedbackTimer = 0.0f;
    }

    // Flashlight & Pip-Boy toggle logic on Triangle:
    // Tap Triangle (< 0.40s) = Toggle Pip-Boy Screen
    // Hold Triangle (> 0.40s) = Toggle Flashlight Bulb
    if (pad.buttons & SCE_CTRL_TRIANGLE) {
        m_triangleHoldTime += deltaTime;
        if (m_triangleHoldTime > 0.40f && !m_flashlightToggledOnHold) {
            m_flashlightOn = !m_flashlightOn;
            if (audio) audio->play(m_flashlightOn ? SoundID::PipBoyLightOn : SoundID::PipBoyLightOff);
            showFeedback(m_flashlightOn ? "PIP-BOY LIGHT: ON" : "PIP-BOY LIGHT: OFF");
            m_flashlightToggledOnHold = true;
        }
    } else {
        if (m_triangleHoldTime > 0.04f && m_triangleHoldTime <= 0.40f) {
            m_isOpen = !m_isOpen;
            if (audio) audio->play(m_isOpen ? SoundID::PipBoyOpen : SoundID::PipBoyClose);
        }
        m_triangleHoldTime = 0.0f;
        m_flashlightToggledOnHold = false;
    }

    if (!m_isOpen) return;

    // Circle closes Pip-Boy
    if (pressed & SCE_CTRL_CIRCLE) {
        m_isOpen = false;
        if (audio) audio->play(SoundID::PipBoyClose);
        return;
    }

    // L / R Triggers: Cycle Main Tabs (STAT <-> ITEMS <-> DATA)
    if (pressed & SCE_CTRL_LTRIGGER) {
        int t = (int)m_mainTab - 1;
        if (t < 0) t = 2;
        m_mainTab = (PipBoyMainTab)t;
        m_subTab = 0;
        m_cursorIndex = 0;
        logMsg("Pip-Boy: main tab %d", t);
        if (audio) audio->play(SoundID::PipBoyTab);
    }
    if (pressed & SCE_CTRL_RTRIGGER) {
        int t = ((int)m_mainTab + 1) % 3;
        m_mainTab = (PipBoyMainTab)t;
        m_subTab = 0;
        m_cursorIndex = 0;
        logMsg("Pip-Boy: main tab %d", t);
        if (audio) audio->play(SoundID::PipBoyTab);
    }

    // Sub-tab counts
    int maxSubTabs = 3;
    if (m_mainTab == PipBoyMainTab::Items) maxSubTabs = 4;
    else if (m_mainTab == PipBoyMainTab::Data) maxSubTabs = 3;
    else maxSubTabs = 3;

    // D-Pad Left / Right: Cycle Sub-tabs
    if (pressed & SCE_CTRL_LEFT) {
        m_subTab = (m_subTab + maxSubTabs - 1) % maxSubTabs;
        m_cursorIndex = 0;
        if (audio) audio->play(SoundID::PipBoyTab);
    }
    if (pressed & SCE_CTRL_RIGHT) {
        m_subTab = (m_subTab + 1) % maxSubTabs;
        m_cursorIndex = 0;
        if (audio) audio->play(SoundID::PipBoyTab);
    }

    // List item count for cursor navigation
    int listCount = 0;
    if (m_mainTab == PipBoyMainTab::Items) {
        if (m_subTab == 0) listCount = (int)m_weapons.size();
        else if (m_subTab == 1) listCount = (int)m_apparel.size();
        else if (m_subTab == 2) listCount = (int)m_aid.size();
        else if (m_subTab == 3) listCount = (int)m_ammo.size();
    } else if (m_mainTab == PipBoyMainTab::Data && m_subTab == 2) {
        listCount = 3; // Radio stations
    }

    if(m_mainTab==PipBoyMainTab::Stat) listCount=m_subTab==1?7:(m_subTab==2?13:0);
    if (listCount > 0) {
        if (pressed & SCE_CTRL_UP) {
            m_cursorIndex = (m_cursorIndex + listCount - 1) % listCount;
            if (audio) audio->play(SoundID::PipBoySelect);
        }
        if (pressed & SCE_CTRL_DOWN) {
            m_cursorIndex = (m_cursorIndex + 1) % listCount;
            if (audio) audio->play(SoundID::PipBoySelect);
        }
    }

    // Cross: Activate / Equip / Consume
    if (pressed & SCE_CTRL_CROSS) {
        if (audio) audio->play(SoundID::PipBoySelect);
        if (m_mainTab == PipBoyMainTab::Items) {
            if (m_subTab == 0 && m_cursorIndex < (int)m_weapons.size()) {
                if (m_weapons[m_cursorIndex].weaponId < 0) {
                    showFeedback("THIS WEAPON IS NOT AVAILABLE YET");
                    return;
                }
                for (size_t w = 0; w < m_weapons.size(); ++w) {
                    m_weapons[w].isEquipped = ((int)w == m_cursorIndex);
                }
                char buf[64];
                snprintf(buf, sizeof(buf), "%s: EQUIPPED", m_weapons[m_cursorIndex].name.c_str());
                showFeedback(buf);
            } else if (m_subTab == 1 && m_cursorIndex < (int)m_apparel.size()) {
                m_apparel[m_cursorIndex].isEquipped = !m_apparel[m_cursorIndex].isEquipped;
                char buf[64];
                snprintf(buf, sizeof(buf), "%s: %s",
                         m_apparel[m_cursorIndex].name.c_str(),
                         m_apparel[m_cursorIndex].isEquipped ? "EQUIPPED" : "UNEQUIPPED");
                showFeedback(buf);
            } else if (m_subTab == 2 && m_cursorIndex < (int)m_aid.size()) {
                auto& item = m_aid[m_cursorIndex];
                if (item.count > 0 && item.healHp > 0 && m_playerHp < m_maxHp) {
                    int oldHp = m_playerHp;
                    m_playerHp = std::min(m_maxHp, m_playerHp + item.healHp);
                    int gained = m_playerHp - oldHp;
                    item.count--;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "+%d HP (%s USED)", gained, item.name.c_str());
                    showFeedback(buf);
                }
            }
        } else if (m_mainTab == PipBoyMainTab::Data && m_subTab == 2) {
            // Radio station toggle
            if (m_activeRadio == m_cursorIndex) {
                m_activeRadio = -1; // Turn off
                showFeedback("RADIO: OFF");
            } else {
                m_activeRadio = m_cursorIndex;
                const char* names[] = { "RADIO NEW VEGAS", "MOJAVE MUSIC RADIO", "BLACK MOUNTAIN RADIO" };
                char buf[64];
                snprintf(buf, sizeof(buf), "TUNED: %s", names[m_cursorIndex]);
                showFeedback(buf);
            }
        }
    }
}

// ============================================================================
// UI Geometry Batching Primitives
// ============================================================================

void PipBoy::resetBatch() {
    m_vertexCount = 0;
    m_indexCount = 0;
}

void PipBoy::addQuad(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (m_vertexCount + 4 > MAX_UI_VERTICES || m_indexCount + 6 > MAX_UI_INDICES) return;

    BasicVertex* verts = m_uiMesh.vertices;
    uint16_t* idxs     = m_uiMesh.indices;
    uint16_t base      = static_cast<uint16_t>(m_vertexCount);

    // 0: top-left
    verts[m_vertexCount].position[0] = x;
    verts[m_vertexCount].position[1] = y;
    verts[m_vertexCount].position[2] = 0.0f;
    verts[m_vertexCount].color[0] = r;
    verts[m_vertexCount].color[1] = g;
    verts[m_vertexCount].color[2] = b;
    verts[m_vertexCount].color[3] = a;
    verts[m_vertexCount].normal[0] = 0.0f; verts[m_vertexCount].normal[1] = 0.0f; verts[m_vertexCount].normal[2] = 1.0f;
    verts[m_vertexCount].uv[0] = 0.0f; verts[m_vertexCount].uv[1] = 0.0f;
    m_vertexCount++;

    // 1: top-right
    verts[m_vertexCount].position[0] = x + w;
    verts[m_vertexCount].position[1] = y;
    verts[m_vertexCount].position[2] = 0.0f;
    verts[m_vertexCount].color[0] = r;
    verts[m_vertexCount].color[1] = g;
    verts[m_vertexCount].color[2] = b;
    verts[m_vertexCount].color[3] = a;
    verts[m_vertexCount].normal[0] = 0.0f; verts[m_vertexCount].normal[1] = 0.0f; verts[m_vertexCount].normal[2] = 1.0f;
    verts[m_vertexCount].uv[0] = 0.0f; verts[m_vertexCount].uv[1] = 0.0f;
    m_vertexCount++;

    // 2: bottom-left
    verts[m_vertexCount].position[0] = x;
    verts[m_vertexCount].position[1] = y + h;
    verts[m_vertexCount].position[2] = 0.0f;
    verts[m_vertexCount].color[0] = r;
    verts[m_vertexCount].color[1] = g;
    verts[m_vertexCount].color[2] = b;
    verts[m_vertexCount].color[3] = a;
    verts[m_vertexCount].normal[0] = 0.0f; verts[m_vertexCount].normal[1] = 0.0f; verts[m_vertexCount].normal[2] = 1.0f;
    verts[m_vertexCount].uv[0] = 0.0f; verts[m_vertexCount].uv[1] = 0.0f;
    m_vertexCount++;

    // 3: bottom-right
    verts[m_vertexCount].position[0] = x + w;
    verts[m_vertexCount].position[1] = y + h;
    verts[m_vertexCount].position[2] = 0.0f;
    verts[m_vertexCount].color[0] = r;
    verts[m_vertexCount].color[1] = g;
    verts[m_vertexCount].color[2] = b;
    verts[m_vertexCount].color[3] = a;
    verts[m_vertexCount].normal[0] = 0.0f; verts[m_vertexCount].normal[1] = 0.0f; verts[m_vertexCount].normal[2] = 1.0f;
    verts[m_vertexCount].uv[0] = 0.0f; verts[m_vertexCount].uv[1] = 0.0f;
    m_vertexCount++;

    idxs[m_indexCount++] = base + 0;
    idxs[m_indexCount++] = base + 1;
    idxs[m_indexCount++] = base + 2;
    idxs[m_indexCount++] = base + 2;
    idxs[m_indexCount++] = base + 1;
    idxs[m_indexCount++] = base + 3;
}

void PipBoy::addTexturedQuad(float x, float y, float w, float h,
                             float u0, float v0, float u1, float v1,
                             float r, float g, float b, float a) {
    if (m_vertexCount + 4 > MAX_UI_VERTICES || m_indexCount + 6 > MAX_UI_INDICES) return;

    BasicVertex* verts = m_uiMesh.vertices + m_vertexCount;
    uint16_t* idxs     = m_uiMesh.indices + m_indexCount;

    // 0: top-left
    verts[0].position[0] = x;     verts[0].position[1] = y;     verts[0].position[2] = 0.0f;
    verts[0].color[0] = r;        verts[0].color[1] = g;        verts[0].color[2] = b;        verts[0].color[3] = a;
    verts[0].normal[0] = 0.0f;    verts[0].normal[1] = 0.0f;    verts[0].normal[2] = 1.0f;
    verts[0].uv[0] = u0;          verts[0].uv[1] = v0;

    // 1: top-right
    verts[1].position[0] = x + w; verts[1].position[1] = y;     verts[1].position[2] = 0.0f;
    verts[1].color[0] = r;        verts[1].color[1] = g;        verts[1].color[2] = b;        verts[1].color[3] = a;
    verts[1].normal[0] = 0.0f;    verts[1].normal[1] = 0.0f;    verts[1].normal[2] = 1.0f;
    verts[1].uv[0] = u1;          verts[1].uv[1] = v0;

    // 2: bottom-left
    verts[2].position[0] = x;     verts[2].position[1] = y + h; verts[2].position[2] = 0.0f;
    verts[2].color[0] = r;        verts[2].color[1] = g;        verts[2].color[2] = b;        verts[2].color[3] = a;
    verts[2].normal[0] = 0.0f;    verts[2].normal[1] = 0.0f;    verts[2].normal[2] = 1.0f;
    verts[2].uv[0] = u0;          verts[2].uv[1] = v1;

    // 3: bottom-right
    verts[3].position[0] = x + w; verts[3].position[1] = y + h; verts[3].position[2] = 0.0f;
    verts[3].color[0] = r;        verts[3].color[1] = g;        verts[3].color[2] = b;        verts[3].color[3] = a;
    verts[3].normal[0] = 0.0f;    verts[3].normal[1] = 0.0f;    verts[3].normal[2] = 1.0f;
    verts[3].uv[0] = u1;          verts[3].uv[1] = v1;

    idxs[0] = 0; idxs[1] = 1; idxs[2] = 2;
    idxs[3] = 2; idxs[4] = 1; idxs[5] = 3;

    m_vertexCount += 4;
    m_indexCount  += 6;
}

void PipBoy::drawTextured(GxmRenderer& renderer, const GxmTexture& tex,
                          float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          float r, float g, float b, float a) {
    if (!tex.isValid()) return;
    if (m_vertexCount + 4 > MAX_UI_VERTICES || m_indexCount + 6 > MAX_UI_INDICES) return;

    uint32_t startV = m_vertexCount;
    uint32_t startI = m_indexCount;
    addTexturedQuad(x, y, w, h, u0, v0, u1, v1, r, g, b, a);
    renderer.drawTextured2D(m_uiMesh.vertices + startV, 4,
                            m_uiMesh.indices + startI, 6,
                            &tex);
}

void PipBoy::addFrame(float x, float y, float w, float h, float thickness, float r, float g, float b, float a) {
    addQuad(x, y, w, thickness, r, g, b, a); // Top
    addQuad(x, y + h - thickness, w, thickness, r, g, b, a); // Bottom
    addQuad(x, y + thickness, thickness, h - 2.0f * thickness, r, g, b, a); // Left
    addQuad(x + w - thickness, y + thickness, thickness, h - 2.0f * thickness, r, g, b, a); // Right
}

void PipBoy::addChar(char c, float x, float y, float scale, float r, float g, float b, float a) {
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
                while (col < 8 && ((bits >> (7 - col)) & 1)) {
                    col++;
                }
                int spanLen = col - startCol;
                addQuad(x + startCol * scale, y + row * scale,
                        spanLen * scale, scale,
                        r, g, b, a);
            } else {
                col++;
            }
        }
    }
}

void PipBoy::addString(const char* text, float x, float y, float scale, float r, float g, float b, float a) {
    if (m_font.valid()) { m_font.text(text, x, y, scale*14.0f, r,g,b,a); return; }
    if (!text) return;
    float curX = x;
    while (*text) {
        if (*text == '\n') {
            y += (8.0f * scale) + 4.0f;
            curX = x;
        } else {
            addChar(*text, curX, y, scale, r, g, b, a);
            curX += (8.0f * scale) + (1.0f * scale);
        }
        text++;
    }
}

void PipBoy::addBar(float x, float y, float w, float h, float fillFraction, float r, float g, float b, float a) {
    if (fillFraction < 0.0f) fillFraction = 0.0f;
    if (fillFraction > 1.0f) fillFraction = 1.0f;

    // Outer frame
    addFrame(x, y, w, h, 1.5f, r * 0.7f, g * 0.7f, b * 0.7f, a);

    // Inner filled segment
    float innerW = (w - 4.0f) * fillFraction;
    if (innerW > 1.0f) {
        addQuad(x + 2.0f, y + 2.0f, innerW, h - 4.0f, r, g, b, a);
    }
}

// ============================================================================
// Render Screens
// ============================================================================

void PipBoy::renderBezelAndScanlines() {
    addQuad(0,0,960,544,0.015f,0.025f,0.015f,1);
}

void PipBoy::renderHeader() {
    // Positions follow menus/main/stats_menu.xml (1024 x 768 logical canvas).
    const char* titles[] = { "STATS", "ITEMS", "DATA" };
    addQuad(47,35,47,1,1,.72f,.18f,1);
    addString(titles[(int)m_mainTab],112,29,1.6f,1,.72f,.18f,1);
    addQuad(235,35,12,1,1,.72f,.18f,1);
    char info[128];
    if (m_mainTab==PipBoyMainTab::Stat) {
        snprintf(info,sizeof(info),"LVL %d     HP %d/%d     AP %d/%d     XP %d/%d",m_playerLevel,m_playerHp,m_maxHp,m_playerAp,m_maxAp,m_playerXp,m_nextLevelXp);
    } else snprintf(info,sizeof(info),"WG %.0f/%.0f       HP %d/%d       CAPS %d",m_weight,m_maxWeight,m_playerHp,m_maxHp,m_caps);
    addString(info,253,29,1.05f,1,.72f,.18f,1);

}

void PipBoy::renderFooter() {
    const char* stat[] = { "Status", "S.P.E.C.I.A.L.", "Skills" };
    const char* items[] = { "Weapons", "Apparel", "Aid", "Ammo" };
    const char* data[] = { "Local Map", "Quests", "Radio" };
    const char** labels=m_mainTab==PipBoyMainTab::Stat?stat:(m_mainTab==PipBoyMainTab::Items?items:data);
    int count=m_mainTab==PipBoyMainTab::Items?4:3;
    addQuad(47,462,12,1,1,.72f,.18f,1);
    for(int i=0;i<count;++i) {
        float x=80+i*(740.0f/count);
        if (i==m_subTab) addFrame(x-8,447,170,30,1,1,.72f,.18f,1);
        addString(labels[i],x,451,1.25f,1,.72f,.18f,i==m_subTab?1:.55f);
    }
    // The physical device buttons are exposed here while the 3D Pip-Boy is unsupported.
    const char* main[]={"STATS", "ITEMS", "DATA"};
    for (int i=0;i<3;++i) addString(main[i],310+i*135,506,1.2f,1,.72f,.18f,i==(int)m_mainTab?1:.45f);
    if (m_feedbackTimer>0) addString(m_feedbackMsg,90,420,1.1f,1,.72f,.18f,1);

}

void PipBoy::renderStatScreen() {
    if(m_subTab==0) {
        addString("CND",60,92,1.35f,1,.72f,.18f,1);
        addString("RAD",60,127,1.35f,1,.72f,.18f,.5f);
        addString("EFF",60,162,1.35f,1,.72f,.18f,.5f);
        return;
    }
    const char* special[]={"Strength","Perception","Endurance","Charisma","Intelligence","Agility","Luck"};
    const int values[]={6,5,7,4,8,6,7};
    const char* skills[]={"Barter","Energy Weapons","Explosives","Guns","Lockpick","Medicine","Melee Weapons","Repair","Science","Sneak","Speech","Survival","Unarmed"};
    const int skillValues[]={35,40,30,65,50,45,35,55,50,45,45,40,30};
    int count=m_subTab==1?7:13;
    int first=std::max(0,m_cursorIndex-8);
    for(int i=first;i<std::min(count,first+9);++i) {
        float y=100+(i-first)*34;
        if(i==m_cursorIndex) addFrame(55,y-3,315,28,1,1,.72f,.18f,1);
        addString(m_subTab==1?special[i]:skills[i],65,y,1.35f,1,.72f,.18f,1);
        char value[12];snprintf(value,sizeof(value),"%d",m_subTab==1?values[i]:skillValues[i]);
        addString(value,330,y,1.35f,1,.72f,.18f,1);
    }
}

void PipBoy::renderItemsScreen() {
    const std::vector<InventoryItem>* lists[]={&m_weapons,&m_apparel,&m_aid,&m_ammo};
    const auto& items=*lists[m_subTab];
    for(size_t i=0;i<items.size();++i) {
        float y=110+i*33; const auto& it=items[i];
        if((int)i==m_cursorIndex) addFrame(55,y-3,410,29,1,1,.72f,.18f,1);
        char text[96]; snprintf(text,sizeof(text),"%s%s%s",it.isEquipped?". ":"  ",it.name.c_str(),it.count>1?"":"");
        addString(text,65,y,1.35f,1,.72f,.18f,1);
    }
    if(m_cursorIndex>=0 && m_cursorIndex<(int)items.size()) {
        const auto& it=items[m_cursorIndex]; char text[128];
        snprintf(text,sizeof(text),"%s %d       WG %.1f       VAL %d",m_subTab==0?"DAM":"DT",it.damageOrDt,it.weight,it.value);
        addString(text,510,350,1.15f,1,.72f,.18f,1);
        if(m_subTab<2) {
            addString("CND",510,386,1.15f,1,.72f,.18f,1);
            addBar(560,390,225,8,it.condition/100.f,1,.72f,.18f,1);
        }
    }

}

void PipBoy::renderDataScreen(const char* currentCellName, float posX, float posY, float posZ, float yaw) {
    (void)posX; (void)posY; (void)posZ; (void)yaw;
    if(m_subTab==0) {
        addString(currentCellName,65,92,1.4f,1,.72f,.18f,1);
        addString("Local map unavailable",325,260,1.2f,1,.72f,.18f,.65f);
    } else if(m_subTab==1) {
        addString("No active quest data",65,110,1.3f,1,.72f,.18f,.65f);
    } else {
        const char* names[]={"Radio New Vegas","Mojave Music Radio","Black Mountain Radio"};
        for(int i=0;i<3;++i) {
            float y=110+i*40;
            if(i==m_cursorIndex) addFrame(55,y-3,410,29,1,1,.72f,.18f,1);
            addString(names[i],65,y,1.35f,1,.72f,.18f,1);
        }
    }

}

void PipBoy::render(GxmRenderer& renderer) {
    if (!m_isOpen || !m_uiMesh.isValid()) return;
    resetBatch(); m_font.clear();
    renderBezelAndScanlines();
    renderer.draw2D(m_uiMesh.vertices,m_vertexCount,m_uiMesh.indices,m_indexCount); resetBatch();
    if(m_hasTexBg) drawTextured(renderer,m_texBg,0,0,960,544,0,0,1,1,1,.72f,.18f,.35f);
    resetBatch();
    renderHeader();
    if(m_mainTab==PipBoyMainTab::Stat) renderStatScreen();
    else if(m_mainTab==PipBoyMainTab::Items) renderItemsScreen();
    else renderDataScreen(m_currentCellName.c_str(),m_posX,m_posY,m_posZ,m_yaw);
    renderFooter();
    if(m_indexCount) renderer.draw2D(m_uiMesh.vertices,m_vertexCount,m_uiMesh.indices,m_indexCount);
    if(m_mainTab==PipBoyMainTab::Stat && m_subTab==0) {
        // Original stats_player_head hierarchy and zoom=145, from stats_menu.xml.
        const float sx=960.f/1024, sy=544.f/768, z=1.45f;
        const float hx=50+(855-85*z)/2, hy=150, tx=hx-14*z, ty=hy+86*z;
        float rects[7][4]={{hx,hy,85*z,92*z},{hx+21,hy+(92*z-64*z)/2+16*z,48*z,64*z},
            {tx,ty,102*z,128*z},{tx+90*z,ty,100*z,52*z},{tx-88*z,ty-5*z,96*z,54*z},
            {tx+47*z,ty+86*z,72*z,112*z},{tx-37*z,ty+83*z,84*z,112*z}};
        for(int i=0;i<7;++i) if(m_hasStatus[i]) drawTextured(renderer,m_statusTextures[i],rects[i][0]*sx,rects[i][1]*sy,rects[i][2]*sx,rects[i][3]*sy,0,0,1,1,1,.72f,.18f,1);
        float offsets[6][2]={{(85*z-128)/2,-43},{(85*z-128)/2,92*z},{150,44},{-190,44},{150,285},{-190,285}};
        resetBatch();
        for(int i=0;i<6;++i) {
            int kind=i<2?0:(i%2?2:1); float width=kind==0?128:256;
            float x=(hx+offsets[i][0])*sx,y=(hy+offsets[i][1])*sy;
            if(m_hasGauge[kind]) drawTextured(renderer,m_gaugeTextures[kind],x,y,width*sx,38*sy,0,0,1,1,1,.72f,.18f,1);
        }
        resetBatch();
        for(int i=0;i<6;++i) {
            int kind=i<2?0:(i%2?2:1);
            float x=(hx+offsets[i][0]+(kind==1?51:25))*sx,y=(hy+offsets[i][1]+24)*sy;
            addQuad(x,y,77*sx,14*sy,1,.72f,.18f,1);
        }
        if(m_indexCount) renderer.draw2D(m_uiMesh.vertices,m_vertexCount,m_uiMesh.indices,m_indexCount);

    }
    if(m_mainTab==PipBoyMainTab::Stat && m_subTab==1 && m_cursorIndex>=0 && m_cursorIndex<7 && m_hasSpecial[m_cursorIndex])
        drawTextured(renderer,m_specialTextures[m_cursorIndex],490,115,280,245,0,0,1,1,1,.72f,.18f,1);
    // The pistol icon is used only for the pistol; other icons must come from their own records.
    if(m_mainTab==PipBoyMainTab::Items && m_subTab==0 && m_cursorIndex==0 && m_hasTexWeaponIcon)
        drawTextured(renderer,m_texWeaponIcon,550,115,220,180,0,0,1,1,1,.72f,.18f,1);
    m_font.flush(renderer);

}
