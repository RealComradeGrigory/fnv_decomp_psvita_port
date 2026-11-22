// Generated no-op declarations for the host-only GXM test.
#include "renderer/gxm_renderer.h"
#include <psp2/display.h>
extern "C" {
int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *pParam, SceDisplaySetBufSync sync) { return {}; }
int sceGxmBeginScene(SceGxmContext *context, unsigned int flags, const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion, SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject, const SceGxmColorSurface *colorSurface, const SceGxmDepthStencilSurface *depthStencil) { return {}; }
int sceGxmColorSurfaceInit(SceGxmColorSurface *surface, SceGxmColorFormat colorFormat, SceGxmColorSurfaceType surfaceType, SceGxmColorSurfaceScaleMode scaleMode, SceGxmOutputRegisterSize outputRegisterSize, unsigned int width, unsigned int height, unsigned int strideInPixels, void *data) { return {}; }
int sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **context) { return {}; }
int sceGxmCreateRenderTarget(const SceGxmRenderTargetParams *params, SceGxmRenderTarget **renderTarget) { return {}; }
int sceGxmDepthStencilSurfaceInit(SceGxmDepthStencilSurface *surface, SceGxmDepthStencilFormat depthStencilFormat, SceGxmDepthStencilSurfaceType surfaceType, unsigned int strideInSamples, void *depthData, void *stencilData) { return {}; }
int sceGxmDestroyContext(SceGxmContext *context) { return {}; }
int sceGxmDestroyRenderTarget(SceGxmRenderTarget *renderTarget) { return {}; }
int sceGxmDisplayQueueAddEntry(SceGxmSyncObject *oldBuffer, SceGxmSyncObject *newBuffer, const void *callbackData) { return {}; }
int sceGxmDisplayQueueFinish() { return {}; }
int sceGxmEndScene(SceGxmContext *context, const SceGxmNotification *vertexNotification, const SceGxmNotification *fragmentNotification) { return {}; }
int sceGxmInitialize(const SceGxmInitializeParams *params) { return {}; }
int sceGxmMapFragmentUsseMemory(void *base, SceSize size, unsigned int *offset) { return {}; }
int sceGxmMapMemory(void *base, SceSize size, SceGxmMemoryAttribFlags attr) { return {}; }
int sceGxmMapVertexUsseMemory(void *base, SceSize size, unsigned int *offset) { return {}; }
int sceGxmPadHeartbeat(const SceGxmColorSurface *displaySurface, SceGxmSyncObject *displaySyncObject) { return {}; }
const SceGxmProgramParameter * sceGxmProgramFindParameterByName(const SceGxmProgram *program, const char *name) { return {}; }
unsigned int sceGxmProgramParameterGetResourceIndex(const SceGxmProgramParameter *parameter) { return {}; }
#ifndef FNV_WORLD_CAPTURE
void sceGxmSetBackDepthFunc(SceGxmContext *context, SceGxmDepthFunc depthFunc) { }
#endif
#ifndef FNV_WORLD_CAPTURE
void sceGxmSetBackDepthWriteEnable(SceGxmContext *context, SceGxmDepthWriteMode enable) { }
#endif
#ifndef FNV_WORLD_CAPTURE
void sceGxmSetCullMode(SceGxmContext *context, SceGxmCullMode mode) { }
#endif
#ifndef FNV_WORLD_CAPTURE
void sceGxmSetFragmentProgram(SceGxmContext *context, const SceGxmFragmentProgram *fragmentProgram) { }
#endif
#ifndef FNV_WORLD_CAPTURE
int sceGxmSetFragmentTexture(SceGxmContext *context, unsigned int textureIndex, const SceGxmTexture *texture) { return {}; }
#endif
#ifndef FNV_WORLD_CAPTURE
void sceGxmSetFrontDepthFunc(SceGxmContext *context, SceGxmDepthFunc depthFunc) { }
#endif
#ifndef FNV_WORLD_CAPTURE
void sceGxmSetFrontDepthWriteEnable(SceGxmContext *context, SceGxmDepthWriteMode enable) { }
int sceGxmSetUniformDataF(void *uniformBuffer, const SceGxmProgramParameter *parameter, unsigned int componentOffset, unsigned int componentCount, const float *sourceData) { return {}; }
void sceGxmSetVertexProgram(SceGxmContext *context, const SceGxmVertexProgram *vertexProgram) { }
#endif
int sceGxmShaderPatcherCreate(const SceGxmShaderPatcherParams *params, SceGxmShaderPatcher **shaderPatcher) { return {}; }
int sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmShaderPatcherId programId, SceGxmOutputRegisterFormat outputFormat, SceGxmMultisampleMode multisampleMode, const SceGxmBlendInfo *blendInfo, const SceGxmProgram *vertexProgram, SceGxmFragmentProgram **fragmentProgram) { return {}; }
int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmShaderPatcherId programId, const SceGxmVertexAttribute *attributes, unsigned int attributeCount, const SceGxmVertexStream *streams, unsigned int streamCount, SceGxmVertexProgram **vertexProgram) { return {}; }
int sceGxmShaderPatcherDestroy(SceGxmShaderPatcher *shaderPatcher) { return {}; }
int sceGxmShaderPatcherRegisterProgram(SceGxmShaderPatcher *shaderPatcher, const SceGxmProgram *programHeader, SceGxmShaderPatcherId *programId) { return {}; }
int sceGxmShaderPatcherReleaseFragmentProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmFragmentProgram *fragmentProgram) { return {}; }
int sceGxmShaderPatcherReleaseVertexProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmVertexProgram *vertexProgram) { return {}; }
int sceGxmShaderPatcherUnregisterProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmShaderPatcherId programId) { return {}; }
int sceGxmSyncObjectCreate(SceGxmSyncObject **syncObject) { return {}; }
int sceGxmSyncObjectDestroy(SceGxmSyncObject *syncObject) { return {}; }
int sceGxmTerminate() { return {}; }
#ifndef FNV_WORLD_CAPTURE
int sceGxmTextureInitLinear(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount) { return {}; }
#endif
int sceGxmTextureSetMagFilter(SceGxmTexture *texture, SceGxmTextureFilter magFilter) { return {}; }
int sceGxmTextureSetMinFilter(SceGxmTexture *texture, SceGxmTextureFilter minFilter) { return {}; }
int sceGxmTextureSetMipFilter(SceGxmTexture *texture, SceGxmTextureMipFilter mipFilter) { return {}; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *texture, SceGxmTextureAddrMode addrMode) { return {}; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *texture, SceGxmTextureAddrMode addrMode) { return {}; }
int sceGxmUnmapFragmentUsseMemory(void *base) { return {}; }
int sceGxmUnmapMemory(void *base) { return {}; }
int sceGxmUnmapVertexUsseMemory(void *base) { return {}; }
}
