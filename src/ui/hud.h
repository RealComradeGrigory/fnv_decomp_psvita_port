#pragma once
#include "ui/original_font.h"

#include <string>
#include <vector>
#include "../renderer/gxm_renderer.h"
#include "../bsa/bsa_reader.h"

class HUD {
public:
    HUD() = default;
    ~HUD() = default;

    bool init(BSAReader* texBsa1, BSAReader* texBsa2, GxmRenderer& renderer);
    void cleanup(GxmRenderer& renderer);

    // Update HUD state (compass yaw, health, AP, notification timers)
    void update(float deltaTime, float playerYaw, int hp, int maxHp, int ap, int maxAp,
                const char* targetPrompt = nullptr);

    // Show quest / location popup
    void showNotification(const char* title, const char* subtitle, float duration = 4.0f);

    // Render 2D HUD elements
    void render(GxmRenderer& renderer);

private:
    OriginalFont m_font;
    float m_playerYaw = 0.0f;
    int m_hp = 100;
    int m_maxHp = 100;
    int m_ap = 85;
    int m_maxAp = 85;

    std::string m_targetPrompt;

    std::string m_notifTitle;
    std::string m_notifSubtitle;
    float m_notifTimer = 0.0f;

    GxmMesh m_hudMesh;
    static const uint32_t MAX_HUD_VERTS = 4096;
    static const uint32_t MAX_HUD_INDICES = 6144;
    uint32_t m_vertCount = 0;
    uint32_t m_indexCount = 0;

    // Authentic Bethesda HUD textures
    GxmTexture m_texCompass;
    bool m_hasTexCompass = false;

    GxmTexture m_texCrosshair;
    bool m_hasTexCrosshair = false;

    GxmTexture m_texSepLeft;
    bool m_hasTexSepLeft = false;

    GxmTexture m_texSepRight;
    bool m_hasTexSepRight = false;

    GxmTexture m_texFillbar;
    bool m_hasTexFillbar = false;

    void addQuad(float x, float y, float w, float h, float r, float g, float b, float a);
    void addTexturedQuad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r, float g, float b, float a);
    void addFrame(float x, float y, float w, float h, float th, float r, float g, float b, float a);
    void addChar(char c, float x, float y, float scale, float r, float g, float b, float a);
    void addString(const char* text, float x, float y, float scale, float r, float g, float b, float a);
    void addBar(float x, float y, float w, float h, float fraction, float r, float g, float b, float a);
};
