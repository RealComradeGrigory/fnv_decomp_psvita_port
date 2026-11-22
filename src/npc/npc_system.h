#pragma once
#include "ui/original_font.h"

#include <string>
#include <vector>
#include "../renderer/gxm_renderer.h"
#include "../bsa/bsa_reader.h"
#include "../audio/audio_system.h"

struct DialogueOption {
    const char* text;
    const char* response;
    int nextNode;
};

struct DialogueNode {
    int id;
    const char* npcPrompt;
    int continueNode = 0;
    std::vector<DialogueOption> options;
};

struct NPC {
    std::string id;
    std::string name;
    float posX, posY, posZ;
    float yaw, pitch;
    float defaultYaw;

    // Meshes and textures
    GxmMesh bodyMesh;
    GxmTexture bodyTex;
    bool hasBodyMesh = false;
    bool hasBodyTex = false;

    std::vector<GxmMesh> faceParts;
    std::vector<GxmMesh> facialHairParts;
    GxmMesh headMesh;
    GxmTexture headTex;
    bool hasHeadMesh = false;
    bool hasHeadTex = false;

    GxmMesh hairMesh;
    GxmTexture hairTex;
    bool hasHairMesh = false;
    bool hasHairTex = false;

    GxmMesh glassesMesh;
    GxmTexture glassesTex;
    bool hasGlassesMesh = false;
    bool hasGlassesTex = false;

    GxmMesh rightHandMesh;
    GxmTexture rightHandTex;
    bool hasRightHandMesh = false;
    bool hasRightHandTex = false;

    GxmMesh leftHandMesh;
    GxmTexture leftHandTex;
    bool hasLeftHandMesh = false;
    bool hasLeftHandTex = false;

    // Animation & Look-at state
    float animTimer = 0.0f;
    float lookPitch = 0.0f;
    float breathOffsetZ = 0.0f;

    // State
    bool isTalking = false;
    int currentDialogueNode = 0;
    int selectedOption = 0;
    bool greetedPlayer = false;
};

class NpcSystem {
public:
    NpcSystem() = default;
    ~NpcSystem();

    bool init(GxmRenderer& renderer);
    void cleanup(GxmRenderer& renderer);

    // Spawn Doc Mitchell inside Doc Mitchell's house
    bool spawnDocMitchell(BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2, GxmRenderer& renderer);

    // Update AI: look-at player, proximity checks, dialogue logic
    void update(float deltaTime, float playerX, float playerY, float playerZ,
                uint32_t buttons, uint32_t pressed, AudioSystem& audio);

    // Render all NPCs in the world
    void render(GxmRenderer& renderer);

    // Render active dialogue UI if talking to an NPC
    void renderDialogue(GxmRenderer& renderer);

    // Check if player is near any NPC
    bool hasNearbyNpc() const { return m_nearbyNpcIndex >= 0; }
    const NPC* getNearbyNpc() const;
    bool isInDialogue() const { return m_activeDialogueNpcIndex >= 0; }

private:
    OriginalFont m_font;
    std::vector<NPC> m_npcs;
    int m_nearbyNpcIndex = -1;
    int m_activeDialogueNpcIndex = -1;

    // Dialogue database
    std::vector<DialogueNode> m_dialogueNodes;
    void initDialogueNodes();

    // 2D UI geometry for dialogue box
    GxmMesh m_dialogueMesh;
    static const uint32_t MAX_UI_VERTS = 4096;
    static const uint32_t MAX_UI_INDICES = 6144;
    uint32_t m_uiVertCount = 0;
    uint32_t m_uiIndexCount = 0;

    void addUiQuad(float x, float y, float w, float h, float r, float g, float b, float a);
    void addUiFrame(float x, float y, float w, float h, float thickness, float r, float g, float b, float a);
    void addUiChar(char c, float x, float y, float scale, float r, float g, float b, float a);
    void addUiString(const char* text, float x, float y, float scale, float r, float g, float b, float a);
};
