/**
 * GXM Renderer - Implementation
 *
 * PS Vita GPU renderer using Sony's GXM API.
 * Handles display setup, shader management, and mesh rendering.
 */

#include "gxm_renderer.h"
#include "near_clip.h"
#include "frustum_clip.h"

#include <psp2/kernel/sysmem.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cctype>
#ifdef FNV_VITA_TARGET
#include <psp2/kernel/processmgr.h>
#else
#include <chrono>
#endif

static uint32_t renderCpuTime() {
#ifdef FNV_VITA_TARGET
    return sceKernelGetProcessTimeLow();
#else
    return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
#include "bsa/bsa_reader.h"

extern void logMsg(const char* fmt, ...);

// ============================================================================
// Pre-compiled GXP shader binaries
// ============================================================================
#include "compiled_shaders.h"

static const uint8_t* const clearVertexProgramGxp   = clear_v_gxp;
static const uint8_t* const clearFragmentProgramGxp = clear_f_gxp;
static const uint8_t* const basicVertexProgramGxp   = color_v_gxp;
static const uint8_t* const basicFragmentProgramGxp = color_f_gxp;
static const uint8_t* const textureVertexProgramGxp   = texture_v_gxp;
static const uint8_t* const textureFragmentProgramGxp = texture_f_gxp;

// ============================================================================
// Display callback data passed to sceGxmDisplayQueueAddEntry
// ============================================================================
struct DisplayCallbackData {
    void* address;
};

// ============================================================================
// Mat4 implementation (column-major)
// ============================================================================

Mat4 Mat4::identity() {
    Mat4 r;
    memset(&r, 0, sizeof(Mat4));
    r.m[0][0] = 1.0f;
    r.m[1][1] = 1.0f;
    r.m[2][2] = 1.0f;
    r.m[3][3] = 1.0f;
    return r;
}

Mat4 Mat4::perspective(float fovY, float aspect, float nearZ, float farZ) {
    Mat4 r;
    memset(&r, 0, sizeof(Mat4));

    float f = 1.0f / tanf(fovY * 0.5f);
    float rangeInv = 1.0f / (nearZ - farZ);

    r.m[0][0] = f / aspect;
    r.m[1][1] = f;
    r.m[2][2] = (farZ + nearZ) * rangeInv;
    r.m[2][3] = -1.0f;
    r.m[3][2] = 2.0f * farZ * nearZ * rangeInv;
    return r;
}

Mat4 Mat4::lookAt(float eyeX, float eyeY, float eyeZ,
                  float atX,  float atY,  float atZ,
                  float upX,  float upY,  float upZ) {
    // Forward vector (from eye to target)
    float fx = atX - eyeX;
    float fy = atY - eyeY;
    float fz = atZ - eyeZ;
    float fLen = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fLen > 0.0f) { fx /= fLen; fy /= fLen; fz /= fLen; }

    // Side vector = forward x up
    float sx = fy * upZ - fz * upY;
    float sy = fz * upX - fx * upZ;
    float sz = fx * upY - fy * upX;
    float sLen = sqrtf(sx * sx + sy * sy + sz * sz);
    if (sLen > 0.0f) { sx /= sLen; sy /= sLen; sz /= sLen; }

    // Recompute up = side x forward
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    Mat4 r;
    memset(&r, 0, sizeof(Mat4));

    // Column-major layout: r.m[col][row]
    r.m[0][0] =  sx;   r.m[0][1] =  ux;   r.m[0][2] = -fx;   r.m[0][3] = 0.0f;
    r.m[1][0] =  sy;   r.m[1][1] =  uy;   r.m[1][2] = -fy;   r.m[1][3] = 0.0f;
    r.m[2][0] =  sz;   r.m[2][1] =  uz;   r.m[2][2] = -fz;   r.m[2][3] = 0.0f;
    r.m[3][0] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
    r.m[3][1] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
    r.m[3][2] =  (fx * eyeX + fy * eyeY + fz * eyeZ);
    r.m[3][3] = 1.0f;

    return r;
}

Mat4 Mat4::rotateX(float radians) {
    Mat4 r = identity();
    float c = cosf(radians);
    float s = sinf(radians);
    r.m[1][1] =  c;
    r.m[1][2] =  s;
    r.m[2][1] = -s;
    r.m[2][2] =  c;
    return r;
}

Mat4 Mat4::rotateY(float radians) {
    Mat4 r = identity();
    float c = cosf(radians);
    float s = sinf(radians);
    r.m[0][0] =  c;
    r.m[0][2] = -s;
    r.m[2][0] =  s;
    r.m[2][2] =  c;
    return r;
}

Mat4 Mat4::rotateZ(float radians) {
    Mat4 r = identity();
    float c = cosf(radians);
    float s = sinf(radians);
    r.m[0][0] =  c;
    r.m[0][1] =  s;
    r.m[1][0] = -s;
    r.m[1][1] =  c;
    return r;
}

Mat4 Mat4::scale(float s) {
    Mat4 r = identity();
    r.m[0][0] = s;
    r.m[1][1] = s;
    r.m[2][2] = s;
    return r;
}

Mat4 Mat4::translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[3][0] = x;
    r.m[3][1] = y;
    r.m[3][2] = z;
    return r;
}

Mat4 Mat4::createTransform(float x, float y, float z,
                            float rx, float ry, float rz,
                            float s) {
    // Gamebryo Euler rotation order: intrinsic ZXY (Rz * Rx * Ry)
    // Gamebryo defines positive angles as clockwise looking down each positive axis,
    // requiring negated angles when converted to standard CCW coordinate systems.
    Mat4 T  = translate(x, y, z);
    Mat4 Rz = rotateZ(-rz);
    Mat4 Rx = rotateX(-rx);
    Mat4 Ry = rotateY(-ry);
    Mat4 S  = scale(s);
    return T * Rz * Rx * Ry * S;
}

Mat4 Mat4::orthographic(float left, float right, float bottom, float top, float near, float far) {
    Mat4 res;
    memset(&res, 0, sizeof(res));
    res.m[0][0] = 2.0f / (right - left);
    res.m[1][1] = 2.0f / (top - bottom);
    res.m[2][2] = -2.0f / (far - near);
    res.m[3][0] = -(right + left) / (right - left);
    res.m[3][1] = -(top + bottom) / (top - bottom);
    res.m[3][2] = -(far + near) / (far - near);
    res.m[3][3] = 1.0f;
    return res;
}

Mat4 Mat4::operator*(const Mat4& other) const {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            r.m[col][row] = m[0][row] * other.m[col][0]
                          + m[1][row] * other.m[col][1]
                          + m[2][row] * other.m[col][2]
                          + m[3][row] * other.m[col][3];
        }
    }
    return r;
}

// ============================================================================
// GxmRenderer - memory helpers
// ============================================================================

static uint32_t getAlignedSize(SceKernelMemBlockType type, uint32_t size) {
    switch (type) {
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW:
        return (size + 256 * 1024 - 1) & ~(256 * 1024 - 1);
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW:
        return (size + 1024 * 1024 - 1) & ~(1024 * 1024 - 1);
    default:
        return (size + 4 * 1024 - 1) & ~(4 * 1024 - 1);
    }
}

void* GxmRenderer::gpuAlloc(SceKernelMemBlockType type, uint32_t size,
                             uint32_t alignment, SceGxmMemoryAttribFlags attribs,
                             SceUID* uid) {
    (void)alignment;
    uint32_t alignedSize = getAlignedSize(type, size);

    *uid = sceKernelAllocMemBlock("gpu_mem", type, alignedSize, nullptr);
    if (*uid < 0) {
        logMsg("gpuAlloc: sceKernelAllocMemBlock failed for type 0x%08X (size %u): 0x%08X, attempting fallback...",
               type, alignedSize, *uid);
        // Fallback from CDRAM to USER_RW_UNCACHE (safe for GPU without CPU cache incoherency)
        if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW) {
            type = SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE;
            alignedSize = getAlignedSize(type, size);
            *uid = sceKernelAllocMemBlock("gpu_mem_uncache", type, alignedSize, nullptr);
        }
        if (*uid < 0) {
            logMsg("gpuAlloc: fallback sceKernelAllocMemBlock failed: 0x%08X", *uid);
            return nullptr;
        }
        logMsg("gpuAlloc: fallback to USER_RW_UNCACHE succeeded!");
    }

    void* base = nullptr;
    int err = sceKernelGetMemBlockBase(*uid, &base);
    if (err < 0) {
        logMsg("gpuAlloc: sceKernelGetMemBlockBase failed: 0x%08X", err);
        sceKernelFreeMemBlock(*uid);
        *uid = 0;
        return nullptr;
    }

    err = sceGxmMapMemory(base, alignedSize, attribs);
    if (err < 0) {
        logMsg("gpuAlloc: sceGxmMapMemory failed: 0x%08X", err);
        sceKernelFreeMemBlock(*uid);
        *uid = 0;
        return nullptr;
    }

    return base;
}

void GxmRenderer::gpuFree(SceUID uid) {
    if (uid > 0) {
        // Destruction is also a submission barrier. A still-active scene must
        // finish before a queued command's buffers can be unmapped.
        if(m_sceneActive)endFrame();
        waitForGpu();
        void* base = nullptr;
        if (sceKernelGetMemBlockBase(uid, &base) >= 0) {
            sceGxmUnmapMemory(base);
        }
        sceKernelFreeMemBlock(uid);
    }
}

void* GxmRenderer::vertexUsseAlloc(uint32_t size, SceUID* uid, uint32_t* usseOffset) {
    size = (size + 4095) & ~4095u; // align to 4KB

    *uid = sceKernelAllocMemBlock("vertex_usse",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, nullptr);
    if (*uid < 0) {
        logMsg("vertexUsseAlloc: sceKernelAllocMemBlock failed: 0x%08X", *uid);
        return nullptr;
    }

    void* base = nullptr;
    sceKernelGetMemBlockBase(*uid, &base);

    int err = sceGxmMapVertexUsseMemory(base, size, usseOffset);
    if (err < 0) {
        logMsg("vertexUsseAlloc: sceGxmMapVertexUsseMemory failed: 0x%08X", err);
        sceKernelFreeMemBlock(*uid);
        return nullptr;
    }

    return base;
}

void GxmRenderer::vertexUsseFree(SceUID uid) {
    if (uid > 0) {
        void* base = nullptr;
        if (sceKernelGetMemBlockBase(uid, &base) >= 0) {
            sceGxmUnmapVertexUsseMemory(base);
        }
        sceKernelFreeMemBlock(uid);
    }
}

void* GxmRenderer::fragmentUsseAlloc(uint32_t size, SceUID* uid, uint32_t* usseOffset) {
    size = (size + 4095) & ~4095u;

    *uid = sceKernelAllocMemBlock("fragment_usse",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, nullptr);
    if (*uid < 0) {
        logMsg("fragmentUsseAlloc: sceKernelAllocMemBlock failed: 0x%08X", *uid);
        return nullptr;
    }

    void* base = nullptr;
    sceKernelGetMemBlockBase(*uid, &base);

    int err = sceGxmMapFragmentUsseMemory(base, size, usseOffset);
    if (err < 0) {
        logMsg("fragmentUsseAlloc: sceGxmMapFragmentUsseMemory failed: 0x%08X", err);
        sceKernelFreeMemBlock(*uid);
        return nullptr;
    }

    return base;
}

void GxmRenderer::fragmentUsseFree(SceUID uid) {
    if (uid > 0) {
        void* base = nullptr;
        if (sceKernelGetMemBlockBase(uid, &base) >= 0) {
            sceGxmUnmapFragmentUsseMemory(base);
        }
        sceKernelFreeMemBlock(uid);
    }
}

// ============================================================================
// Display callback (called by GXM when a buffer is ready to display)
// ============================================================================

void GxmRenderer::displayCallback(const void* callbackData) {
    const DisplayCallbackData* data =
        static_cast<const DisplayCallbackData*>(callbackData);

    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(SceDisplayFrameBuf));
    fb.size        = sizeof(SceDisplayFrameBuf);
    fb.base        = data->address;
    fb.pitch       = DISPLAY_STRIDE;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fb.width       = DISPLAY_WIDTH;
    fb.height      = DISPLAY_HEIGHT;

    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

// ============================================================================
// Initialization
// ============================================================================

bool GxmRenderer::init() {
    if (m_initialized) {
        logMsg("GxmRenderer: already initialized");
        return true;
    }

    logMsg("GxmRenderer: initializing...");

    // --- Initialize GXM library ---
    SceGxmInitializeParams initParams;
    memset(&initParams, 0, sizeof(SceGxmInitializeParams));
    initParams.flags                        = 0;
    initParams.displayQueueMaxPendingCount  = MAX_PENDING_SWAPS;
    initParams.displayQueueCallback         = displayCallback;
    initParams.displayQueueCallbackDataSize = sizeof(DisplayCallbackData);
    initParams.parameterBufferSize          = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;

    int err = sceGxmInitialize(&initParams);
    if (err < 0) {
        logMsg("GxmRenderer: sceGxmInitialize failed: 0x%08X", err);
        return false;
    }
    logMsg("GxmRenderer: GXM initialized");

    // --- Allocate ring buffers for the GXM context ---
    static const uint32_t VDM_RING_BUFFER_SIZE      = 1024 * 1024;
    static const uint32_t VERTEX_RING_BUFFER_SIZE    = 2 * 1024 * 1024;
    static const uint32_t FRAGMENT_RING_BUFFER_SIZE  = 1024 * 1024;
    static const uint32_t FRAGMENT_USSE_RING_BUFFER_SIZE = 64 * 1024;

    void* vdmRingBuffer = gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        VDM_RING_BUFFER_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ,
        &m_vdmRingBufferUid);
    if (!vdmRingBuffer) return false;

    void* vertexRingBuffer = gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        VERTEX_RING_BUFFER_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ,
        &m_vertexRingBufferUid);
    if (!vertexRingBuffer) return false;

    void* fragmentRingBuffer = gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        FRAGMENT_RING_BUFFER_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ,
        &m_fragmentRingBufferUid);
    if (!fragmentRingBuffer) return false;

    uint32_t fragmentUsseRingBufferOffset = 0;
    void* fragmentUsseRingBuffer = fragmentUsseAlloc(
        FRAGMENT_USSE_RING_BUFFER_SIZE,
        &m_fragmentUsseRingBufferUid,
        &fragmentUsseRingBufferOffset);
    if (!fragmentUsseRingBuffer) return false;

    // --- Create GXM context ---
    SceGxmContextParams contextParams;
    memset(&contextParams, 0, sizeof(SceGxmContextParams));
    contextParams.hostMem                       = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    contextParams.hostMemSize                   = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    contextParams.vdmRingBufferMem              = vdmRingBuffer;
    contextParams.vdmRingBufferMemSize          = VDM_RING_BUFFER_SIZE;
    contextParams.vertexRingBufferMem           = vertexRingBuffer;
    contextParams.vertexRingBufferMemSize       = VERTEX_RING_BUFFER_SIZE;
    contextParams.fragmentRingBufferMem         = fragmentRingBuffer;
    contextParams.fragmentRingBufferMemSize     = FRAGMENT_RING_BUFFER_SIZE;
    contextParams.fragmentUsseRingBufferMem     = fragmentUsseRingBuffer;
    contextParams.fragmentUsseRingBufferMemSize = FRAGMENT_USSE_RING_BUFFER_SIZE;
    contextParams.fragmentUsseRingBufferOffset  = fragmentUsseRingBufferOffset;

    err = sceGxmCreateContext(&contextParams, &m_context);
    if (err < 0) {
        logMsg("GxmRenderer: sceGxmCreateContext failed: 0x%08X", err);
        free(contextParams.hostMem);
        return false;
    }
    logMsg("GxmRenderer: context created");

    // --- Create render target ---
    SceGxmRenderTargetParams rtParams;
    memset(&rtParams, 0, sizeof(SceGxmRenderTargetParams));
    rtParams.flags                = 0;
    rtParams.width                = DISPLAY_WIDTH;
    rtParams.height               = DISPLAY_HEIGHT;
    rtParams.scenesPerFrame       = 1;
    rtParams.multisampleMode      = SCE_GXM_MULTISAMPLE_NONE;
    rtParams.multisampleLocations = 0;
    rtParams.driverMemBlock       = -1; // let the system allocate

    err = sceGxmCreateRenderTarget(&rtParams, &m_renderTarget);
    if (err < 0) {
        logMsg("GxmRenderer: sceGxmCreateRenderTarget failed: 0x%08X", err);
        return false;
    }
    logMsg("GxmRenderer: render target created (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // --- Allocate display buffers (triple buffering) ---
    for (int i = 0; i < DISPLAY_BUFFER_COUNT; ++i) {
        m_displayBuffers[i].data = gpuAlloc(
            SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
            DISPLAY_STRIDE * DISPLAY_HEIGHT * 4,  // RGBA8888 = 4 bytes/pixel
            256,
            SCE_GXM_MEMORY_ATTRIB_RW,
            &m_displayBuffers[i].uid);

        if (!m_displayBuffers[i].data) {
            logMsg("GxmRenderer: failed to allocate display buffer %d", i);
            return false;
        }

        // Clear the buffer
        memset(m_displayBuffers[i].data, 0, DISPLAY_STRIDE * DISPLAY_HEIGHT * 4);

        // Initialize color surface
        err = sceGxmColorSurfaceInit(
            &m_displayBuffers[i].surface,
            SCE_GXM_COLOR_FORMAT_A8B8G8R8,
            SCE_GXM_COLOR_SURFACE_LINEAR,
            SCE_GXM_COLOR_SURFACE_SCALE_NONE,
            SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            DISPLAY_STRIDE,
            m_displayBuffers[i].data);
        if (err < 0) {
            logMsg("GxmRenderer: sceGxmColorSurfaceInit failed for buffer %d: 0x%08X", i, err);
            return false;
        }

        // Create sync object for this buffer
        err = sceGxmSyncObjectCreate(&m_displayBuffers[i].sync);
        if (err < 0) {
            logMsg("GxmRenderer: sceGxmSyncObjectCreate failed for buffer %d: 0x%08X", i, err);
            return false;
        }
    }
    logMsg("GxmRenderer: %d display buffers allocated", DISPLAY_BUFFER_COUNT);

    // --- Allocate depth/stencil buffer ---
    const uint32_t alignedWidth = (DISPLAY_WIDTH + SCE_GXM_TILE_SIZEX - 1) & ~(SCE_GXM_TILE_SIZEX - 1);
    const uint32_t alignedHeight = (DISPLAY_HEIGHT + SCE_GXM_TILE_SIZEY - 1) & ~(SCE_GXM_TILE_SIZEY - 1);
    const uint32_t depthStrideInSamples = alignedWidth;
    const uint32_t depthStencilSize = 4 * alignedWidth * alignedHeight;

    m_depthBufferData = gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        depthStencilSize, SCE_GXM_DEPTHSTENCIL_SURFACE_ALIGNMENT,
        SCE_GXM_MEMORY_ATTRIB_RW,
        &m_depthBufferUid);
    if (!m_depthBufferData) {
        logMsg("GxmRenderer: failed to allocate depth buffer");
        return false;
    }

    err = sceGxmDepthStencilSurfaceInit(
        &m_depthSurface,
        SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
        SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,
        depthStrideInSamples,
        m_depthBufferData,
        nullptr); // no stencil-only buffer
    if (err < 0) {
        logMsg("GxmRenderer: sceGxmDepthStencilSurfaceInit failed: 0x%08X", err);
        return false;
    }
    logMsg("GxmRenderer: depth/stencil surface created");

    // --- Set up shader patcher ---
    static const uint32_t PATCHER_BUFFER_SIZE        = 64 * 1024;
    static const uint32_t PATCHER_VERTEX_USSE_SIZE   = 64 * 1024;
    static const uint32_t PATCHER_FRAGMENT_USSE_SIZE = 64 * 1024;

    void* patcherBuffer = gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        PATCHER_BUFFER_SIZE, 4,
        SCE_GXM_MEMORY_ATTRIB_RW,
        &m_patcherBufferUid);
    if (!patcherBuffer) return false;

    uint32_t patcherVertexUsseOffset = 0;
    void* patcherVertexUsse = vertexUsseAlloc(
        PATCHER_VERTEX_USSE_SIZE,
        &m_patcherVertexUsseUid,
        &patcherVertexUsseOffset);
    if (!patcherVertexUsse) return false;

    uint32_t patcherFragmentUsseOffset = 0;
    void* patcherFragmentUsse = fragmentUsseAlloc(
        PATCHER_FRAGMENT_USSE_SIZE,
        &m_patcherFragmentUsseUid,
        &patcherFragmentUsseOffset);
    if (!patcherFragmentUsse) return false;

    auto patcherHostAlloc = [](void* /*userData*/, uint32_t size) -> void* {
        return malloc(size);
    };
    auto patcherHostFree = [](void* /*userData*/, void* mem) {
        free(mem);
    };

    SceGxmShaderPatcherParams patcherParams;
    memset(&patcherParams, 0, sizeof(SceGxmShaderPatcherParams));
    patcherParams.userData                  = nullptr;
    patcherParams.hostAllocCallback         = +patcherHostAlloc;
    patcherParams.hostFreeCallback          = +patcherHostFree;
    patcherParams.bufferAllocCallback       = nullptr;
    patcherParams.bufferFreeCallback        = nullptr;
    patcherParams.bufferMem                 = patcherBuffer;
    patcherParams.bufferMemSize             = PATCHER_BUFFER_SIZE;
    patcherParams.vertexUsseAllocCallback   = nullptr;
    patcherParams.vertexUsseFreeCallback    = nullptr;
    patcherParams.vertexUsseMem             = patcherVertexUsse;
    patcherParams.vertexUsseMemSize         = PATCHER_VERTEX_USSE_SIZE;
    patcherParams.vertexUsseOffset          = patcherVertexUsseOffset;
    patcherParams.fragmentUsseAllocCallback = nullptr;
    patcherParams.fragmentUsseFreeCallback  = nullptr;
    patcherParams.fragmentUsseMem           = patcherFragmentUsse;
    patcherParams.fragmentUsseMemSize       = PATCHER_FRAGMENT_USSE_SIZE;
    patcherParams.fragmentUsseOffset        = patcherFragmentUsseOffset;

    err = sceGxmShaderPatcherCreate(&patcherParams, &m_shaderPatcher);
    if (err < 0) {
        logMsg("GxmRenderer: sceGxmShaderPatcherCreate failed: 0x%08X", err);
        return false;
    }
    logMsg("GxmRenderer: shader patcher created");

    // --- Create shaders ---
    if (!createShaders()) {
        logMsg("GxmRenderer: shader creation failed");
        return false;
    }

    // --- Create clear geometry (fullscreen quad) ---
    m_clearVertices = static_cast<BasicVertex*>(gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        4 * sizeof(BasicVertex), 4,
        SCE_GXM_MEMORY_ATTRIB_READ,
        &m_clearVerticesUid));
    if (!m_clearVertices) return false;

    m_clearIndices = static_cast<uint16_t*>(gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        6 * sizeof(uint16_t), 4,
        SCE_GXM_MEMORY_ATTRIB_READ,
        &m_clearIndicesUid));
    if (!m_clearIndices) return false;

    // Fullscreen quad vertices (NDC coords: -1 to +1, Z=1.0 for far plane depth clear)
    m_clearVertices[0].position[0] = -1.0f; m_clearVertices[0].position[1] = -1.0f; m_clearVertices[0].position[2] = 1.0f;
    m_clearVertices[1].position[0] =  1.0f; m_clearVertices[1].position[1] = -1.0f; m_clearVertices[1].position[2] = 1.0f;
    m_clearVertices[2].position[0] = -1.0f; m_clearVertices[2].position[1] =  1.0f; m_clearVertices[2].position[2] = 1.0f;
    m_clearVertices[3].position[0] =  1.0f; m_clearVertices[3].position[1] =  1.0f; m_clearVertices[3].position[2] = 1.0f;

    // Zero out normals and UVs for clear vertices (unused by clear shader)
    for (int i = 0; i < 4; ++i) {
        m_clearVertices[i].normal[0] = 0.0f;
        m_clearVertices[i].normal[1] = 0.0f;
        m_clearVertices[i].normal[2] = 1.0f;
        m_clearVertices[i].uv[0] = 0.0f;
        m_clearVertices[i].uv[1] = 0.0f;
    }

    // Two triangles forming a quad
    m_clearIndices[0] = 0; m_clearIndices[1] = 1; m_clearIndices[2] = 2;
    m_clearIndices[3] = 2; m_clearIndices[4] = 1; m_clearIndices[5] = 3;

    // --- Initialize camera defaults ---
    m_viewMatrix = Mat4::identity();
    m_projMatrix = Mat4::perspective(
        60.0f * (3.14159265f / 180.0f),  // 60 degrees FOV
        (float)DISPLAY_WIDTH / (float)DISPLAY_HEIGHT,
        0.1f,
        1000.0f);
    m_wvpMatrix = m_projMatrix * m_viewMatrix;

    m_frontBufferIndex = 0;
    m_backBufferIndex  = 0;
    m_initialized = true;
    logMsg("GxmRenderer: initialization complete");
    return true;
}

// ============================================================================
// Shader creation
// ============================================================================

bool GxmRenderer::createShaders() {
    int err;

    // --- Register clear vertex program ---
    const SceGxmProgram* clearVertexProg =
        reinterpret_cast<const SceGxmProgram*>(clearVertexProgramGxp);
    err = sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, clearVertexProg,
                                              &m_clearVertexProgramId);
    if (err < 0) {
        logMsg("GxmRenderer: failed to register clear vertex program: 0x%08X", err);
        return false;
    }

    // --- Register clear fragment program ---
    const SceGxmProgram* clearFragmentProg =
        reinterpret_cast<const SceGxmProgram*>(clearFragmentProgramGxp);
    err = sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, clearFragmentProg,
                                              &m_clearFragmentProgramId);
    if (err < 0) {
        logMsg("GxmRenderer: failed to register clear fragment program: 0x%08X", err);
        return false;
    }

    // --- Register basic vertex program ---
    const SceGxmProgram* basicVertexProg =
        reinterpret_cast<const SceGxmProgram*>(basicVertexProgramGxp);
    err = sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, basicVertexProg,
                                              &m_basicVertexProgramId);
    if (err < 0) {
        logMsg("GxmRenderer: failed to register basic vertex program: 0x%08X", err);
        return false;
    }

    // --- Register basic fragment program ---
    const SceGxmProgram* basicFragmentProg =
        reinterpret_cast<const SceGxmProgram*>(basicFragmentProgramGxp);
    err = sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, basicFragmentProg,
                                              &m_basicFragmentProgramId);
    if (err < 0) {
        logMsg("GxmRenderer: failed to register basic fragment program: 0x%08X", err);
        return false;
    }

    // --- Register texture vertex program ---
    const SceGxmProgram* texVertexProg =
        reinterpret_cast<const SceGxmProgram*>(textureVertexProgramGxp);
    err = sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, texVertexProg,
                                              &m_textureVertexProgramId);
    if (err < 0) {
        logMsg("GxmRenderer: failed to register texture vertex program: 0x%08X", err);
        return false;
    }

    // --- Register texture fragment program ---
    const SceGxmProgram* texFragmentProg =
        reinterpret_cast<const SceGxmProgram*>(textureFragmentProgramGxp);
    err = sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, texFragmentProg,
                                              &m_textureFragmentProgramId);
    if (err < 0) {
        logMsg("GxmRenderer: failed to register texture fragment program: 0x%08X", err);
        return false;
    }

    // --- Look up uniform parameters ---
    m_paramClearColor = sceGxmProgramFindParameterByName(clearFragmentProg, "uClearColor");
    if (!m_paramClearColor) {
        logMsg("GxmRenderer: 'uClearColor' uniform not found in clear fragment shader");
        return false;
    }

    m_paramWVP = sceGxmProgramFindParameterByName(basicVertexProg, "wvp");
    if (!m_paramWVP) {
        logMsg("GxmRenderer: 'wvp' uniform not found in basic vertex shader");
        return false;
    }

    m_paramTextureWVP = sceGxmProgramFindParameterByName(texVertexProg, "wvp");
    if (!m_paramTextureWVP) {
        logMsg("GxmRenderer: 'wvp' uniform not found in texture vertex shader");
        return false;
    }

    // --- Define vertex attribute format for BasicVertex ---
    // position: 3 floats at offset 0
    // normal:   3 floats at offset 12
    // color:    4 floats at offset 24
    // uv:       2 floats at offset 40
    // total stride: 48 bytes

    // Clear shader vertex attributes (only uses position)
    const SceGxmProgramParameter* clearAttrPosition =
        sceGxmProgramFindParameterByName(clearVertexProg, "aPosition");

    SceGxmVertexAttribute clearAttributes[1];
    clearAttributes[0].streamIndex    = 0;
    clearAttributes[0].offset         = 0;
    clearAttributes[0].format         = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    clearAttributes[0].componentCount = 2;
    clearAttributes[0].regIndex       = sceGxmProgramParameterGetResourceIndex(clearAttrPosition);

    SceGxmVertexStream clearStreams[1];
    clearStreams[0].stride      = sizeof(BasicVertex);
    clearStreams[0].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

    err = sceGxmShaderPatcherCreateVertexProgram(
        m_shaderPatcher, m_clearVertexProgramId,
        clearAttributes, 1,
        clearStreams, 1,
        &m_clearVertexProgram);
    if (err < 0) {
        logMsg("GxmRenderer: failed to create clear vertex program: 0x%08X", err);
        return false;
    }

    // Clear fragment program
    SceGxmBlendInfo clearBlendInfo;
    memset(&clearBlendInfo, 0, sizeof(SceGxmBlendInfo));
    clearBlendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
    clearBlendInfo.colorFunc = SCE_GXM_BLEND_FUNC_NONE;
    clearBlendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
    clearBlendInfo.colorSrc  = SCE_GXM_BLEND_FACTOR_ONE;
    clearBlendInfo.colorDst  = SCE_GXM_BLEND_FACTOR_ZERO;
    clearBlendInfo.alphaSrc  = SCE_GXM_BLEND_FACTOR_ONE;
    clearBlendInfo.alphaDst  = SCE_GXM_BLEND_FACTOR_ZERO;

    err = sceGxmShaderPatcherCreateFragmentProgram(
        m_shaderPatcher, m_clearFragmentProgramId,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
        SCE_GXM_MULTISAMPLE_NONE,
        &clearBlendInfo,
        clearVertexProg,
        &m_clearFragmentProgram);
    if (err < 0) {
        logMsg("GxmRenderer: failed to create clear fragment program: 0x%08X", err);
        return false;
    }

    // Basic shader vertex attributes (position + color)
    const SceGxmProgramParameter* basicAttrPosition =
        sceGxmProgramFindParameterByName(basicVertexProg, "aPosition");
    const SceGxmProgramParameter* basicAttrColor =
        sceGxmProgramFindParameterByName(basicVertexProg, "aColor");

    SceGxmVertexAttribute basicAttributes[2];

    // Position: 3 floats at offset 0
    basicAttributes[0].streamIndex    = 0;
    basicAttributes[0].offset         = offsetof(BasicVertex, position);
    basicAttributes[0].format         = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    basicAttributes[0].componentCount = 3;
    basicAttributes[0].regIndex       = sceGxmProgramParameterGetResourceIndex(basicAttrPosition);

    // Color: 4 floats
    basicAttributes[1].streamIndex    = 0;
    basicAttributes[1].offset         = offsetof(BasicVertex, color);
    basicAttributes[1].format         = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    basicAttributes[1].componentCount = 4;
    basicAttributes[1].regIndex       = sceGxmProgramParameterGetResourceIndex(basicAttrColor);

    SceGxmVertexStream basicStreams[1];
    basicStreams[0].stride      = sizeof(BasicVertex);
    basicStreams[0].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

    err = sceGxmShaderPatcherCreateVertexProgram(
        m_shaderPatcher, m_basicVertexProgramId,
        basicAttributes, 2,
        basicStreams, 1,
        &m_basicVertexProgram);
    if (err < 0) {
        logMsg("GxmRenderer: failed to create basic vertex program: 0x%08X", err);
        return false;
    }

    // Basic fragment program (alpha blending enabled for 2D UI & transparent elements)
    SceGxmBlendInfo basicBlendInfo;
    memset(&basicBlendInfo, 0, sizeof(SceGxmBlendInfo));
    basicBlendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
    basicBlendInfo.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    basicBlendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    basicBlendInfo.colorSrc  = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    basicBlendInfo.colorDst  = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    basicBlendInfo.alphaSrc  = SCE_GXM_BLEND_FACTOR_ONE;
    basicBlendInfo.alphaDst  = SCE_GXM_BLEND_FACTOR_ZERO;

    err = sceGxmShaderPatcherCreateFragmentProgram(
        m_shaderPatcher, m_basicFragmentProgramId,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
        SCE_GXM_MULTISAMPLE_NONE,
        &basicBlendInfo,
        basicVertexProg,
        &m_basicFragmentProgram);
    if (err < 0) {
        logMsg("GxmRenderer: failed to create basic fragment program: 0x%08X", err);
        return false;
    }

    // Texture shader vertex attributes (position + texcoord)
    const SceGxmProgramParameter* texAttrPosition =
        sceGxmProgramFindParameterByName(texVertexProg, "aPosition");
    const SceGxmProgramParameter* texAttrTexcoord =
        sceGxmProgramFindParameterByName(texVertexProg, "aTexcoord");

    SceGxmVertexAttribute texAttributes[2];

    // Position: 3 floats at offset 0
    texAttributes[0].streamIndex    = 0;
    texAttributes[0].offset         = offsetof(BasicVertex, position);
    texAttributes[0].format         = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    texAttributes[0].componentCount = 3;
    texAttributes[0].regIndex       = sceGxmProgramParameterGetResourceIndex(texAttrPosition);

    // Texcoord: 2 floats at offset uv
    texAttributes[1].streamIndex    = 0;
    texAttributes[1].offset         = offsetof(BasicVertex, uv);
    texAttributes[1].format         = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    texAttributes[1].componentCount = 2;
    texAttributes[1].regIndex       = sceGxmProgramParameterGetResourceIndex(texAttrTexcoord);

    SceGxmVertexStream texStreams[1];
    texStreams[0].stride      = sizeof(BasicVertex);
    texStreams[0].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

    err = sceGxmShaderPatcherCreateVertexProgram(
        m_shaderPatcher, m_textureVertexProgramId,
        texAttributes, 2,
        texStreams, 1,
        &m_textureVertexProgram);
    if (err < 0) {
        logMsg("GxmRenderer: failed to create texture vertex program: 0x%08X", err);
        return false;
    }

    // Texture fragment program
    SceGxmBlendInfo texBlendInfo;
    memset(&texBlendInfo, 0, sizeof(SceGxmBlendInfo));
    texBlendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
    texBlendInfo.colorFunc = SCE_GXM_BLEND_FUNC_NONE;
    texBlendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
    texBlendInfo.colorSrc  = SCE_GXM_BLEND_FACTOR_ONE;
    texBlendInfo.colorDst  = SCE_GXM_BLEND_FACTOR_ZERO;
    texBlendInfo.alphaSrc  = SCE_GXM_BLEND_FACTOR_ONE;
    texBlendInfo.alphaDst  = SCE_GXM_BLEND_FACTOR_ZERO;

    err = sceGxmShaderPatcherCreateFragmentProgram(
        m_shaderPatcher, m_textureFragmentProgramId,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
        SCE_GXM_MULTISAMPLE_NONE,
        &texBlendInfo,
        texVertexProg,
        &m_textureFragmentProgram);
    if (err < 0) {
        logMsg("GxmRenderer: failed to create texture fragment program: 0x%08X", err);
        return false;
    }

    // UI Texture fragment program (with alpha blending)
    SceGxmBlendInfo uiBlendInfo;
    memset(&uiBlendInfo, 0, sizeof(SceGxmBlendInfo));
    uiBlendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
    uiBlendInfo.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    uiBlendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    uiBlendInfo.colorSrc  = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    uiBlendInfo.colorDst  = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    uiBlendInfo.alphaSrc  = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    uiBlendInfo.alphaDst  = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    err = sceGxmShaderPatcherCreateFragmentProgram(
        m_shaderPatcher, m_textureFragmentProgramId,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
        SCE_GXM_MULTISAMPLE_NONE,
        &uiBlendInfo,
        texVertexProg,
        &m_uiTextureFragmentProgram);
    if (err < 0) {
        logMsg("GxmRenderer: failed to create UI texture fragment program: 0x%08X", err);
        return false;
    }

    logMsg("GxmRenderer: shaders created successfully");
    return true;
}

// ============================================================================
// Frame rendering
// ============================================================================

void GxmRenderer::beginFrame() {
    waitForGpu();
    m_frameStats={};m_drawState.valid=false;
    m_frameVertexCount = 0;
    m_frameIndexCount = 0;
    if (!m_frameVertices) {
        m_frameVertices = static_cast<BasicVertex*>(gpuAlloc(
            SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
            FRAME_VERTEX_CAPACITY * sizeof(BasicVertex), 16,
            SCE_GXM_MEMORY_ATTRIB_READ, &m_frameVerticesUid));
    }
    if (!m_frameIndices) {
        m_frameIndices = static_cast<uint16_t*>(gpuAlloc(
            SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
            FRAME_INDEX_CAPACITY * sizeof(uint16_t), 4,
            SCE_GXM_MEMORY_ATTRIB_READ, &m_frameIndicesUid));
    }
    int err = sceGxmBeginScene(
        m_context,
        0,
        m_renderTarget,
        nullptr,
        nullptr,
        m_displayBuffers[m_backBufferIndex].sync,
        &m_displayBuffers[m_backBufferIndex].surface,
        &m_depthSurface);
    m_sceneActive = (err >= 0);
    if (err < 0) logMsg("GXM: begin scene failed: 0x%08X", err);
}

void GxmRenderer::endFrame() {
    endStaticBatch();
    uint32_t presentStart=renderCpuTime();
    if (!m_sceneActive) return;
    int err = sceGxmEndScene(m_context, nullptr, nullptr);
    m_sceneActive = false;
    m_gpuWorkPending = true;
    if (err < 0) {
        logMsg("GXM: end scene failed: 0x%08X", err);
        waitForGpu();
        return;
    }

    // Send heartbeat to prevent GPU watchdog timeout
    sceGxmPadHeartbeat(
        &m_displayBuffers[m_backBufferIndex].surface,
        m_displayBuffers[m_backBufferIndex].sync);

    // Queue display callback to swap this buffer onto the screen
    DisplayCallbackData callbackData;
    callbackData.address = m_displayBuffers[m_backBufferIndex].data;

    sceGxmDisplayQueueAddEntry(
        m_displayBuffers[m_frontBufferIndex].sync,  // old buffer sync
        m_displayBuffers[m_backBufferIndex].sync,   // new buffer sync
        &callbackData);

    // Advance buffer indices
    m_frontBufferIndex = m_backBufferIndex;
    m_backBufferIndex  = (m_backBufferIndex + 1) % DISPLAY_BUFFER_COUNT;
    m_frameStats.presentUs=renderCpuTime()-presentStart;
}

void GxmRenderer::waitForGpu() {
    if (m_context && m_gpuWorkPending && !m_sceneActive) {
        sceGxmFinish(m_context);
        m_gpuWorkPending = false;
    }
}

bool GxmRenderer::copyFrameGeometry(const BasicVertex*& vertices, uint32_t vertexCount,
                                    const uint16_t*& indices, uint32_t indexCount, bool triangleList) {
    if (!m_sceneActive || !vertices || !indices || vertexCount == 0 || indexCount == 0 ||
        (triangleList && indexCount % 3 != 0) || !m_frameVertices || !m_frameIndices) return false;
    uint32_t firstIndex = (m_frameIndexCount + 1u) & ~1u;
    if (vertexCount > FRAME_VERTEX_CAPACITY - m_frameVertexCount ||
        indexCount > FRAME_INDEX_CAPACITY - firstIndex) {
        logMsg("GXM: frame geometry capacity exceeded (%u vertices, %u indices)",
               vertexCount, indexCount);
        return false;
    }
    for (uint32_t i = 0; i < indexCount; ++i) {
        if (indices[i] >= vertexCount) {
            logMsg("GXM: rejected index %u for %u vertices", indices[i], vertexCount);
            return false;
        }
    }
    BasicVertex* frameVertices = m_frameVertices + m_frameVertexCount;
    uint16_t* frameIndices = m_frameIndices + firstIndex;
    memcpy(frameVertices, vertices, vertexCount * sizeof(BasicVertex));
    memcpy(frameIndices, indices, indexCount * sizeof(uint16_t));
    vertices = frameVertices;
    indices = frameIndices;
    m_frameVertexCount += vertexCount;
    m_frameIndexCount = firstIndex + indexCount;
    return true;
}

void GxmRenderer::clear(float r, float g, float b, float a) {
    endStaticBatch();m_drawState.valid=false;
    if (!m_sceneActive) return;
    // Disable culling and set depth test to ALWAYS for clear quad
    sceGxmSetCullMode(m_context, SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmSetBackDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_ENABLED);

    // Bind clear shaders
    sceGxmSetVertexProgram(m_context, m_clearVertexProgram);
    sceGxmSetFragmentProgram(m_context, m_clearFragmentProgram);

    // Set the clear color uniform
    void* uniformBuffer = nullptr;
    if (sceGxmReserveFragmentDefaultUniformBuffer(m_context, &uniformBuffer) < 0 || !uniformBuffer) {
        logMsg("GXM: clear uniform allocation failed");
        return;
    }
    float clearColor[4] = { r, g, b, a };
    sceGxmSetUniformDataF(uniformBuffer, m_paramClearColor, 0, 4, clearColor);

    // Draw the fullscreen quad
    sceGxmSetVertexStream(m_context, 0, m_clearVertices);
    sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES,
               SCE_GXM_INDEX_FORMAT_U16, m_clearIndices, 6);
}

void GxmRenderer::distanceFog(float start,float end,const float* color) {
    if(!m_sceneActive || !color || end<=start || m_projMatrix.m[2][3]>=-.5f)return;
    // Depth-tested translucent layers approximate distance fog with the
    // existing color shader. Foreground opaque geometry occludes each layer.
    // No depth readback, shader recompilation or mutable queued buffers.
    BasicVertex vertices[24]{};alignas(4) uint16_t indices[36];
    for(unsigned layer=0;layer<6;++layer) {
        float depth=start+(end-start)*(layer+1)/6;
        for(unsigned k=0;k<4;++k) {
            auto& v=vertices[layer*4+k];v.position[0]=((k&1)?1.f:-1.f)*depth/m_projMatrix.m[0][0]*1.001f;
            v.position[1]=((k>>1)?1.f:-1.f)*depth/m_projMatrix.m[1][1]*1.001f;v.position[2]=-depth;
            for(unsigned c=0;c<3;++c)v.color[c]=color[c];
            v.color[3]=1.f/(6-layer);
        }
        const unsigned order[]={0,1,2,2,1,3};for(unsigned k=0;k<6;++k)indices[layer*6+k]=layer*4+order[k];
    }
    GxmMesh fog;fog.vertices=vertices;fog.indices=indices;fog.vertexCount=24;fog.indexCount=36;fog.dynamic=true;fog.transparent=true;
    Mat4 view=m_viewMatrix,projection=m_projMatrix;setViewProjection(Mat4::identity(),projection);
    drawMeshInstance(fog,nullptr,Mat4::identity());setViewProjection(view,projection);
}

void GxmRenderer::setCamera(float distance, float pitchDeg, float yawDeg,
                            float targetX, float targetY, float targetZ) {
    endStaticBatch();
    static const float DEG_TO_RAD = 3.14159265f / 180.0f;

    float pitchRad = pitchDeg * DEG_TO_RAD;
    float yawRad   = yawDeg   * DEG_TO_RAD;

    // Gamebryo models have Z as UP axis.
    // Orbit around the target point in XY with Z elevation:
    float eyeX = targetX + distance * cosf(pitchRad) * sinf(yawRad);
    float eyeY = targetY - distance * cosf(pitchRad) * cosf(yawRad);
    float eyeZ = targetZ + distance * sinf(pitchRad);

    m_viewMatrix = Mat4::lookAt(eyeX, eyeY, eyeZ,
                                targetX, targetY, targetZ,
                                0.0f, 0.0f, 1.0f);   // Z-up for Bethesda models

    m_projMatrix = Mat4::perspective(
        60.0f * DEG_TO_RAD,
        (float)DISPLAY_WIDTH / (float)DISPLAY_HEIGHT,
        1.0f,
        5000.0f);

    m_viewProjMatrix = m_projMatrix * m_viewMatrix;
    m_wvpMatrix = m_viewProjMatrix;
}

void GxmRenderer::setViewProjection(const Mat4& view, const Mat4& proj) {
    endStaticBatch();
    m_viewMatrix = view;
    m_projMatrix = proj;
    m_viewProjMatrix = m_projMatrix * m_viewMatrix;
    m_wvpMatrix = m_viewProjMatrix;
}

void GxmRenderer::setCameraFreecam(float eyeX, float eyeY, float eyeZ, float pitchRad, float yawRad, float fovDeg, float farPlane) {
    endStaticBatch();
    static const float DEG_TO_RAD = 3.14159265f / 180.0f;

    // View direction vector (Gamebryo Z-up)
    // Yaw 0 = looking North (+Y), Yaw 90 = looking East (+X)
    float fx = cosf(pitchRad) * sinf(yawRad);
    float fy = cosf(pitchRad) * cosf(yawRad);
    float fz = sinf(pitchRad);

    float targetX = eyeX + fx;
    float targetY = eyeY + fy;
    float targetZ = eyeZ + fz;

    m_viewMatrix = Mat4::lookAt(eyeX, eyeY, eyeZ,
                                targetX, targetY, targetZ,
                                0.0f, 0.0f, 1.0f); // Z-up for Bethesda models

    m_projMatrix = Mat4::perspective(
        fovDeg * DEG_TO_RAD,
        (float)DISPLAY_WIDTH / (float)DISPLAY_HEIGHT,
        2.0f,
        farPlane);

    m_viewProjMatrix = m_projMatrix * m_viewMatrix;
    m_wvpMatrix = m_viewProjMatrix;
}

void GxmRenderer::drawMesh() {
    if (!m_hasMesh) return;

    GxmMesh currentMesh;
    currentMesh.vertices = m_meshVertices;
    currentMesh.verticesUid = m_meshVerticesUid;
    currentMesh.indices = m_meshIndices;
    currentMesh.indicesUid = m_meshIndicesUid;
    currentMesh.indexCount = m_meshIndexCount;
    currentMesh.vertexCount = m_meshVertexCount;
    currentMesh.parts = m_meshParts;

    GxmTexture currentTex;
    currentTex.texture = m_gxmTexture;
    currentTex.data = m_textureData;
    currentTex.dataUid = m_textureDataUid;

    drawMeshInstance(currentMesh, m_hasTexture ? &currentTex : nullptr, Mat4::identity());
}

void GxmRenderer::drawMeshInstance(const GxmMesh& mesh, const GxmTexture* texture, const Mat4& worldMatrix, int pass) {
    if (!m_sceneActive || !mesh.isValid()) return;
    Mat4 wvp = m_viewProjMatrix * worldMatrix;++m_frameStats.wvpBuilds;
    drawMeshWithWvp(mesh,texture,wvp,pass,m_projMatrix.m[2][3]<-.5f);
}
void GxmRenderer::drawMeshWithWvp(const GxmMesh& mesh,const GxmTexture* texture,const Mat4& wvp,int pass,bool perspective) {
    FrustumClipper frustum(wvp);
    int clipMask=63;
    if(perspective && mesh.hasBounds && !mesh.dynamic) {
        ++m_frameStats.boundsTests;
        clipMask=frustum.classify(mesh.boundsMin,mesh.boundsMax);
        if(clipMask<0) {++m_frameStats.culled;return;}
    }
    // Snapshot a whole animated mesh once, before splitting it into material draws.
    // Each queued material draw must see the same immutable vertex frame.
    if(mesh.dynamic && !mesh.parts.empty()) {
        GxmMesh snapshot;snapshot.vertexCount=mesh.vertexCount;snapshot.indexCount=mesh.indexCount;
        snapshot.parts=mesh.parts;snapshot.transparent=mesh.transparent;snapshot.clustersValid=false;
        const BasicVertex* v=mesh.vertices;const uint16_t* i=mesh.indices;
        // The packed buffer includes alignment words BETWEEN triangle lists.
        // Only each material range must be divisible by three, not this buffer.
        if(!copyFrameGeometry(v,mesh.vertexCount,i,mesh.indexCount,false)) return;
        snapshot.vertices=const_cast<BasicVertex*>(v);snapshot.indices=const_cast<uint16_t*>(i);
        snapshot.dynamic=false;snapshot.hasBounds=false;
        snapshot.cpuDynamicVertices=mesh.cpuDynamicVertices;snapshot.cpuDynamicIndices=mesh.cpuDynamicIndices;
        drawMeshWithWvp(snapshot,texture,wvp,pass,perspective);return;
    }
    if (!mesh.parts.empty()) {
        for (const auto& part : mesh.parts) {
            if(pass>=0 && part.transparent!=(pass==1)) continue;
            if (part.firstIndex > mesh.indexCount || part.indexCount > mesh.indexCount - part.firstIndex || part.indexCount%3) continue;
            GxmMesh section;
            section.vertices = mesh.vertices;
            section.vertexCount = mesh.vertexCount;
            section.indices = mesh.indices + part.firstIndex;
            section.indexCount = part.indexCount;
            section.dynamic = mesh.dynamic;
            if(!mesh.dynamic) {section.cpuGeometry=mesh.cpuGeometry;section.cpuIndexOffset=mesh.cpuIndexOffset+part.firstIndex;}
            section.cpuDynamicVertices=mesh.cpuDynamicVertices;
            section.cpuDynamicIndices=mesh.cpuDynamicIndices?mesh.cpuDynamicIndices+part.firstIndex:nullptr;
            section.transparent = part.transparent;
            section.hasBounds=mesh.hasBounds;section.boundsMin=part.boundsMin;section.boundsMax=part.boundsMax;
            const GxmTexture* material = part.texturePath.empty() ? texture : part.texture;
            if(mesh.clustersValid && !mesh.dynamic && part.clusterCount) {
                section.clusterView=mesh.clipClusters.data()+part.firstCluster;section.clusterViewCount=part.clusterCount;
            }
            drawMeshWithWvp(section,material,wvp,pass,perspective);
        }
        return;
    }
    if(pass>=0 && mesh.transparent!=(pass==1)) return;
    const BasicVertex* vertices = mesh.vertices;
    const uint16_t* indices = mesh.indices;
    if (mesh.dynamic && !copyFrameGeometry(vertices, mesh.vertexCount, indices, mesh.indexCount)) return;

    DrawJob job;job.vertices=vertices;job.indices=indices;job.vertexCount=mesh.vertexCount;
    job.indexCount=mesh.indexCount;job.wvp=wvp;job.texture=texture;
    job.cpuVertices=mesh.cpuDynamicVertices;job.cpuIndices=mesh.cpuDynamicIndices;
    if(m_optimizeSubmission && !mesh.dynamic && mesh.cpuGeometry) {job.cpuPositions=mesh.cpuGeometry->positions.data();job.cpuIndices=mesh.cpuGeometry->indices.data()+mesh.cpuIndexOffset;}
    job.transparent=mesh.transparent;job.clipMask=perspective?clipMask:0;
    if(m_optimizeSubmission && mesh.clustersValid && !mesh.dynamic) {
        job.clusters=mesh.clusterView?mesh.clusterView:mesh.clipClusters.data();
        job.clusterCount=mesh.clusterView?mesh.clusterViewCount:mesh.clipClusters.size();
    }
    if(m_staticBatch && !mesh.dynamic) {
        if(m_jobCount && (m_jobCount==PREPARE_BATCH_SIZE || m_jobIndices+job.indexCount>PREPARE_INDEX_BUDGET))flushStaticBatch();
        m_drawJobs[m_jobCount++]=job;m_jobIndices+=job.indexCount;
        if(m_jobCount==PREPARE_BATCH_SIZE || m_jobIndices>=PREPARE_INDEX_BUDGET)flushStaticBatch();
    } else {
        flushStaticBatch();
        prepareDraw(job,m_clipResults[0],m_clipScratch[0]);submitDraw(job,m_clipResults[0]);
    }
}

void GxmRenderer::beginStaticBatch() {
    if(m_staticBatch)return;
    m_staticBatch=true;
    // Lazy startup also supports render tests that deliberately bypass init().
    if(m_parallelPreparation)m_workers.start();
    if(m_parallelPreparation && !m_workersLogged) {
        logMsg("CPU preparation: %u workers (Vita user cores 1/2), main thread participates; GPU submission on main",m_workers.workers());
        m_workersLogged=true;
    }
}
void GxmRenderer::endStaticBatch() { flushStaticBatch();m_staticBatch=false; }
void GxmRenderer::prepareJob(void* context,unsigned index,unsigned lane) {
    auto& r=*static_cast<GxmRenderer*>(context);
    r.prepareDraw(r.m_drawJobs[index],r.m_clipResults[index],r.m_clipScratch[lane]);
}
void GxmRenderer::flushStaticBatch() {
    if(!m_jobCount)return;
    uint32_t begin=renderCpuTime();
    m_workers.run(m_jobCount,prepareJob,this,m_parallelPreparation);
    uint32_t prepared=renderCpuTime();
    m_frameStats.prepareUs+=prepared-begin;
    for(unsigned lane=0;lane<3;++lane)m_frameStats.workerJobs[lane]+=m_workers.jobs(lane);
    for(unsigned i=0;i<m_jobCount;++i)submitDraw(m_drawJobs[i],m_clipResults[i]);
    m_frameStats.submitUs+=renderCpuTime()-prepared;
    // Retain at most 4 MiB of result capacities across batches. A large result
    // must not accumulate separately in every slot over hours of exploration.
    size_t retained=0;
    for(auto& result:m_clipResults) {
        retained+=result.front.capacity()*sizeof(uint16_t)+result.split.capacity()*sizeof(BasicVertex);
        if(retained>4*1024*1024) {
            retained-=result.front.capacity()*sizeof(uint16_t)+result.split.capacity()*sizeof(BasicVertex);
            std::vector<uint16_t>().swap(result.front);std::vector<BasicVertex>().swap(result.split);
        }
    }
    m_jobCount=m_jobIndices=0;
}
size_t GxmRenderer::preparationScratchBytes() const {
    size_t bytes=m_sequentialIndices.capacity()*sizeof(uint16_t);
    for(const auto& lane:m_clipScratch)bytes+=lane.codes.capacity()+lane.cachedVertices.capacity()*sizeof(BasicVertex)+lane.cachedIndices.capacity()*sizeof(uint16_t);
    for(const auto& result:m_clipResults)bytes+=result.front.capacity()*sizeof(uint16_t)+result.split.capacity()*sizeof(BasicVertex);
    return bytes;
}
void GxmRenderer::prepareDraw(const DrawJob& job,ClipResult& result,ClipScratch& scratch) {
    result.front.clear();result.split.clear();result.changed=false;result.valid=true;result.scanned=0;result.clusterInside=result.clusterOutside=result.triangleTests=0;
    if(!job.clipMask)return;
    auto& codes=scratch.codes;auto& cached=scratch.cachedVertices;
    codes.assign(job.vertexCount,0x80);cached.resize(job.vertexCount);
    const uint16_t* sourceIndices=job.cpuIndices?job.cpuIndices:job.indices;
    const BasicVertex* sourceVertices=job.cpuVertices?job.cpuVertices:job.vertices;
    scratch.cachedIndices.assign(sourceIndices,sourceIndices+job.indexCount);
    FrustumClipper frustum(job.wvp);unsigned clusterIndex=0;
    for(unsigned i=0;i+2<job.indexCount;i+=3) {
        if(clusterIndex<job.clusterCount && i==job.clusters[clusterIndex].firstIndex) {
            const auto& cluster=job.clusters[clusterIndex++];
            if(!cluster.indexCount || cluster.indexCount%3 || cluster.indexCount>job.indexCount-i) {result.valid=false;return;}
            int mask=frustum.classifyCluster(cluster.minimum,cluster.maximum);
            if(mask<=0) {
                if(mask<0){result.changed=true;result.clusterOutside+=cluster.indexCount/3;}
                else {result.front.insert(result.front.end(),scratch.cachedIndices.begin()+i,scratch.cachedIndices.begin()+i+cluster.indexCount);result.clusterInside+=cluster.indexCount/3;}
                i+=cluster.indexCount-3;continue;
            }
        }
        ++result.triangleTests;
        uint16_t ids[]={scratch.cachedIndices[i],scratch.cachedIndices[i+1],scratch.cachedIndices[i+2]};
        if(ids[0]>=job.vertexCount || ids[1]>=job.vertexCount || ids[2]>=job.vertexCount) {result.valid=false;return;}
        for(unsigned k=0;k<3;++k)if(codes[ids[k]]==0x80) {
            // Visibility needs only position. Fetch the remaining attributes
            // lazily for triangles that actually require interpolation.
            if(job.cpuPositions) {const auto& p=job.cpuPositions[ids[k]];cached[ids[k]].position[0]=p.x;cached[ids[k]].position[1]=p.y;cached[ids[k]].position[2]=p.z;}
            else for(unsigned axis=0;axis<3;++axis)cached[ids[k]].position[axis]=sourceVertices[ids[k]].position[axis];
            codes[ids[k]]=frustum.code(cached[ids[k]],job.clipMask);++result.scanned;
        }
        unsigned outside=(codes[ids[0]]|codes[ids[1]]|codes[ids[2]])&63;
        if(!outside)result.front.insert(result.front.end(),ids,ids+3);
        else {
            result.changed=true;
            if((codes[ids[0]]&codes[ids[1]]&codes[ids[2]])&63)continue;
            for(unsigned k=0;k<3;++k)if(!(codes[ids[k]]&64)) {
                cached[ids[k]]=sourceVertices[ids[k]];codes[ids[k]]|=64;
            }
            BasicVertex triangle[]={cached[ids[0]],cached[ids[1]],cached[ids[2]]},polygon[12];
            unsigned n=frustum.clip(triangle,outside,polygon);
            if(result.split.size()+(n>=3?(n-2)*3:0)>65535) {result.valid=false;return;}
            for(unsigned k=1;k+1<n;++k) {result.split.push_back(polygon[0]);result.split.push_back(polygon[k]);result.split.push_back(polygon[k+1]);}
        }
    }
}
void GxmRenderer::submitDraw(const DrawJob& job,const ClipResult& result) {
    m_frameStats.scannedVertices+=result.scanned;
    m_frameStats.clusterInside+=result.clusterInside;m_frameStats.clusterOutside+=result.clusterOutside;m_frameStats.triangleTests+=result.triangleTests;
    if(!result.valid) {logMsg("GXM: invalid or oversized prepared geometry");return;}
    const BasicVertex* vertices=job.vertices;const uint16_t* indices=job.indices;
    const BasicVertex* clippedVertices=nullptr;const uint16_t* clippedIndices=nullptr;
    uint32_t drawIndices=job.indexCount,clippedCount=0;
    if(result.changed) {
        drawIndices=result.front.size();
        if(drawIndices) {
            unsigned first=(m_frameIndexCount+1)&~1u;
            if(!m_frameIndices || first+drawIndices>FRAME_INDEX_CAPACITY) {logMsg("GXM: clip index capacity exhausted");return;}
            memcpy(m_frameIndices+first,result.front.data(),drawIndices*sizeof(uint16_t));
            indices=m_frameIndices+first;m_frameIndexCount=first+drawIndices;
        }
        if(!result.split.empty()) {
            auto& sequential=m_sequentialIndices;sequential.resize(result.split.size());
            for(unsigned i=0;i<sequential.size();++i)sequential[i]=i;
            clippedVertices=result.split.data();clippedIndices=sequential.data();clippedCount=result.split.size();
            m_frameStats.splitTriangles+=clippedCount/3;
            if(!copyFrameGeometry(clippedVertices,result.split.size(),clippedIndices,result.split.size()))return;
        }
    }
    if(!drawIndices && !clippedCount)return;
    if(!bindDrawState(job.wvp,job.texture,job.transparent))return;

    auto submit=[&](const BasicVertex* v,const uint16_t* i,uint32_t count) {
        if(!count) return;
        ++m_frameStats.draws;m_frameStats.triangles+=count/3;
        sceGxmSetVertexStream(m_context,0,v);
        int err=sceGxmDraw(m_context,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,i,count);
        if(err<0) {static unsigned errors=0;if(errors++<32) logMsg("GXM: mesh draw failed 0x%08X (%u indices)",err,count);}
    };
    submit(vertices,indices,drawIndices);
    submit(clippedVertices,clippedIndices,clippedCount);
}

bool GxmRenderer::bindDrawState(const Mat4& wvp,const GxmTexture* texture,bool transparent) {
    bool textured=texture && texture->isValid() && m_textureEnabled;
    auto vertex=textured?m_textureVertexProgram:m_basicVertexProgram;
    auto fragment=textured?(transparent && m_uiTextureFragmentProgram?m_uiTextureFragmentProgram:m_textureFragmentProgram):m_basicFragmentProgram;
    auto& state=m_drawState;bool valid=state.valid && m_optimizeSubmission;
    if(!valid) {
        sceGxmSetCullMode(m_context,SCE_GXM_CULL_NONE);
        sceGxmSetFrontDepthFunc(m_context,SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
        sceGxmSetBackDepthFunc(m_context,SCE_GXM_DEPTH_FUNC_LESS_EQUAL);m_frameStats.stateCalls+=3;
    }
    if(!valid || state.transparent!=transparent) {
        auto mode=transparent?SCE_GXM_DEPTH_WRITE_DISABLED:SCE_GXM_DEPTH_WRITE_ENABLED;
        sceGxmSetFrontDepthWriteEnable(m_context,mode);sceGxmSetBackDepthWriteEnable(m_context,mode);m_frameStats.stateCalls+=2;
    }
    bool vertexChanged=!valid || state.vertex!=vertex || state.textured!=textured;
    if(vertexChanged){sceGxmSetVertexProgram(m_context,vertex);++m_frameStats.stateCalls;}
    if(!valid || state.fragment!=fragment){sceGxmSetFragmentProgram(m_context,fragment);++m_frameStats.stateCalls;}
    if(vertexChanged || memcmp(&state.wvp,&wvp,sizeof(Mat4))) {
        void* buffer=nullptr;
        if(sceGxmReserveVertexDefaultUniformBuffer(m_context,&buffer)<0 || !buffer) {
            state.valid=false;logMsg("GXM: vertex uniform allocation failed");return false;
        }
        sceGxmSetUniformDataF(buffer,textured?m_paramTextureWVP:m_paramWVP,0,16,&wvp.m[0][0]);++m_frameStats.uniformWrites;
    }
    if(textured && (!valid || state.texture!=texture)) {
        sceGxmSetFragmentTexture(m_context,0,&texture->texture);++m_frameStats.stateCalls;
    }
    state.valid=true;state.vertex=vertex;state.fragment=fragment;state.texture=textured?texture:nullptr;
    state.textured=textured;state.transparent=transparent;state.wvp=wvp;return true;
}

bool GxmRenderer::allocateDynamicMesh(uint32_t maxVertices, uint32_t maxIndices, GxmMesh& outMesh) {
    destroyGxmMesh(outMesh);

    outMesh.vertices = static_cast<BasicVertex*>(gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        maxVertices * sizeof(BasicVertex), 4,
        SCE_GXM_MEMORY_ATTRIB_READ,
        &outMesh.verticesUid));
    if (!outMesh.vertices) {
        logMsg("allocateDynamicMesh: failed to allocate vertex buffer (%u verts)", maxVertices);
        return false;
    }

    outMesh.indices = static_cast<uint16_t*>(gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        maxIndices * sizeof(uint16_t), 4,
        SCE_GXM_MEMORY_ATTRIB_READ,
        &outMesh.indicesUid));
    if (!outMesh.indices) {
        logMsg("allocateDynamicMesh: failed to allocate index buffer (%u indices)", maxIndices);
        gpuFree(outMesh.verticesUid);
        outMesh.vertices = nullptr;
        outMesh.verticesUid = 0;
        return false;
    }

    outMesh.vertexCount = maxVertices;
    outMesh.indexCount  = maxIndices;
    outMesh.dynamic = true;
    return true;
}

void GxmRenderer::draw2D(const BasicVertex* vertices, uint32_t vertexCount,
                         const uint16_t* indices, uint32_t indexCount) {
    endStaticBatch();m_drawState.valid=false;
    if (!copyFrameGeometry(vertices, vertexCount, indices, indexCount)) return;

    // 2D UI: Disable culling and bypass depth test
    sceGxmSetCullMode(m_context, SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetBackDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_DISABLED);

    // Screen pixel orthographic projection: (0,0) top-left, (960,544) bottom-right
    Mat4 ortho = Mat4::orthographic(0.0f, 960.0f, 544.0f, 0.0f, -1.0f, 1.0f);

    sceGxmSetVertexProgram(m_context, m_basicVertexProgram);
    sceGxmSetFragmentProgram(m_context, m_basicFragmentProgram);

    void* uniformBuffer = nullptr;
    if (sceGxmReserveVertexDefaultUniformBuffer(m_context, &uniformBuffer) < 0 || !uniformBuffer) {
        logMsg("GXM: vertex uniform allocation failed");
        return;
    }
    sceGxmSetUniformDataF(uniformBuffer, m_paramWVP, 0, 16, &ortho.m[0][0]);

    sceGxmSetVertexStream(m_context, 0, vertices);
    sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES,
               SCE_GXM_INDEX_FORMAT_U16, indices, indexCount);
}

void GxmRenderer::drawTextured2D(const BasicVertex* vertices, uint32_t vertexCount,
                                 const uint16_t* indices, uint32_t indexCount,
                                 const GxmTexture* texture) {
    endStaticBatch();m_drawState.valid=false;
    if (!texture || !texture->isValid() || !m_textureEnabled) {
        draw2D(vertices, vertexCount, indices, indexCount);
        return;
    }
    if (!copyFrameGeometry(vertices, vertexCount, indices, indexCount)) return;

    sceGxmSetCullMode(m_context, SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetBackDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_DISABLED);

    Mat4 ortho = Mat4::orthographic(0.0f, 960.0f, 544.0f, 0.0f, -1.0f, 1.0f);

    sceGxmSetVertexProgram(m_context, m_textureVertexProgram);
    sceGxmSetFragmentProgram(m_context, m_uiTextureFragmentProgram ? m_uiTextureFragmentProgram : m_textureFragmentProgram);

    void* uniformBuffer = nullptr;
    if (sceGxmReserveVertexDefaultUniformBuffer(m_context, &uniformBuffer) < 0 || !uniformBuffer) {
        logMsg("GXM: vertex uniform allocation failed");
        return;
    }
    sceGxmSetUniformDataF(uniformBuffer, m_paramTextureWVP, 0, 16, &ortho.m[0][0]);

    sceGxmSetFragmentTexture(m_context, 0, &texture->texture);

    sceGxmSetVertexStream(m_context, 0, vertices);
    sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES,
               SCE_GXM_INDEX_FORMAT_U16, indices, indexCount);
}

// ============================================================================
// Multi-Object Mesh and Texture Management
// ============================================================================

bool GxmRenderer::createGxmMesh(const NifMesh& mesh, GxmMesh& outMesh) {
    destroyGxmMesh(outMesh);

    if (mesh.vertexCount == 0 || mesh.vertexCount > 65536 || mesh.indices.empty() ||
        mesh.vertexCount != mesh.vertices.size() || mesh.indices.size() % 3 != 0) {
        return false;
    }
    for (uint16_t index : mesh.indices) {
        if (index >= mesh.vertexCount) {
            logMsg("createGxmMesh: index outside vertex buffer");
            return false;
        }
    }

    // Allocate GPU vertex buffer
    outMesh.vertices = static_cast<BasicVertex*>(gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        mesh.vertexCount * sizeof(BasicVertex), 4,
        SCE_GXM_MEMORY_ATTRIB_READ,
        &outMesh.verticesUid));
    if (!outMesh.vertices) {
        logMsg("createGxmMesh: failed to allocate vertex buffer (%u verts)", mesh.vertexCount);
        return false;
    }

    // Allocate GPU index buffer
    outMesh.indexCount = static_cast<uint32_t>(mesh.indices.size() + mesh.parts.size());
    outMesh.indices = static_cast<uint16_t*>(gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        outMesh.indexCount * sizeof(uint16_t), 4,
        SCE_GXM_MEMORY_ATTRIB_READ,
        &outMesh.indicesUid));
    if (!outMesh.indices) {
        logMsg("createGxmMesh: failed to allocate index buffer (%u indices)", outMesh.indexCount);
        gpuFree(outMesh.verticesUid);
        outMesh.vertices = nullptr;
        outMesh.verticesUid = 0;
        return false;
    }

    outMesh.vertexCount = mesh.vertexCount;

    // Copy vertex data: interleave position, normal, uv into BasicVertex
    for (uint32_t i = 0; i < mesh.vertexCount; ++i) {
        BasicVertex& v = outMesh.vertices[i];

        // Position
        if (i < mesh.vertices.size()) {
            v.position[0] = mesh.vertices[i].x;
            v.position[1] = mesh.vertices[i].y;
            v.position[2] = mesh.vertices[i].z;
        } else {
            v.position[0] = v.position[1] = v.position[2] = 0.0f;
        }

        // Normal
        if (i < mesh.normals.size()) {
            v.normal[0] = mesh.normals[i].x;
            v.normal[1] = mesh.normals[i].y;
            v.normal[2] = mesh.normals[i].z;
        } else {
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
        }

        // Directional light in Gamebryo Z-up space
        float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
        float nLen = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nLen > 0.0001f) { nx /= nLen; ny /= nLen; nz /= nLen; }
        float lDotN = nx * 0.3f - ny * 0.4f + nz * 0.8f;
        if (lDotN < 0.0f) lDotN = 0.0f;
        float intensity = 0.35f + 0.65f * lDotN;
        v.color[0] = 0.90f * intensity;
        v.color[1] = 0.85f * intensity;
        v.color[2] = 0.75f * intensity;
        v.color[3] = 1.0f;
        if(i<mesh.colors.size()) {
            const auto& color=mesh.colors[i];
            v.color[0]*=color.r;v.color[1]*=color.g;v.color[2]*=color.b;v.color[3]=color.a;
        }

        // UV
        if (i < mesh.uvs.size()) {
            v.uv[0] = mesh.uvs[i].u;
            v.uv[1] = mesh.uvs[i].v;
        } else {
            v.uv[0] = v.uv[1] = 0.0f;
        }
    }

    outMesh.boundsMin=outMesh.boundsMax=mesh.vertices[0];outMesh.hasBounds=true;
    for(const auto& v:mesh.vertices) {
        outMesh.boundsMin.x=std::min(outMesh.boundsMin.x,v.x);outMesh.boundsMax.x=std::max(outMesh.boundsMax.x,v.x);
        outMesh.boundsMin.y=std::min(outMesh.boundsMin.y,v.y);outMesh.boundsMax.y=std::max(outMesh.boundsMax.y,v.y);
        outMesh.boundsMin.z=std::min(outMesh.boundsMin.z,v.z);outMesh.boundsMax.z=std::max(outMesh.boundsMax.z,v.z);
    }
    auto addClusters=[&](uint32_t sourceFirst,uint32_t count) {
        if(count<384)return;
        for(uint32_t offset=0;offset<count;offset+=192) {
            GxmClipCluster c;c.firstIndex=offset;c.indexCount=std::min(192u,count-offset);
            c.minimum=c.maximum=mesh.vertices[mesh.indices[sourceFirst+offset]];
            for(uint32_t k=0;k<c.indexCount;++k) {
                const auto& v=mesh.vertices[mesh.indices[sourceFirst+offset+k]];
                c.minimum.x=std::min(c.minimum.x,v.x);c.maximum.x=std::max(c.maximum.x,v.x);
                c.minimum.y=std::min(c.minimum.y,v.y);c.maximum.y=std::max(c.maximum.y,v.y);
                c.minimum.z=std::min(c.minimum.z,v.z);c.maximum.z=std::max(c.maximum.z,v.z);
            }
            outMesh.clipClusters.push_back(c);
        }
    };
    // Copy index data
    if (mesh.parts.empty())
        memcpy(outMesh.indices, mesh.indices.data(), mesh.indices.size() * sizeof(uint16_t));
    uint32_t nextIndex = 0;
    for (const auto& sourcePart : mesh.parts) {
        if (sourcePart.firstIndex > mesh.indices.size() ||
            sourcePart.indexCount > mesh.indices.size() - sourcePart.firstIndex) continue;
        // GXM index streams start on a four-byte boundary, including submeshes.
        if (nextIndex & 1) outMesh.indices[nextIndex++] = 0;
        GxmMeshPart part;
        part.transparent = sourcePart.transparent;
        part.firstIndex = nextIndex;
        part.indexCount = sourcePart.indexCount;
        memcpy(outMesh.indices + nextIndex, mesh.indices.data() + sourcePart.firstIndex,
               sourcePart.indexCount * sizeof(uint16_t));
        nextIndex += sourcePart.indexCount;
        if(sourcePart.indexCount) {
            part.boundsMin=part.boundsMax=mesh.vertices[mesh.indices[sourcePart.firstIndex]];
            for(uint32_t k=0;k<sourcePart.indexCount;++k) {
                const auto& v=mesh.vertices[mesh.indices[sourcePart.firstIndex+k]];
                part.boundsMin.x=std::min(part.boundsMin.x,v.x);part.boundsMax.x=std::max(part.boundsMax.x,v.x);
                part.boundsMin.y=std::min(part.boundsMin.y,v.y);part.boundsMax.y=std::max(part.boundsMax.y,v.y);
                part.boundsMin.z=std::min(part.boundsMin.z,v.z);part.boundsMax.z=std::max(part.boundsMax.z,v.z);
            }
        }
        part.texturePath = sourcePart.diffuseTexture;
        std::transform(part.texturePath.begin(), part.texturePath.end(), part.texturePath.begin(),
                       [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
        if(part.texturePath.rfind("data\\",0)==0)part.texturePath.erase(0,5);
        if (!part.texturePath.empty() && part.texturePath.rfind("textures\\", 0) != 0)
            part.texturePath = "textures\\" + part.texturePath;
        part.texture = acquireMaterialTexture(part.texturePath);
        part.firstCluster=outMesh.clipClusters.size();addClusters(sourcePart.firstIndex,sourcePart.indexCount);
        part.clusterCount=outMesh.clipClusters.size()-part.firstCluster;
        outMesh.parts.push_back(std::move(part));
    }
    if (!mesh.parts.empty()) outMesh.indexCount = nextIndex;
    else addClusters(0,mesh.indices.size());
    auto cpu=std::make_shared<GxmCpuGeometry>();cpu->positions=mesh.vertices;
    cpu->indices.assign(outMesh.indices,outMesh.indices+outMesh.indexCount);outMesh.cpuGeometry=std::move(cpu);
    return true;
}

const GxmTexture* GxmRenderer::acquireMaterialTexture(const std::string& path) {
    if (path.empty() || !m_primaryTextures) return nullptr;
    auto found = m_materialTextures.find(path);
    if (found != m_materialTextures.end()) {
        ++found->second.users;
        return &found->second.texture;
    }
    size_t slash = path.find_last_of('\\');
    std::string folder = path.substr(0, slash);
    std::string name = path.substr(slash + 1);
    std::vector<uint8_t> data;
    bool loaded = m_primaryTextures->extractFile(folder.c_str(), name.c_str(), data);
    if (!loaded && m_secondaryTextures)
        loaded = m_secondaryTextures->extractFile(folder.c_str(), name.c_str(), data);
    if (!loaded) return nullptr;
    DDSLoader loader;
    if (!loader.load(data.data(), data.size(),m_materialTextureLimit)) return nullptr;
    CachedTexture cached;
    if (!createGxmTexture(loader.getTexture(), cached.texture)) return nullptr;
    sceGxmTextureSetUAddrMode(&cached.texture.texture, SCE_GXM_TEXTURE_ADDR_REPEAT);
    sceGxmTextureSetVAddrMode(&cached.texture.texture, SCE_GXM_TEXTURE_ADDR_REPEAT);
    cached.users = 1;
    auto inserted = m_materialTextures.emplace(path, std::move(cached));
    return &inserted.first->second.texture;
}

void GxmRenderer::releaseMaterialTextures(std::vector<GxmMeshPart>& parts) {
    for (const auto& part : parts) {
        if (!part.texture) continue;
        auto found = m_materialTextures.find(part.texturePath);
        if (found != m_materialTextures.end() && --found->second.users == 0) {
            destroyGxmTexture(found->second.texture);
            m_materialTextures.erase(found);
        }
    }
    parts.clear();
}

void GxmRenderer::destroyGxmMesh(GxmMesh& mesh) {
    endStaticBatch();
    releaseMaterialTextures(mesh.parts);
    if (mesh.verticesUid > 0) {
        gpuFree(mesh.verticesUid);
        mesh.vertices = nullptr;
        mesh.verticesUid = 0;
    }
    if (mesh.indicesUid > 0) {
        gpuFree(mesh.indicesUid);
        mesh.indices = nullptr;
        mesh.indicesUid = 0;
    }
    mesh.vertexCount = 0;
    mesh.indexCount = 0;
    mesh.clipClusters.clear();mesh.clusterView=nullptr;mesh.clusterViewCount=0;mesh.clustersValid=true;
    mesh.dynamic = false;mesh.cpuGeometry.reset();mesh.cpuIndexOffset=0;
    mesh.cpuDynamicVertices=nullptr;mesh.cpuDynamicIndices=nullptr;
}

bool GxmRenderer::createGxmTexture(const TextureData& texture, GxmTexture& outTex) {
    destroyGxmTexture(outTex);

    if (texture.width == 0 || texture.height == 0 || texture.width > 4096 ||
        texture.height > 4096 ||
        texture.pixels.size() != static_cast<size_t>(texture.width) * texture.height * 4) {
        return false;
    }

    uint32_t texSize = texture.width * texture.height * 4;
    outTex.data = gpuAlloc(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
        texSize, 16,
        SCE_GXM_MEMORY_ATTRIB_READ,
        &outTex.dataUid);
    if (!outTex.data) {
        logMsg("createGxmTexture: failed to allocate GPU memory for texture (%u bytes)", texSize);
        return false;
    }

    memcpy(outTex.data, texture.pixels.data(), texSize);

    int err = sceGxmTextureInitLinear(
        &outTex.texture,
        outTex.data,
        SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,
        texture.width,
        texture.height,
        0);
    if (err < 0) {
        logMsg("createGxmTexture: sceGxmTextureInitLinear failed: 0x%08X", err);
        gpuFree(outTex.dataUid);
        outTex.data = nullptr;
        outTex.dataUid = 0;
        return false;
    }

    sceGxmTextureSetMinFilter(&outTex.texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&outTex.texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMipFilter(&outTex.texture, SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
    sceGxmTextureSetUAddrMode(&outTex.texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&outTex.texture, SCE_GXM_TEXTURE_ADDR_CLAMP);

    outTex.width = texture.width;
    outTex.height = texture.height;
    return true;
}

void GxmRenderer::destroyGxmTexture(GxmTexture& tex) {
    endStaticBatch();m_drawState.valid=false;
    if (tex.dataUid > 0) {
        gpuFree(tex.dataUid);
        tex.data = nullptr;
        tex.dataUid = 0;
    }
    tex.width = 0;
    tex.height = 0;
}

// ============================================================================
// Single-Mesh upload (Phase 1 compatibility)
// ============================================================================

bool GxmRenderer::uploadMesh(const NifMesh& mesh) {
    GxmMesh tempMesh;
    if (!createGxmMesh(mesh, tempMesh)) return false;

    // Free previously uploaded mesh
    if (m_hasMesh) {
        releaseMaterialTextures(m_meshParts);
        gpuFree(m_meshVerticesUid);
        gpuFree(m_meshIndicesUid);
        m_meshVertices = nullptr;
        m_meshIndices = nullptr;
        m_hasMesh = false;
    }

    m_meshVertices = tempMesh.vertices;
    m_meshVerticesUid = tempMesh.verticesUid;
    m_meshIndices = tempMesh.indices;
    m_meshIndicesUid = tempMesh.indicesUid;
    m_meshIndexCount = tempMesh.indexCount;
    m_meshVertexCount = tempMesh.vertexCount;
    m_meshParts = std::move(tempMesh.parts);
    m_hasMesh = true;
    return true;
}

void GxmRenderer::freeTexture() {
    if (m_hasTexture) {
        gpuFree(m_textureDataUid);
        m_textureData = nullptr;
        m_textureDataUid = 0;
        m_hasTexture = false;
    }
}

bool GxmRenderer::uploadTexture(const TextureData& texture) {
    GxmTexture tempTex;
    if (!createGxmTexture(texture, tempTex)) return false;

    freeTexture();
    m_gxmTexture = tempTex.texture;
    m_textureData = tempTex.data;
    m_textureDataUid = tempTex.dataUid;
    m_hasTexture = true;
    return true;
}

// ============================================================================
// Shutdown & destructor
// ============================================================================

void GxmRenderer::shutdown() {
    endStaticBatch();m_workers.shutdown();
    if (!m_initialized) return;

    logMsg("GxmRenderer: shutting down...");

    // Wait for GPU to finish all pending work
    sceGxmFinish(m_context);
    sceGxmDisplayQueueFinish();

    gpuFree(m_frameVerticesUid);
    gpuFree(m_frameIndicesUid);
    m_frameVertices = nullptr;
    m_frameIndices = nullptr;
    m_frameVerticesUid = 0;
    m_frameIndicesUid = 0;

    // Free texture
    freeTexture();

    // Free mesh data
    releaseMaterialTextures(m_meshParts);
    if (m_hasMesh) {
        gpuFree(m_meshVerticesUid);
        gpuFree(m_meshIndicesUid);
        m_meshVertices = nullptr;
        m_meshIndices  = nullptr;
        m_hasMesh = false;
    }

    // Free clear geometry
    gpuFree(m_clearVerticesUid);
    gpuFree(m_clearIndicesUid);

    // Destroy patched programs
    if (m_uiTextureFragmentProgram)
        sceGxmShaderPatcherReleaseFragmentProgram(m_shaderPatcher, m_uiTextureFragmentProgram);
    if (m_textureFragmentProgram)
        sceGxmShaderPatcherReleaseFragmentProgram(m_shaderPatcher, m_textureFragmentProgram);
    if (m_textureVertexProgram)
        sceGxmShaderPatcherReleaseVertexProgram(m_shaderPatcher, m_textureVertexProgram);
    if (m_basicFragmentProgram)
        sceGxmShaderPatcherReleaseFragmentProgram(m_shaderPatcher, m_basicFragmentProgram);
    if (m_basicVertexProgram)
        sceGxmShaderPatcherReleaseVertexProgram(m_shaderPatcher, m_basicVertexProgram);
    if (m_clearFragmentProgram)
        sceGxmShaderPatcherReleaseFragmentProgram(m_shaderPatcher, m_clearFragmentProgram);
    if (m_clearVertexProgram)
        sceGxmShaderPatcherReleaseVertexProgram(m_shaderPatcher, m_clearVertexProgram);

    // Unregister shader programs
    sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_textureFragmentProgramId);
    sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_textureVertexProgramId);
    sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_basicFragmentProgramId);
    sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_basicVertexProgramId);
    sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_clearFragmentProgramId);
    sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_clearVertexProgramId);

    // Destroy shader patcher
    sceGxmShaderPatcherDestroy(m_shaderPatcher);

    // Free patcher memory
    gpuFree(m_patcherBufferUid);
    vertexUsseFree(m_patcherVertexUsseUid);
    fragmentUsseFree(m_patcherFragmentUsseUid);

    // Destroy render target
    sceGxmDestroyRenderTarget(m_renderTarget);

    // Free depth buffer
    gpuFree(m_depthBufferUid);

    // Free display buffers and sync objects
    for (int i = 0; i < DISPLAY_BUFFER_COUNT; ++i) {
        gpuFree(m_displayBuffers[i].uid);
        sceGxmSyncObjectDestroy(m_displayBuffers[i].sync);
    }

    // Destroy context
    sceGxmDestroyContext(m_context);

    // Free ring buffers
    gpuFree(m_vdmRingBufferUid);
    gpuFree(m_vertexRingBufferUid);
    gpuFree(m_fragmentRingBufferUid);
    fragmentUsseFree(m_fragmentUsseRingBufferUid);

    // Terminate GXM
    sceGxmTerminate();

    m_initialized = false;
    logMsg("GxmRenderer: shutdown complete");
}

GxmRenderer::~GxmRenderer() {
    shutdown();
}
