#include "scene_support.h"
#define private public
#include "npc/npc_system.h"
#undef private
#include "npc/head_attachments.h"
AudioSystem::AudioSystem()=default;
AudioSystem::~AudioSystem()=default;
int main(int argc,char** argv) {
 require(argc==4,"visual test arguments");
 std::ifstream manifest(argv[1]);std::string line;
 while(std::getline(manifest,line)) {auto tab=line.find('\t');fixturePaths[line.substr(0,tab)]=line.substr(tab+1);}
 GxmRenderer r;r.m_context=reinterpret_cast<SceGxmContext*>(1);BSAReader archive;
 const std::string effect="meshes\\effects\\ambient\\fxlightbeamw03.nif";
 std::vector<uint8_t> bytes;require(archive.extractFile("meshes\\effects\\ambient","fxlightbeamw03.nif",bytes),"original beam fixture");
 NifLoader nif;require(nif.load(bytes.data(),bytes.size()),"beam parses");
 const auto& mesh=nif.getMesh();bool alpha=false,textured=false;float opacity=1;
 for(const auto& part:mesh.parts) {alpha|=part.transparent;textured|=!part.diffuseTexture.empty();}
 for(const auto& c:mesh.colors) opacity=std::min(opacity,c.a);
 require(alpha && textured,"original light beam retains alpha material and no-lighting texture");
 require(mesh.colors.size()==mesh.vertices.size() && opacity<.99f,"original beam vertex opacity retained");
 GxmMesh gpu;require(r.createGxmMesh(mesh,gpu),"beam GPU mesh");
 r.beginFrame();int before=drawCount;r.drawMeshInstance(gpu,nullptr,Mat4::identity(),0);
 int opaque=0,transparent=0;for(const auto& part:mesh.parts) (part.transparent?transparent:opaque)++;
 require(drawCount-before==opaque,"transparent beam parts excluded from opaque pass");
 before=drawCount;
 r.drawMeshInstance(gpu,nullptr,Mat4::identity(),1);require(drawCount-before==transparent,"only transparent beam parts submitted in alpha pass");r.endFrame();r.waitForGpu();r.destroyGxmMesh(gpu);
 for(const char* file:{"doc_body.nif","doc_right.nif","doc_left.nif","hands0_right.nif","hands0_left.nif","hands1_right.nif","hands1_left.nif","hands2_right.nif","hands2_left.nif","hands3_right.nif","hands3_left.nif","hands4_right.nif","hands4_left.nif"}) {
  std::ifstream in(std::string(argv[2])+"/"+file,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(in),{});
  require(nif.load(bytes.data(),bytes.size()),"packaged posed mesh loads");
  if(std::string(file)=="doc_body.nif") {require(nif.getMesh().radius<80,"idle mesh does not explode");float lo=1e9,hi=-1e9;for(const auto& v:nif.getMesh().vertices){lo=std::min(lo,v.z);hi=std::max(hi,v.z);}require(lo>-3 && hi<122,"idle feet and neck have original character heights");}
 }
 NpcSystem npc;AudioSystem audio;require(npc.init(r),"dialogue init");NPC doc{};doc.posX=doc.posY=doc.posZ=doc.yaw=doc.pitch=doc.defaultYaw=0;npc.m_npcs.push_back(doc);
 auto update=[&](unsigned pressed) {npc.update(.016f,0,0,60,pressed,pressed,audio);};
 update(SCE_CTRL_CROSS);require(npc.m_npcs[0].currentDialogueNode==1,"first interaction shows greeting once");
 update(0);require(npc.m_npcs[0].currentDialogueNode==1,"held input does not advance dialogue");update(SCE_CTRL_CROSS);require(npc.m_npcs[0].currentDialogueNode==0,"greeting advances to topics");
 update(SCE_CTRL_CROSS);require(npc.m_npcs[0].currentDialogueNode==2,"topic displays original reply");update(SCE_CTRL_CROSS);require(npc.m_npcs[0].currentDialogueNode==3,"second INFO response page appears");update(SCE_CTRL_CROSS);require(npc.m_npcs[0].currentDialogueNode==0,"response finishes at topic list");
 npc.m_npcs[0].selectedOption=npc.m_dialogueNodes[0].options.size()-1;update(SCE_CTRL_CROSS);require(npc.isInDialogue(),"goodbye response displayed before exit");update(SCE_CTRL_CROSS);require(!npc.isInDialogue(),"goodbye ends conversation");update(SCE_CTRL_CROSS);require(npc.m_npcs[0].currentDialogueNode==0,"reopening does not replay intro");
 npc.cleanup(r);
 r.setTextureArchives(&archive,nullptr);require(npc.init(r),"actual NPC init");
 require(npc.spawnDocMitchell(archive,archive,nullptr,r),"actual Doc spawn path");
 auto& originalDoc=npc.m_npcs[0];require(originalDoc.hasBodyMesh && originalDoc.hasHeadMesh,"actual Doc body and head loaded");
 require(originalDoc.facialHairParts.size()==2,"Doc PNAM mustache and eyebrows both loaded");
 originalDoc.posX=originalDoc.posY=originalDoc.posZ=originalDoc.yaw=0;
 r.setCameraFreecam(0,60,120,0,3.14159265f,65);
 r.beginFrame();npc.render(r);require(!pending.empty(),"actual NPC renders");r.endFrame();r.waitForGpu();
 for(const auto& hair:originalDoc.facialHairParts) {
  require(hair.parts.size()==1 && hair.parts[0].texture && hair.parts[0].texture->isValid(),"original facial hair diffuse/alpha texture loaded");
  r.beginFrame();before=drawCount;r.drawMeshInstance(hair,nullptr,docHairBind());
  require(drawCount>before,"mustache/eyebrow themselves submit visible geometry");r.endFrame();r.waitForGpu();
 }
 npc.cleanup(r);r.gpuFree(r.m_frameVerticesUid);r.gpuFree(r.m_frameIndicesUid);r.m_context=nullptr;require(blocks.empty(),"visual regression resources released");
 std::printf("PASS: original light beam alpha/texture/opacity and render passes, 13 original posed meshes, original dialogue paging and goodbye\n");
}
