#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iterator>
// Inspect ownership and force allocation failures without changing shipping code.
#define private public
#include "renderer/gxm_renderer.h"
#include "pipboy/pipboy.h"
#undef private
#include "audio/audio_system.h"

static void require(bool ok, const char* message) {
    if (!ok) { std::printf("FAIL: %s\n", message); std::exit(1); }
}
static std::unordered_map<int, void*> blocks;
static std::unordered_map<std::string, std::string> fixturePaths;
static std::unordered_map<int, size_t> allocationSizes;
static size_t allocatedBytes=0, peakBytes=0;
static int nextUid=1, finishCount=0, drawCount=0, overflows=0;
static bool failUniform=false;
static const BasicVertex* vertexStream=nullptr;
#ifdef FNV_WORLD_CAPTURE
void hostCaptureDraw(const BasicVertex*,const uint16_t*,unsigned);
#endif
struct Draw {
    const BasicVertex* vertices; const uint16_t* indices;
    std::vector<BasicVertex> vertexCopy; std::vector<uint16_t> indexCopy;
};
static std::vector<Draw> pending;
void logMsg(const char* message, ...) {
    if (strstr(message,"capacity")) ++overflows;
}
#ifndef FNV_REAL_BSA
bool BSAReader::extractFile(const char* folder, const char* file, std::vector<uint8_t>& data) {
    auto found=fixturePaths.find(std::string(folder)+"\\"+file);
    if(found==fixturePaths.end()) return false;
    std::ifstream input(found->second,std::ios::binary);
    data.assign(std::istreambuf_iterator<char>(input),{});
    return !data.empty();
}
#endif
void AudioSystem::play(SoundID,float,float) {}

extern "C" {
SceUID sceKernelAllocMemBlock(const char*, SceKernelMemBlockType, SceSize size, SceKernelAllocMemBlockOpt*) {
    int uid=nextUid++; blocks[uid]=calloc(1,size); allocationSizes[uid]=size;
    allocatedBytes+=size; peakBytes=std::max(allocatedBytes,peakBytes); return uid;
}
int sceKernelGetMemBlockBase(SceUID uid, void** base) {
    auto found=blocks.find(uid); if(found==blocks.end()) return -1;
    *base=found->second; return 0;
}
int sceKernelFreeMemBlock(SceUID uid) {
    require(pending.empty(), "resource freed before queued draws finished");
    auto found=blocks.find(uid); require(found!=blocks.end(),"double free");
    free(found->second); blocks.erase(found);
    allocatedBytes-=allocationSizes.at(uid); allocationSizes.erase(uid); return 0;
}
int sceGxmSetVertexStream(SceGxmContext*, unsigned, const void* ptr) {
    vertexStream=static_cast<const BasicVertex*>(ptr); return 0;
}
int sceGxmDraw(SceGxmContext*,SceGxmPrimitiveType,SceGxmIndexFormat,const void* ptr,unsigned count) {
    auto indices=static_cast<const uint16_t*>(ptr);
    require(reinterpret_cast<uintptr_t>(indices)%4==0,"unaligned GPU indices");
    unsigned vertices=1+*std::max_element(indices,indices+count);
    pending.push_back({vertexStream,indices,{vertexStream,vertexStream+vertices},{indices,indices+count}});
#ifdef FNV_WORLD_CAPTURE
    hostCaptureDraw(vertexStream,indices,count);
#endif
    ++drawCount; return 0;
}
void sceGxmFinish(SceGxmContext*) {
    for(const auto& d:pending) {
        require(memcmp(d.vertices,d.vertexCopy.data(),d.vertexCopy.size()*sizeof(BasicVertex))==0,
                "vertices changed while GPU still reading");
        require(memcmp(d.indices,d.indexCopy.data(),d.indexCopy.size()*sizeof(uint16_t))==0,
                "indices changed while GPU still reading");
    }
    pending.clear(); ++finishCount;
}
int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext*,void** p) {
    static float buffer[64]; *p=failUniform?nullptr:buffer; return failUniform?-1:0;
}
int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext* c,void** p) {
    return sceGxmReserveVertexDefaultUniformBuffer(c,p);
}
}

int main() {
    GxmRenderer r; r.m_context=reinterpret_cast<SceGxmContext*>(1);
    BasicVertex vertices[3]{}; alignas(4) uint16_t indices[]={0,1,2};
    r.beginFrame();
    r.draw2D(vertices,3,indices,3);
    vertices[0].position[0]=42; indices[0]=1;
    r.draw2D(vertices,3,indices,3);
    r.endFrame(); r.waitForGpu();
    require(drawCount==2,"copied both UI batches");
    r.beginFrame(); int before=drawCount;
    indices[0]=9; r.draw2D(vertices,3,indices,3);
    require(drawCount==before,"invalid indices rejected"); indices[0]=0;
    failUniform=true; r.draw2D(vertices,3,indices,3);
    require(drawCount==before,"failed uniforms reject draw"); failUniform=false;
    r.endFrame(); r.waitForGpu();
    GxmMesh fx; require(r.allocateDynamicMesh(3,3,fx),"allocate dynamic effect");
    memcpy(fx.indices,indices,sizeof(indices));
    r.beginFrame(); r.drawMeshInstance(fx,nullptr,Mat4::identity());
    fx.vertices[0].position[1]=73; fx.indices[0]=2;
    r.endFrame(); int oldFinish=finishCount; r.destroyGxmMesh(fx);
    require(finishCount>oldFinish,"free waits for GPU");

    PipBoy ui; require(ui.init(r,nullptr,nullptr),"allocate Pip-Boy buffers");
    TextureData tex{}; tex.width=tex.height=4; tex.pixels.resize(64,255);
    require(r.createGxmTexture(tex,ui.m_texVaultBoyHappy),"create UI texture"); ui.m_hasTexVaultBoyHappy=true;
    require(r.createGxmTexture(tex,ui.m_texVaultBoyPain),"create UI texture"); ui.m_hasTexVaultBoyPain=true;
    require(r.createGxmTexture(tex,ui.m_texWeaponIcon),"create UI texture"); ui.m_hasTexWeaponIcon=true;
    SceCtrlData pad{}; pad.lx=pad.ly=pad.rx=pad.ry=128;
    for(int cycle=0;cycle<100;cycle++) {
        for(int main=0;main<3;main++) {
            for(int sub=0;sub<(main==1?4:3);sub++) {
                ui.m_isOpen=true; ui.m_mainTab=static_cast<PipBoyMainTab>(main); ui.m_subTab=sub;
                ui.m_cursorIndex=cycle%5; ui.m_playerHp=cycle%2?20:185;
                ui.update(pad,0,1.f/60,"Goodsprings Test",120,240,124,.3f,nullptr);
                r.beginFrame(); before=drawCount; ui.render(r);
                require(drawCount>before,"UI screen submitted geometry");
                // Write to the original CPU buffer immediately after submission.
                ui.m_uiMesh.vertices[0].position[0]=-1234;
                r.endFrame(); r.waitForGpu();
                ui.close(); r.beginFrame(); ui.render(r); r.endFrame(); r.waitForGpu();
            }
        }
    }
    require(overflows==0,"all UI tabs fit frame arena");
    ui.cleanup(r);
    r.gpuFree(r.m_frameVerticesUid); r.gpuFree(r.m_frameIndicesUid);
    r.m_context=nullptr;
    require(blocks.empty(),"all GPU allocations released");
    std::printf("PASS: immutable UI/effect snapshots, aligned indices, rejected invalid draws, safe cleanup; 1000 UI screens (%d deferred draws)\n",drawCount);
    return 0;
}
