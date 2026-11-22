#include "npc_system.h"
#include "head_attachments.h"
#include "nif/packaged_asset.h"
#include <psp2/ctrl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "../nif/nif_loader.h"
#include "../dds/dds_loader.h"
#include "../pipboy/pipboy_font.h"

extern void logMsg(const char* fmt, ...);

static bool extractDds(BSAReader& texBsa1, BSAReader* texBsa2, const std::string& path, std::vector<uint8_t>& outData) {
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

NpcSystem::~NpcSystem() {
}

bool NpcSystem::init(GxmRenderer& renderer) {
    if (!renderer.allocateDynamicMesh(MAX_UI_VERTS, MAX_UI_INDICES, m_dialogueMesh)) {
        return false;
    }
    initDialogueNodes();
    return true;
}

void NpcSystem::cleanup(GxmRenderer& renderer) {
    m_font.cleanup(renderer);
    for (auto& npc : m_npcs) {
        for(auto& part:npc.faceParts) renderer.destroyGxmMesh(part);
        for(auto& part:npc.facialHairParts) renderer.destroyGxmMesh(part);
        if (npc.hasBodyTex) renderer.destroyGxmTexture(npc.bodyTex);
        if (npc.hasBodyMesh) renderer.destroyGxmMesh(npc.bodyMesh);
        if (npc.hasHeadTex) renderer.destroyGxmTexture(npc.headTex);
        if (npc.hasHeadMesh) renderer.destroyGxmMesh(npc.headMesh);
        if (npc.hasHairTex) renderer.destroyGxmTexture(npc.hairTex);
        if (npc.hasHairMesh) renderer.destroyGxmMesh(npc.hairMesh);
        if (npc.hasGlassesTex) renderer.destroyGxmTexture(npc.glassesTex);
        if (npc.hasGlassesMesh) renderer.destroyGxmMesh(npc.glassesMesh);
        if (npc.hasRightHandTex) renderer.destroyGxmTexture(npc.rightHandTex);
        if (npc.hasRightHandMesh) renderer.destroyGxmMesh(npc.rightHandMesh);
        if (npc.hasLeftHandTex) renderer.destroyGxmTexture(npc.leftHandTex);
        if (npc.hasLeftHandMesh) renderer.destroyGxmMesh(npc.leftHandMesh);
    }
    m_npcs.clear();
    m_nearbyNpcIndex = -1;
    m_activeDialogueNpcIndex = -1;
    renderer.destroyGxmMesh(m_dialogueMesh);
}

void NpcSystem::initDialogueNodes() {
    m_dialogueNodes.clear();
    // Topic list links the original INFO responses below; no quest scripts are executed here.
    DialogueNode n0; n0.id=0; n0.npcPrompt="";
    n0.options={{"What town is this?","",2},{"Do you know anything about the man that shot me?","",4},{"Who was it that rescued me?","",6},{"Tell me about yourself, Doc.","",9},{"What else can you tell me about Goodsprings?","",13},{"Goodbye.","",16}};
    m_dialogueNodes.push_back(n0);
    // Original FalloutNV.esm INFO 0x00107221; no quest scripts are executed here.
    DialogueNode n1; n1.id=1; n1.npcPrompt="I expect you'll be wanting to go outside after being cooped up for so long, but if you have any questions, I'll answer what I can.";
    n1.continueNode=0;
    m_dialogueNodes.push_back(n1);
    // Original FalloutNV.esm INFO 0x00107499; no quest scripts are executed here.
    DialogueNode n2; n2.id=2; n2.npcPrompt="This here's Goodsprings. Named after the water we got here, just down the road to the southeast. Goodsprings Source, they call it.";
    n2.continueNode=3;
    m_dialogueNodes.push_back(n2);
    // Original FalloutNV.esm INFO 0x00107499; no quest scripts are executed here.
    DialogueNode n3; n3.id=3; n3.npcPrompt="It's a quiet town, and that's how we like it. We don't go looking for trouble, though occasionally it sees fit to come looking for us.";
    n3.continueNode=0;
    m_dialogueNodes.push_back(n3);
    // Original FalloutNV.esm INFO 0x00107492; no quest scripts are executed here.
    DialogueNode n4; n4.id=4; n4.npcPrompt="I didn't see him or the men with him. You might ask around town, though. Could be someone saw which way they was headed.";
    n4.continueNode=5;
    m_dialogueNodes.push_back(n4);
    // Original FalloutNV.esm INFO 0x00107492; no quest scripts are executed here.
    DialogueNode n5; n5.id=5; n5.npcPrompt="Your best bet would probably be Trudy, the bartender at the saloon up the road. If anyone saw anything, she'd know about it.";
    n5.continueNode=0;
    m_dialogueNodes.push_back(n5);
    // Original FalloutNV.esm INFO 0x0010749E; no quest scripts are executed here.
    DialogueNode n6; n6.id=6; n6.npcPrompt="That'd be Victor. Curious fella. Sort of odd. And I don't just mean 'cause he's a robot. I couldn't tell you much about him.";
    n6.continueNode=7;
    m_dialogueNodes.push_back(n6);
    // Original FalloutNV.esm INFO 0x0010749E; no quest scripts are executed here.
    DialogueNode n7; n7.id=7; n7.npcPrompt="He's real friendly, don't get me wrong. You just get the sense that ain't the whole picture. Just a feeling.";
    n7.continueNode=8;
    m_dialogueNodes.push_back(n7);
    // Original FalloutNV.esm INFO 0x0010749E; no quest scripts are executed here.
    DialogueNode n8; n8.id=8; n8.npcPrompt="Keeps to himself, mostly. You want to know more about him, you'll have to ask him yourself. He has a shack on the southern edge of town.";
    n8.continueNode=0;
    m_dialogueNodes.push_back(n8);
    // Original FalloutNV.esm INFO 0x00107491; no quest scripts are executed here.
    DialogueNode n9; n9.id=9; n9.npcPrompt="Well, I already told you I came from a vault. After that, was a traveling doctor for a spell.";
    n9.continueNode=10;
    m_dialogueNodes.push_back(n9);
    // Original FalloutNV.esm INFO 0x00107491; no quest scripts are executed here.
    DialogueNode n10; n10.id=10; n10.npcPrompt="Seemed like a good idea at the time. Most folks out here ain't educated, so people with medical knowhow are hard to come by.";
    n10.continueNode=11;
    m_dialogueNodes.push_back(n10);
    // Original FalloutNV.esm INFO 0x00107491; no quest scripts are executed here.
    DialogueNode n11; n11.id=11; n11.npcPrompt="Found that I could help a lot of people with what I knew, and that was all right with me.";
    n11.continueNode=12;
    m_dialogueNodes.push_back(n11);
    // Original FalloutNV.esm INFO 0x00107491; no quest scripts are executed here.
    DialogueNode n12; n12.id=12; n12.npcPrompt="Eventually, I went back and married my childhood sweetheart and that was the end of my traveling days. Didn't miss it none then. Still don't.";
    n12.continueNode=0;
    m_dialogueNodes.push_back(n12);
    // Original FalloutNV.esm INFO 0x00107493; no quest scripts are executed here.
    DialogueNode n13; n13.id=13; n13.npcPrompt="Well, there's a general store just up the road. Run by a fella named Chet. He don't got nothing fancy, but he's got your basics covered.";
    n13.continueNode=14;
    m_dialogueNodes.push_back(n13);
    // Original FalloutNV.esm INFO 0x00107493; no quest scripts are executed here.
    DialogueNode n14; n14.id=14; n14.npcPrompt="If you're looking to wet your whistle, just past Chet's is the Prospector Saloon. The bartender there, Trudy, knows everyone in town.";
    n14.continueNode=15;
    m_dialogueNodes.push_back(n14);
    // Original FalloutNV.esm INFO 0x00107493; no quest scripts are executed here.
    DialogueNode n15; n15.id=15; n15.npcPrompt="Other than that, there's not much to see. Just people trying to scratch out a living.";
    n15.continueNode=0;
    m_dialogueNodes.push_back(n15);
    // Original FalloutNV.esm INFO 0x0015E13A; no quest scripts are executed here.
    DialogueNode n16; n16.id=16; n16.npcPrompt="You take care, now.";
    n16.continueNode=-1;
    m_dialogueNodes.push_back(n16);
}

bool NpcSystem::spawnDocMitchell(BSAReader& meshBsa, BSAReader& texBsa1, BSAReader* texBsa2, GxmRenderer& renderer) {
    m_font.init(renderer, &texBsa1, texBsa2);
    NPC doc;
    doc.id = "DocMitchell";
    doc.name = "Doc Mitchell";

    // Authentic ESM coordinates from GSDocMitchellHouse
    doc.posX = 2288.76f;
    doc.posY = 2244.41f;
    doc.posZ = 7360.00f;
    doc.yaw  = 0.4000f; // in radians (~23 deg)
    doc.pitch = 0.0f;
    doc.defaultYaw = doc.yaw;

    // 1. Load Body/Outfit NIF: Field Hand Outfit (republican_02.nif)
    std::vector<uint8_t> bodyNifData;
    if (readPackagedAsset("doc_body.nif", bodyNifData) || meshBsa.extractFile("meshes\\armor\\republicans", "republican_02.nif", bodyNifData)) {
        NifLoader nif;
        if (nif.load(bodyNifData.data(), bodyNifData.size())) {
            NifMesh m = nif.getMesh();
            if (renderer.createGxmMesh(m, doc.bodyMesh)) {
                doc.hasBodyMesh = true;
                logMsg("NpcSystem: loaded Doc Mitchell body mesh (%u verts)", m.vertexCount);

                std::string tex = !m.diffuseTexture.empty() ? m.diffuseTexture : "textures\\armor\\republicans\\suspender_d1.dds";
                std::vector<uint8_t> ddsData;
                if (extractDds(texBsa1, texBsa2, tex, ddsData)) {
                    DDSLoader dds;
                    if (dds.load(ddsData.data(), ddsData.size())) {
                        if (renderer.createGxmTexture(dds.getTexture(), doc.bodyTex)) {
                            doc.hasBodyTex = true;
                        }
                    }
                }
            }
        }
    }

    // 2. Load Head NIF: HeadHuman.nif
    std::vector<uint8_t> headNifData;
    if (meshBsa.extractFile("meshes\\characters\\head", "headhuman.nif", headNifData)) {
        NifLoader nif;
        if (nif.load(headNifData.data(), headNifData.size())) {
            NifMesh m = nif.getMesh();
            for(auto& part:m.parts) part.diffuseTexture="textures\\characters\\male\\headhumanold.dds";
            if (renderer.createGxmMesh(m, doc.headMesh)) {
                doc.hasHeadMesh = true;
                logMsg("NpcSystem: loaded Doc Mitchell head mesh (%u verts)", m.vertexCount);

                std::vector<uint8_t> ddsData;
                // Try authentic elder head texture first
                if (extractDds(texBsa1, texBsa2, "textures\\characters\\male\\headhumanold.dds", ddsData) ||
                    extractDds(texBsa1, texBsa2, "textures\\characters\\male\\headhuman.dds", ddsData)) {
                    DDSLoader dds;
                    if (dds.load(ddsData.data(), ddsData.size())) {
                        if (renderer.createGxmTexture(dds.getTexture(), doc.headTex)) {
                            doc.hasHeadTex = true;
                        }
                    }
                }
            }
        }
    }

    // 3. Load Hair NIF: hairbaldingold.nif
    std::vector<uint8_t> hairNifData;
    if (meshBsa.extractFile("meshes\\characters\\hair", "hairbaldingold.nif", hairNifData)) {
        NifLoader nif;
        if (nif.load(hairNifData.data(), hairNifData.size())) {
            const NifMesh& m = nif.getMesh();
            if (renderer.createGxmMesh(m, doc.hairMesh)) {
                doc.hasHairMesh = true;
                logMsg("NpcSystem: loaded Doc Mitchell hair mesh (%u verts)", m.vertexCount);

                std::vector<uint8_t> ddsData;
                if (extractDds(texBsa1, texBsa2, "textures\\characters\\hair\\hairbalding.dds", ddsData)) {
                    DDSLoader dds;
                    if (dds.load(ddsData.data(), ddsData.size())) {
                        if (renderer.createGxmTexture(dds.getTexture(), doc.hairTex)) {
                            doc.hasHairTex = true;
                        }
                    }
                }
            }
        }
    }

    // Doc has no glasses item in his original NPC inventory.

    // 5. Load Right Hand NIF: righthand.nif
    std::vector<uint8_t> rhData;
    if (readPackagedAsset("doc_right.nif", rhData) || meshBsa.extractFile("meshes\\characters\\_male", "righthand.nif", rhData)) {
        NifLoader nif;
        if (nif.load(rhData.data(), rhData.size())) {
            NifMesh m = nif.getMesh();
            if (renderer.createGxmMesh(m, doc.rightHandMesh)) {
                doc.hasRightHandMesh = true;
                logMsg("NpcSystem: loaded Doc Mitchell right hand mesh (%u verts)", m.vertexCount);

                std::vector<uint8_t> ddsData;
                if (extractDds(texBsa1, texBsa2, "textures\\characters\\male\\handmale.dds", ddsData)) {
                    DDSLoader dds;
                    if (dds.load(ddsData.data(), ddsData.size())) {
                        if (renderer.createGxmTexture(dds.getTexture(), doc.rightHandTex)) {
                            doc.hasRightHandTex = true;
                        }
                    }
                }
            }
        }
    }

    // 6. Load Left Hand NIF: lefthand.nif
    std::vector<uint8_t> lhData;
    if (readPackagedAsset("doc_left.nif", lhData) || meshBsa.extractFile("meshes\\characters\\_male", "lefthand.nif", lhData)) {
        NifLoader nif;
        if (nif.load(lhData.data(), lhData.size())) {
            NifMesh m = nif.getMesh();
            if (renderer.createGxmMesh(m, doc.leftHandMesh)) {
                doc.hasLeftHandMesh = true;
                logMsg("NpcSystem: loaded Doc Mitchell left hand mesh (%u verts)", m.vertexCount);

                std::vector<uint8_t> ddsData;
                if (extractDds(texBsa1, texBsa2, "textures\\characters\\male\\handmale.dds", ddsData)) {
                    DDSLoader dds;
                    if (dds.load(ddsData.data(), ddsData.size())) {
                        if (renderer.createGxmTexture(dds.getTexture(), doc.leftHandTex)) {
                            doc.hasLeftHandTex = true;
                        }
                    }
                }
            }
        }
    }

    const char* faceParts[]={"eyelefthuman.nif","eyerighthuman.nif","mouthhuman.nif","teethupperhuman.nif","teethlowerhuman.nif","tonguehuman.nif"};
    for(const char* part:faceParts) {
        std::vector<uint8_t> bytes;
        if(meshBsa.extractFile("meshes\\characters\\head",part,bytes)) {
            NifLoader nif; GxmMesh mesh;
            if(nif.load(bytes.data(),bytes.size())) {
                NifMesh source=nif.getMesh();
                if(strncmp(part,"eye",3)==0) for(auto& material:source.parts) material.diffuseTexture="textures\\characters\\eyes\\eyeblue.dds";
                if(renderer.createGxmMesh(source,mesh)) doc.faceParts.push_back(std::move(mesh));
            }
        }
    }
    // NPC_ 00104C0C PNAM references HDPT 00066FCF and 000B5BEA.
    // These are separate Z-up meshes, not part of the head or scalp NIF.
    for(const char* part:{"beardmustachecurly.nif","eyebrowm.nif"}) {
        std::vector<uint8_t> bytes;NifLoader nif;GxmMesh mesh;
        if(meshBsa.extractFile("meshes\\characters\\hair",part,bytes) && nif.load(bytes.data(),bytes.size()) && renderer.createGxmMesh(nif.getMesh(),mesh)) {
            doc.facialHairParts.push_back(std::move(mesh));
            logMsg("NpcSystem: loaded original Doc head part %s",part);
        } else logMsg("NpcSystem: missing original Doc head part %s",part);
    }
    logMsg("NpcSystem: Doc meshes body=%d head=%d hair=%d hands=%d/%d faceParts=%u facialHairParts=%u", doc.hasBodyMesh, doc.hasHeadMesh, doc.hasHairMesh, doc.hasRightHandMesh, doc.hasLeftHandMesh, (unsigned)doc.faceParts.size(), (unsigned)doc.facialHairParts.size());
    m_npcs.push_back(std::move(doc));
    return true;
}

const NPC* NpcSystem::getNearbyNpc() const {
    if (m_nearbyNpcIndex >= 0 && m_nearbyNpcIndex < (int)m_npcs.size()) {
        return &m_npcs[m_nearbyNpcIndex];
    }
    return nullptr;
}

void NpcSystem::update(float deltaTime, float playerX, float playerY, float playerZ,
                       uint32_t buttons, uint32_t pressed, AudioSystem& audio) {
    (void)buttons;
    m_nearbyNpcIndex = -1;

    bool startedDialogue = false;
    for (size_t i = 0; i < m_npcs.size(); ++i) {
        NPC& npc = m_npcs[i];
        npc.animTimer += deltaTime;

        // Subtle idle breathing sway
        npc.breathOffsetZ = sinf(npc.animTimer * 1.8f) * 0.35f;

        float dx = playerX - npc.posX;
        float dy = playerY - npc.posY;
        float dz = playerZ - (npc.posZ + 115.0f);
        float dist = sqrtf(dx * dx + dy * dy);

        // Look-at player tracking AI when within 380 units
        if (dist < 380.0f && dist > 1.0f) {
            float targetYaw = atan2f(dx, dy);
            float diffY = targetYaw - npc.yaw;
            while (diffY > 3.14159f)  diffY -= 6.28318f;
            while (diffY < -3.14159f) diffY += 6.28318f;
            npc.yaw += diffY * (4.5f * deltaTime);

            float targetPitch = atan2f(dz, dist);
            targetPitch = std::max(-0.45f, std::min(0.45f, targetPitch));
            npc.lookPitch += (targetPitch - npc.lookPitch) * (4.0f * deltaTime);
        } else {
            // Turn back to default facing
            float diffY = npc.defaultYaw - npc.yaw;
            while (diffY > 3.14159f)  diffY -= 6.28318f;
            while (diffY < -3.14159f) diffY += 6.28318f;
            npc.yaw += diffY * (2.0f * deltaTime);
            npc.lookPitch += (0.0f - npc.lookPitch) * (2.0f * deltaTime);
        }

        // Talking head nod gesture
        if (npc.isTalking) {
            float nod = sinf(npc.animTimer * 4.5f) * 0.05f;
            npc.pitch = npc.lookPitch + nod;
        } else {
            npc.pitch = npc.lookPitch;
        }

        // Proximity detection for interaction
        if (dist < 130.0f) {
            m_nearbyNpcIndex = (int)i;

            // Start dialogue on [Cross] press
            if (m_activeDialogueNpcIndex < 0 && (pressed & SCE_CTRL_CROSS)) {
                m_activeDialogueNpcIndex = (int)i;
                npc.isTalking = true;
                npc.currentDialogueNode = npc.greetedPlayer ? 0 : 1;
                npc.greetedPlayer = true;
                npc.selectedOption = 0;
                startedDialogue = true;

                // Voice playback must match the current INFO, not replay an unrelated intro.
                audio.play(SoundID::PipBoySelect, 0.7f);
            }
        }
    }

    // Active Dialogue navigation
    if (m_activeDialogueNpcIndex >= 0 && m_activeDialogueNpcIndex < (int)m_npcs.size()) {
        NPC& npc = m_npcs[m_activeDialogueNpcIndex];
        const DialogueNode& node = m_dialogueNodes[npc.currentDialogueNode];

        // Navigate dialogue options
        if (!node.options.empty() && (pressed & SCE_CTRL_DOWN)) {
            npc.selectedOption = (npc.selectedOption + 1) % (int)node.options.size();
            audio.play(SoundID::PipBoyTab, 0.6f);
        }
        if (!node.options.empty() && (pressed & SCE_CTRL_UP)) {
            npc.selectedOption = (npc.selectedOption + (int)node.options.size() - 1) % (int)node.options.size();
            audio.play(SoundID::PipBoyTab, 0.6f);
        }

        // Select dialogue option on [Cross]
        if ((pressed & SCE_CTRL_CROSS) && !startedDialogue) {
            int next = node.options.empty() ? node.continueNode : node.options[npc.selectedOption].nextNode;
            audio.play(SoundID::PipBoySelect, 0.8f);

            if (next < 0 || next >= (int)m_dialogueNodes.size()) {
                // End conversation
                npc.isTalking = false;
                m_activeDialogueNpcIndex = -1;
            } else {
                npc.currentDialogueNode = next;
                npc.selectedOption = 0;
            }
        }

        // Cancel / Exit on [Circle]
        if (pressed & SCE_CTRL_CIRCLE) {
            npc.isTalking = false;
            m_activeDialogueNpcIndex = -1;
            audio.play(SoundID::PipBoyLightOff, 0.5f);
        }
    }
}

void NpcSystem::render(GxmRenderer& renderer) {
    for (const auto& npc : m_npcs) {
        if (!npc.hasBodyMesh) continue;

        // 1. Render Body
        Mat4 bodyWorld = Mat4::createTransform(npc.posX, npc.posY, npc.posZ, 0.0f, 0.0f, npc.yaw, 1.0f);
        renderer.drawMeshInstance(npc.bodyMesh, npc.hasBodyTex ? &npc.bodyTex : nullptr, bodyWorld);

        // 2. Render Hands (attached to arms in natural standing pose)
        if (npc.hasRightHandMesh) {
            renderer.drawMeshInstance(npc.rightHandMesh, npc.hasRightHandTex ? &npc.rightHandTex : nullptr, bodyWorld);
        }
        if (npc.hasLeftHandMesh) {
            renderer.drawMeshInstance(npc.leftHandMesh, npc.hasLeftHandTex ? &npc.leftHandTex : nullptr, bodyWorld);
        }

        // Head vertices already include character-space height; rotate about the neck.
        Mat4 headWorld = bodyWorld * Mat4::translate(0,0,112.8f) * Mat4::rotateX(-npc.pitch) * Mat4::translate(0,0,-112.8f);
        // Eye/mouth/teeth geometry is local to Bip01 Head, unlike the skinned head.
        Mat4 faceAttachment=docHeadBoneBind();
        for(const auto& part:npc.faceParts) renderer.drawMeshInstance(part,nullptr,headWorld*faceAttachment);
        if (npc.hasHeadMesh) {
            renderer.drawMeshInstance(npc.headMesh, npc.hasHeadTex ? &npc.headTex : nullptr, headWorld);
        }
        if (npc.hasHairMesh) {
            renderer.drawMeshInstance(npc.hairMesh, npc.hasHairTex ? &npc.hairTex : nullptr, headWorld*docHairBind());
        }
        for(const auto& part:npc.facialHairParts) renderer.drawMeshInstance(part,nullptr,headWorld*docHairBind());
        if (npc.hasGlassesMesh) {
            renderer.drawMeshInstance(npc.glassesMesh, npc.hasGlassesTex ? &npc.glassesTex : nullptr, headWorld);
        }
    }
}

// ============================================================================
// 2D Dialogue UI Drawing
// ============================================================================

void NpcSystem::addUiQuad(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (m_uiVertCount + 4 > MAX_UI_VERTS || m_uiIndexCount + 6 > MAX_UI_INDICES) return;

    BasicVertex* v = &m_dialogueMesh.vertices[m_uiVertCount];
    uint16_t* idx  = &m_dialogueMesh.indices[m_uiIndexCount];
    uint16_t base  = (uint16_t)m_uiVertCount;

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

    m_uiVertCount += 4;
    m_uiIndexCount += 6;
}

void NpcSystem::addUiFrame(float x, float y, float w, float h, float th, float r, float g, float b, float a) {
    addUiQuad(x, y, w, th, r, g, b, a);
    addUiQuad(x, y + h - th, w, th, r, g, b, a);
    addUiQuad(x, y + th, th, h - 2.0f * th, r, g, b, a);
    addUiQuad(x + w - th, y + th, th, h - 2.0f * th, r, g, b, a);
}

void NpcSystem::addUiChar(char c, float x, float y, float scale, float r, float g, float b, float a) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = g_pipboyFont8x8[c - 32];

    for (int row = 0; row < 8; ++row) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (0x80 >> col)) {
                addUiQuad(x + col * scale, y + row * scale, scale, scale, r, g, b, a);
            }
        }
    }
}

void NpcSystem::addUiString(const char* text, float x, float y, float scale, float r, float g, float b, float a) {
    if (m_font.valid()) { m_font.text(text, x, y, scale*14.0f, r,g,b,a); return; }
    if (!text) return;
    float curX = x;
    while (*text) {
        char c = *text++;
        if (c == '\n') {
            y += 10.0f * scale;
            curX = x;
        } else {
            addUiChar(c, curX, y, scale, r, g, b, a);
            curX += 9.0f * scale;
        }
    }
}

void NpcSystem::renderDialogue(GxmRenderer& renderer) {
    if(!m_dialogueMesh.isValid() || m_activeDialogueNpcIndex<0 || m_activeDialogueNpcIndex>=(int)m_npcs.size()) return;
    m_uiVertCount=m_uiIndexCount=0;m_font.clear();
    const auto& npc=m_npcs[m_activeDialogueNpcIndex];const auto& node=m_dialogueNodes[npc.currentDialogueNode];
    // DialogMenu uses a subtitle area and a topic list, without an invented window frame.
    addUiQuad(48,322,864,210,0,0,0,.65f);
    addUiString(npc.name.c_str(),64,330,1.4f,1,.72f,.18f,1);
    if(node.options.empty()) {
        if(m_font.valid()) m_font.text(node.npcPrompt,64,365,22,1,.72f,.18f,1,820);
        else addUiString(node.npcPrompt,64,365,1.0f,1,.72f,.18f,1);
        addUiString("[X] Continue     [O] Goodbye",64,505,1.05f,1,.72f,.18f,.8f);
    } else {
        float y=362;
        for(size_t i=0;i<node.options.size();++i) {
            bool selected=(int)i==npc.selectedOption;
            if(selected) addUiQuad(58,y-1,840,25,1,.72f,.18f,.18f);
            addUiString(node.options[i].text,64,y,1.15f,1,.72f,.18f,selected?1:.65f);y+=27;
        }
    }
    if(m_uiIndexCount) renderer.draw2D(m_dialogueMesh.vertices,m_uiVertCount,m_dialogueMesh.indices,m_uiIndexCount);
    m_font.flush(renderer);
}
