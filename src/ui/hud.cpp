#include "hud.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "../pipboy/pipboy_font.h"
#include "../dds/dds_loader.h"

static const float AMBER_COLOR[3] = { 1.0f, 0.72f, 0.18f };
static const float AMBER_BG[4]    = { 0.08f, 0.06f, 0.02f, 0.75f };

static bool loadHudTexture(BSAReader* texBsa1, BSAReader* texBsa2, const char* folder, const char* file,
                           GxmRenderer& renderer, GxmTexture& outTex) {
    std::vector<uint8_t> ddsData;
    bool found = false;
    if (texBsa2 && texBsa2->isOpen() && texBsa2->extractFile(folder, file, ddsData)) {
        found = true;
    } else if (texBsa1 && texBsa1->isOpen() && texBsa1->extractFile(folder, file, ddsData)) {
        found = true;
    }
    if (!found) return false;

    DDSLoader loader;
    if (!loader.load(ddsData.data(), ddsData.size())) return false;
    return renderer.createGxmTexture(loader.getTexture(), outTex);
}

bool HUD::init(BSAReader* texBsa1, BSAReader* texBsa2, GxmRenderer& renderer) {
    m_font.init(renderer, texBsa1, texBsa2);
    if (!renderer.allocateDynamicMesh(MAX_HUD_VERTS, MAX_HUD_INDICES, m_hudMesh)) {
        return false;
    }

    // Load authentic Fallout New Vegas UI textures from Textures BSA
    if (loadHudTexture(texBsa1, texBsa2, "textures\\interface\\hud", "hud_comp_direction_strip.dds", renderer, m_texCompass)) {
        m_hasTexCompass = true;
    }
    if (loadHudTexture(texBsa1, texBsa2, "textures\\interface\\hud", "crosshair.dds", renderer, m_texCrosshair)) {
        m_hasTexCrosshair = true;
    }
    if (loadHudTexture(texBsa1, texBsa2, "textures\\interface\\hud", "hud_left_seperator.dds", renderer, m_texSepLeft)) {
        m_hasTexSepLeft = true;
    }
    if (loadHudTexture(texBsa1, texBsa2, "textures\\interface\\hud", "hud_right_seperator.dds", renderer, m_texSepRight)) {
        m_hasTexSepRight = true;
    }
    if (loadHudTexture(texBsa1, texBsa2, "textures\\interface\\shared\\meter", "fillbar.dds", renderer, m_texFillbar)) {
        m_hasTexFillbar = true;
    }

    return true;
}

void HUD::cleanup(GxmRenderer& renderer) {
    m_font.cleanup(renderer);
    if (m_hasTexCompass)   { renderer.destroyGxmTexture(m_texCompass);   m_hasTexCompass = false; }
    if (m_hasTexCrosshair) { renderer.destroyGxmTexture(m_texCrosshair); m_hasTexCrosshair = false; }
    if (m_hasTexSepLeft)   { renderer.destroyGxmTexture(m_texSepLeft);   m_hasTexSepLeft = false; }
    if (m_hasTexSepRight)  { renderer.destroyGxmTexture(m_texSepRight);  m_hasTexSepRight = false; }
    if (m_hasTexFillbar)   { renderer.destroyGxmTexture(m_texFillbar);   m_hasTexFillbar = false; }

    renderer.destroyGxmMesh(m_hudMesh);
}

void HUD::update(float deltaTime, float playerYaw, int hp, int maxHp, int ap, int maxAp,
                 const char* targetPrompt) {
    m_playerYaw = playerYaw;
    m_hp = hp;
    m_maxHp = maxHp;
    m_ap = ap;
    m_maxAp = maxAp;

    m_targetPrompt = targetPrompt ? targetPrompt : "";

    if (m_notifTimer > 0.0f) {
        m_notifTimer -= deltaTime;
    }
}

void HUD::showNotification(const char* title, const char* subtitle, float duration) {
    m_notifTitle = title ? title : "";
    m_notifSubtitle = subtitle ? subtitle : "";
    m_notifTimer = duration;
}

void HUD::addQuad(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (m_vertCount + 4 > MAX_HUD_VERTS || m_indexCount + 6 > MAX_HUD_INDICES) return;

    BasicVertex* v = &m_hudMesh.vertices[m_vertCount];
    uint16_t* idx  = &m_hudMesh.indices[m_indexCount];
    uint16_t base  = (uint16_t)m_vertCount;

    v[0].position[0] = x;     v[0].position[1] = y;     v[0].position[2] = 0.0f;
    v[1].position[0] = x + w; v[1].position[1] = y;     v[1].position[2] = 0.0f;
    v[2].position[0] = x;     v[2].position[1] = y + h; v[2].position[2] = 0.0f;
    v[3].position[0] = x + w; v[3].position[1] = y + h; v[3].position[2] = 0.0f;

    for (int i = 0; i < 4; ++i) {
        v[i].color[0] = r; v[i].color[1] = g; v[i].color[2] = b; v[i].color[3] = a;
        v[i].normal[0] = 0.0f; v[i].normal[1] = 0.0f; v[i].normal[2] = 1.0f;
        v[i].uv[0] = 0.0f; v[i].uv[1] = 0.0f;
    }

    idx[0] = base + 0; idx[1] = base + 1; idx[2] = base + 2;
    idx[3] = base + 1; idx[4] = base + 3; idx[5] = base + 2;

    m_vertCount += 4;
    m_indexCount += 6;
}

void HUD::addTexturedQuad(float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          float r, float g, float b, float a) {
    if (m_vertCount + 4 > MAX_HUD_VERTS || m_indexCount + 6 > MAX_HUD_INDICES) return;

    BasicVertex* v = &m_hudMesh.vertices[m_vertCount];
    uint16_t* idx  = &m_hudMesh.indices[m_indexCount];
    uint16_t base  = (uint16_t)m_vertCount;

    v[0].position[0] = x;     v[0].position[1] = y;     v[0].position[2] = 0.0f;
    v[0].uv[0] = u0;          v[0].uv[1] = v0;

    v[1].position[0] = x + w; v[1].position[1] = y;     v[1].position[2] = 0.0f;
    v[1].uv[0] = u1;          v[1].uv[1] = v0;

    v[2].position[0] = x;     v[2].position[1] = y + h; v[2].position[2] = 0.0f;
    v[2].uv[0] = u0;          v[2].uv[1] = v1;

    v[3].position[0] = x + w; v[3].position[1] = y + h; v[3].position[2] = 0.0f;
    v[3].uv[0] = u1;          v[3].uv[1] = v1;

    for (int i = 0; i < 4; ++i) {
        v[i].color[0] = r; v[i].color[1] = g; v[i].color[2] = b; v[i].color[3] = a;
        v[i].normal[0] = 0.0f; v[i].normal[1] = 0.0f; v[i].normal[2] = 1.0f;
    }

    idx[0] = base + 0; idx[1] = base + 1; idx[2] = base + 2;
    idx[3] = base + 1; idx[4] = base + 3; idx[5] = base + 2;

    m_vertCount += 4;
    m_indexCount += 6;
}

void HUD::addFrame(float x, float y, float w, float h, float th, float r, float g, float b, float a) {
    addQuad(x, y, w, th, r, g, b, a);
    addQuad(x, y + h - th, w, th, r, g, b, a);
    addQuad(x, y + th, th, h - 2.0f * th, r, g, b, a);
    addQuad(x + w - th, y + th, th, h - 2.0f * th, r, g, b, a);
}

void HUD::addChar(char c, float x, float y, float scale, float r, float g, float b, float a) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = g_pipboyFont8x8[c - 32];

    for (int row = 0; row < 8; ++row) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (0x80 >> col)) {
                addQuad(x + col * scale, y + row * scale, scale, scale, r, g, b, a);
            }
        }
    }
}

void HUD::addString(const char* text, float x, float y, float scale, float r, float g, float b, float a) {
    if (m_font.valid()) { m_font.text(text, x, y, scale*14.0f, r,g,b,a); return; }
    if (!text) return;
    float curX = x;
    while (*text) {
        char c = *text++;
        if (c == '\n') {
            y += 10.0f * scale;
            curX = x;
        } else {
            addChar(c, curX, y, scale, r, g, b, a);
            curX += 9.0f * scale;
        }
    }
}

void HUD::addBar(float x, float y, float w, float h, float fraction, float r, float g, float b, float a) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    addQuad(x, y, w, h, AMBER_BG[0], AMBER_BG[1], AMBER_BG[2], 0.6f);
    addFrame(x, y, w, h, 1.2f, r, g, b, a * 0.7f);
    if (fraction > 0.01f) {
        addQuad(x + 2.0f, y + 2.0f, (w - 4.0f) * fraction, h - 4.0f, r, g, b, a);
    }
}

void HUD::render(GxmRenderer& renderer) {
    if (!m_hudMesh.isValid()) return;
    // 1. Draw Authentic HUD Frame Brackets (Left & Right)
    if (m_hasTexSepLeft) {
        m_vertCount = 0; m_indexCount = 0;
        addTexturedQuad(25.0f, 430.0f, 220.0f, 100.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                        AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 0.85f);
        renderer.drawTextured2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount, &m_texSepLeft);
    }

    if (m_hasTexSepRight) {
        m_vertCount = 0; m_indexCount = 0;
        addTexturedQuad(715.0f, 430.0f, 220.0f, 100.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                        AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 0.85f);
        renderer.drawTextured2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount, &m_texSepRight);
    }

    // 3. Draw Authentic Sliding Compass Strip
    const float cmpX = 35.0f;
    const float cmpY = 495.0f;
    const float cmpW = 200.0f;
    const float cmpH = 22.0f;

    if (m_hasTexCompass) {
        m_vertCount = 0; m_indexCount = 0;
        // Background window
        addQuad(cmpX, cmpY, cmpW, cmpH, AMBER_BG[0], AMBER_BG[1], AMBER_BG[2], 0.65f);
        renderer.draw2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount);

        // Yaw fraction [0..1]
        float yawFrac = m_playerYaw / (2.0f * 3.14159265f);
        while (yawFrac < 0.0f)  yawFrac += 1.0f;
        while (yawFrac >= 1.0f) yawFrac -= 1.0f;

        float uWidth = 0.18f; // ~65 degree viewing angle window
        float u0 = yawFrac - uWidth * 0.5f;
        float u1 = yawFrac + uWidth * 0.5f;

        m_vertCount = 0; m_indexCount = 0;
        addTexturedQuad(cmpX + 4.0f, cmpY + 2.0f, cmpW - 8.0f, cmpH - 4.0f,
                        u0, 0.0f, u1, 1.0f,
                        AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 0.95f);
        renderer.drawTextured2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount, &m_texCompass);

        // Center indicator tick
        m_vertCount = 0; m_indexCount = 0;
        addQuad(cmpX + cmpW * 0.5f - 1.0f, cmpY + cmpH - 7.0f, 2.0f, 6.0f,
                AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 1.0f);
        renderer.draw2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount);
    }

    // 4. HP Bar and AP Bar with Authentic Fillbar Texture
    const float hpX = 35.0f;
    const float hpY = 458.0f;
    const float hpW = 160.0f;
    const float hpH = 14.0f;

    float hpFrac = (float)m_hp / (float)m_maxHp;
    if (hpFrac < 0.0f) hpFrac = 0.0f;
    if (hpFrac > 1.0f) hpFrac = 1.0f;

    const float apX = 765.0f;
    const float apY = 440.0f;
    const float apW = 160.0f;
    const float apH = 14.0f;

    float apFrac = (float)m_ap / (float)m_maxAp;
    if (apFrac < 0.0f) apFrac = 0.0f;
    if (apFrac > 1.0f) apFrac = 1.0f;

    if (m_hasTexFillbar) {
        // Draw HP Fillbar
        m_vertCount = 0; m_indexCount = 0;
        addQuad(hpX, hpY, hpW, hpH, AMBER_BG[0], AMBER_BG[1], AMBER_BG[2], 0.6f);
        addFrame(hpX, hpY, hpW, hpH, 1.2f, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 0.75f);
        renderer.draw2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount);

        if (hpFrac > 0.01f) {
            m_vertCount = 0; m_indexCount = 0;
            addTexturedQuad(hpX + 2.0f, hpY + 2.0f, (hpW - 4.0f) * hpFrac, hpH - 4.0f,
                            0.0f, 0.0f, hpFrac, 1.0f,
                            AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 1.0f);
            renderer.drawTextured2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount, &m_texFillbar);
        }

        // Draw AP Fillbar
        m_vertCount = 0; m_indexCount = 0;
        addQuad(apX, apY, apW, apH, AMBER_BG[0], AMBER_BG[1], AMBER_BG[2], 0.6f);
        addFrame(apX, apY, apW, apH, 1.2f, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 0.75f);
        renderer.draw2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount);

        if (apFrac > 0.01f) {
            m_vertCount = 0; m_indexCount = 0;
            addTexturedQuad(apX + 2.0f, apY + 2.0f, (apW - 4.0f) * apFrac, apH - 4.0f,
                            0.0f, 0.0f, apFrac, 1.0f,
                            AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 1.0f);
            renderer.drawTextured2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount, &m_texFillbar);
        }
    } else {
        m_vertCount = 0; m_indexCount = 0;
        addBar(hpX, hpY, hpW, hpH, hpFrac, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 1.0f);
        addBar(apX, apY, apW, apH, apFrac, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 1.0f);
        renderer.draw2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount);
    }

    // 5. Draw HUD Text (HP / AP labels, interaction prompt & notifications)
    m_vertCount = 0; m_indexCount = 0;

    addString("HP", hpX, hpY - 14.0f, 1.3f, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 1.0f);
    char hpStr[16];
    snprintf(hpStr, sizeof(hpStr), "%d/%d", m_hp, m_maxHp);
    addString(hpStr, hpX + hpW - 65.0f, hpY - 14.0f, 1.2f, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 0.9f);

    addString("AP", apX + apW - 25.0f, apY - 14.0f, 1.3f, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 1.0f);
    char apStr[16];
    snprintf(apStr, sizeof(apStr), "%d/%d", m_ap, m_maxAp);
    addString(apStr, apX + 10.0f, apY - 14.0f, 1.2f, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 0.9f);

    // Target Interaction Prompt
    if (!m_targetPrompt.empty()) {
        float promptLen = m_font.valid() ? m_font.width(m_targetPrompt.c_str(), 1.3f*14.0f) : (float)m_targetPrompt.size()*9.0f*1.3f;
        float px = 480.0f - promptLen * 0.5f;
        float py = 320.0f;

        addString(m_targetPrompt.c_str(), px, py, 1.3f, AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], 1.0f);
    }

    // Quest / Location Notification Banner
    if (m_notifTimer > 0.0f && !m_notifTitle.empty()) {
        float alpha = (m_notifTimer > 0.5f) ? 1.0f : (m_notifTimer / 0.5f);
        const float nx = 220.0f;
        const float ny = 35.0f;


        addString(m_notifTitle.c_str(), nx + 20.0f, ny + 8.0f, 1.35f,
                  AMBER_COLOR[0], AMBER_COLOR[1], AMBER_COLOR[2], alpha);
        if (!m_notifSubtitle.empty()) {
            addString(m_notifSubtitle.c_str(), nx + 20.0f, ny + 28.0f, 1.15f,
                      0.90f, 0.85f, 0.70f, 0.9f * alpha);
        }
    }

    if (m_indexCount > 0) {
        renderer.draw2D(m_hudMesh.vertices, m_vertCount, m_hudMesh.indices, m_indexCount);
    }
    m_font.flush(renderer);
}
