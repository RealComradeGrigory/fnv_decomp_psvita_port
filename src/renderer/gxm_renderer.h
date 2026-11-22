/**
 * GXM Renderer - Header
 *
 * Minimal PS Vita graphics renderer using Sony's GXM API.
 * Handles initialization, basic 3D rendering of meshes loaded
 * from NIF files, and camera control.
 *
 * GXM = GPU eXtended Memory architecture
 * Vita display: 960 x 544 pixels
 */

#pragma once

#include <psp2/gxm.h>
#include <psp2/types.h>
#include <psp2/kernel/sysmem.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "cpu_workers.h"

#include "../nif/nif_loader.h"
#include "../dds/dds_loader.h"

// Vita display constants
constexpr int DISPLAY_WIDTH      = 960;
constexpr int DISPLAY_HEIGHT     = 544;
constexpr int DISPLAY_STRIDE     = 960;   // 960 native Vita display pitch
constexpr int DISPLAY_BUFFER_COUNT = 3;   // triple buffering
constexpr int MAX_PENDING_SWAPS  = 2;

// Simple 4x4 matrix (column-major, OpenGL-style)
struct Mat4 {
    float m[4][4];

    static Mat4 identity();
    static Mat4 perspective(float fovY, float aspect, float near, float far);
    static Mat4 lookAt(float eyeX, float eyeY, float eyeZ,
                       float atX, float atY, float atZ,
                       float upX, float upY, float upZ);
    static Mat4 rotateX(float radians);
    static Mat4 rotateY(float radians);
    static Mat4 rotateZ(float radians);
    static Mat4 scale(float s);
    static Mat4 translate(float x, float y, float z);
    static Mat4 createTransform(float x, float y, float z,
                                float rx, float ry, float rz,
                                float scale);
    static Mat4 orthographic(float left, float right, float bottom, float top, float near, float far);
    Mat4 operator*(const Mat4& other) const;
};

// Vertex format for our basic shader
struct BasicVertex {
    float position[3];
    float normal[3];
    float color[4];
    float uv[2];
};

class BSAReader;
struct GxmTexture;

struct GxmMeshPart {
    bool transparent = false;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    std::string texturePath;
    const GxmTexture* texture = nullptr;
    Vec3 boundsMin{}, boundsMax{};
    uint32_t firstCluster=0,clusterCount=0;
};

struct GxmClipCluster {
    Vec3 minimum{},maximum{};
    uint32_t firstIndex=0,indexCount=0;
};

// CPU classification uses cached positions/indices, without duplicating GPU attributes.
struct GxmCpuGeometry {std::vector<Vec3> positions;std::vector<uint16_t> indices;};

// Standalone GPU mesh resource
struct GxmMesh {
    BasicVertex* vertices = nullptr;
    SceUID       verticesUid = 0;
    uint16_t*    indices = nullptr;
    SceUID       indicesUid = 0;
    uint32_t     vertexCount = 0;
    uint32_t     indexCount = 0;
    bool         dynamic = false;
    bool         transparent = false;
    bool         hasBounds = false;
    Vec3         boundsMin{}, boundsMax{};
    std::vector<GxmMeshPart> parts;
    std::vector<GxmClipCluster> clipClusters;
    const GxmClipCluster* clusterView=nullptr;
    uint32_t clusterViewCount=0;
    bool clustersValid=true;
    std::shared_ptr<const GxmCpuGeometry> cpuGeometry;
    uint32_t cpuIndexOffset=0;
    const BasicVertex* cpuDynamicVertices=nullptr;
    const uint16_t* cpuDynamicIndices=nullptr;
    bool isValid() const { return vertices != nullptr && indices != nullptr && indexCount > 0; }
};

// Standalone GPU texture resource
struct GxmTexture {
    SceGxmTexture texture;
    void*         data = nullptr;
    SceUID        dataUid = 0;
    uint32_t      width = 0;
    uint32_t      height = 0;
    bool isValid() const { return data != nullptr; }
};

/**
 * GXM renderer.
 * Initializes the Vita GPU and provides simple mesh rendering.
 */
class GxmRenderer {
public:
    struct FrameStats {
        uint32_t boundsTests=0, culled=0, draws=0, triangles=0, splitTriangles=0, scannedVertices=0;
        uint32_t prepareUs=0, submitUs=0, workerJobs[3]{};
        uint32_t clusterInside=0,clusterOutside=0,triangleTests=0;
        uint32_t stateCalls=0,uniformWrites=0,wvpBuilds=0,presentUs=0;
    };
    const FrameStats& frameStats() const { return m_frameStats; }
    GxmRenderer() = default;
    ~GxmRenderer();

    // Initialize GXM and allocate display buffers
    bool init();

    // Shutdown and free all GPU resources
    void shutdown();

    // Frame lifecycle
    void beginFrame();
    void endFrame();
    void waitForGpu();
    void setTextureArchives(BSAReader* primary, BSAReader* secondary) {
        m_primaryTextures = primary;
        m_secondaryTextures = secondary;
    }
    void setMaterialTextureLimit(uint32_t dimension) {m_materialTextureLimit=dimension;}

    // Clear the screen
    void clear(float r, float g, float b, float a);
    void distanceFog(float start,float end,const float* color);

    // Upload a NIF mesh to GPU memory
    bool uploadMesh(const NifMesh& mesh);

    // Upload a DDS texture to GPU memory
    bool uploadTexture(const TextureData& texture);
    void freeTexture();
    bool hasTexture() const { return m_hasTexture; }
    void setTextureEnabled(bool enabled) { m_textureEnabled = enabled; }
    bool isTextureEnabled() const { return m_textureEnabled; }

    // Standalone multi-object resource management
    bool createGxmMesh(const NifMesh& mesh, GxmMesh& outMesh);
    void destroyGxmMesh(GxmMesh& mesh);

    bool createGxmTexture(const TextureData& texture, GxmTexture& outTex);
    void destroyGxmTexture(GxmTexture& tex);

    // Camera modes
    void setCamera(float distance, float pitchDeg, float yawDeg,
                   float targetX = 0.0f, float targetY = 0.0f, float targetZ = 0.0f);
    void setCameraFreecam(float eyeX, float eyeY, float eyeZ, float pitchRad, float yawRad, float fovDeg = 65.0f, float farPlane = 15000.0f);
    void setViewProjection(const Mat4& view, const Mat4& proj);

    // Draw single mesh (Phase 1 showcase)
    void drawMesh();

    // Draw mesh instance with arbitrary world matrix and optional texture (Phase 2 cell)
    void drawMeshInstance(const GxmMesh& mesh, const GxmTexture* texture, const Mat4& worldMatrix, int pass = -1);
    // Static geometry may be prepared concurrently; the end barrier preserves
    // material submission order and completes before any camera/resource change.
    void beginStaticBatch();
    void endStaticBatch();
    void setParallelPreparation(bool enabled) { m_parallelPreparation=enabled; }
    unsigned preparationWorkers() const { return m_workers.workers(); }
    CpuWorkers* cpuPreparationPool() {endStaticBatch();if(!m_parallelPreparation)return nullptr;m_workers.start();return &m_workers;}
    size_t preparationScratchBytes() const;
    void setSubmissionOptimization(bool enabled) {endStaticBatch();m_optimizeSubmission=enabled;m_drawState.valid=false;}

    // Dynamic 2D rendering for HUD / Pip-Boy 3000 (Phase 3)
    bool allocateDynamicMesh(uint32_t maxVertices, uint32_t maxIndices, GxmMesh& outMesh);
    void draw2D(const BasicVertex* vertices, uint32_t vertexCount,
                const uint16_t* indices, uint32_t indexCount);
    void drawTextured2D(const BasicVertex* vertices, uint32_t vertexCount,
                        const uint16_t* indices, uint32_t indexCount,
                        const GxmTexture* texture);

    // Pip-Boy Flashlight illumination
    void setFlashlight(bool enabled) { m_flashlight = enabled; }
    bool isFlashlight() const { return m_flashlight; }

private:
    bool m_optimizeSubmission=true;
    struct DrawState {
        bool valid=false,textured=false,transparent=false;
        const SceGxmVertexProgram* vertex=nullptr;
        const SceGxmFragmentProgram* fragment=nullptr;
        const GxmTexture* texture=nullptr;
        Mat4 wvp{};
    } m_drawState;
    void drawMeshWithWvp(const GxmMesh& mesh,const GxmTexture* texture,const Mat4& wvp,int pass,bool perspective);
    bool bindDrawState(const Mat4& wvp,const GxmTexture* texture,bool transparent);
    uint32_t m_materialTextureLimit=512;
    FrameStats m_frameStats;
    // CPU scratch only; queued GPU submissions use the immutable frame arena.
    CpuWorkers m_workers;
    bool m_parallelPreparation=true,m_staticBatch=false,m_workersLogged=false;
    struct ClipScratch { std::vector<uint8_t> codes;std::vector<BasicVertex> cachedVertices;std::vector<uint16_t> cachedIndices; } m_clipScratch[3];
    struct DrawJob {
        const BasicVertex* vertices=nullptr;const uint16_t* indices=nullptr;
        const BasicVertex* cpuVertices=nullptr;const Vec3* cpuPositions=nullptr;const uint16_t* cpuIndices=nullptr;
        uint32_t vertexCount=0,indexCount=0;Mat4 wvp{};
        const GxmTexture* texture=nullptr;bool transparent=false;int clipMask=0;
        const GxmClipCluster* clusters=nullptr;uint32_t clusterCount=0;
    };
    struct ClipResult {
        std::vector<uint16_t> front;std::vector<BasicVertex> split;
        uint32_t scanned=0;bool changed=false,valid=true;
        uint32_t clusterInside=0,clusterOutside=0,triangleTests=0;
    };
    static constexpr unsigned PREPARE_BATCH_SIZE=24,PREPARE_INDEX_BUDGET=16384;
    DrawJob m_drawJobs[PREPARE_BATCH_SIZE];ClipResult m_clipResults[PREPARE_BATCH_SIZE];
    unsigned m_jobCount=0,m_jobIndices=0;
    std::vector<uint16_t> m_sequentialIndices;
    static void prepareJob(void* context,unsigned index,unsigned lane);
    void prepareDraw(const DrawJob& job,ClipResult& result,ClipScratch& scratch);
    void submitDraw(const DrawJob& job,const ClipResult& result);
    void flushStaticBatch();
    // Submitted geometry stays immutable until the scene has finished on the GPU.
    bool copyFrameGeometry(const BasicVertex*& vertices, uint32_t vertexCount,
                           const uint16_t*& indices, uint32_t indexCount, bool triangleList = true);
    // Outdoor views clip many independent static meshes. Indices remain local
    // to each draw; the frame arena itself may contain more than 65536 vertices.
    static constexpr uint32_t FRAME_VERTEX_CAPACITY = 131072;
    static constexpr uint32_t FRAME_INDEX_CAPACITY = 1048576;
    BasicVertex* m_frameVertices = nullptr;
    uint16_t* m_frameIndices = nullptr;
    SceUID m_frameVerticesUid = 0;
    SceUID m_frameIndicesUid = 0;
    uint32_t m_frameVertexCount = 0;
    uint32_t m_frameIndexCount = 0;
    bool m_gpuWorkPending = false;
    bool m_sceneActive = false;
    struct CachedTexture {
        GxmTexture texture;
        uint32_t users = 0;
    };
    std::unordered_map<std::string, CachedTexture> m_materialTextures;
    BSAReader* m_primaryTextures = nullptr;
    BSAReader* m_secondaryTextures = nullptr;
    const GxmTexture* acquireMaterialTexture(const std::string& path);
    void releaseMaterialTextures(std::vector<GxmMeshPart>& parts);

    // GXM memory allocation helpers
    void* gpuAlloc(SceKernelMemBlockType type, uint32_t size,
                   uint32_t alignment, SceGxmMemoryAttribFlags attribs,
                   SceUID* uid);
    void  gpuFree(SceUID uid);

    // Vertex USSE allocation (for vertex programs)
    void* vertexUsseAlloc(uint32_t size, SceUID* uid, uint32_t* usseOffset);
    void  vertexUsseFree(SceUID uid);

    // Fragment USSE allocation (for fragment programs)
    void* fragmentUsseAlloc(uint32_t size, SceUID* uid, uint32_t* usseOffset);
    void  fragmentUsseFree(SceUID uid);

    // Create shaders
    bool createShaders();

    // Display callback
    static void displayCallback(const void* callbackData);

    // State
    bool m_initialized = false;

    // GXM context
    SceGxmContext*         m_context = nullptr;
    SceUID                 m_vdmRingBufferUid = 0;
    SceUID                 m_vertexRingBufferUid = 0;
    SceUID                 m_fragmentRingBufferUid = 0;
    SceUID                 m_fragmentUsseRingBufferUid = 0;

    // Render target
    SceGxmRenderTarget*    m_renderTarget = nullptr;

    // Display buffers
    struct DisplayBuffer {
        void*              data = nullptr;
        SceUID             uid = 0;
        SceGxmColorSurface surface;
        SceGxmSyncObject*  sync = nullptr;
    };
    DisplayBuffer          m_displayBuffers[DISPLAY_BUFFER_COUNT];
    int                    m_frontBufferIndex = 0;
    int                    m_backBufferIndex = 0;

    // Depth/stencil
    SceGxmDepthStencilSurface m_depthSurface;
    void*                  m_depthBufferData = nullptr;
    SceUID                 m_depthBufferUid = 0;

    // Shaders
    SceGxmShaderPatcher*   m_shaderPatcher = nullptr;
    SceUID                 m_patcherBufferUid = 0;
    SceUID                 m_patcherVertexUsseUid = 0;
    SceUID                 m_patcherFragmentUsseUid = 0;

    // Basic shader program
    SceGxmShaderPatcherId  m_basicVertexProgramId;
    SceGxmShaderPatcherId  m_basicFragmentProgramId;
    SceGxmVertexProgram*   m_basicVertexProgram = nullptr;
    SceGxmFragmentProgram* m_basicFragmentProgram = nullptr;
    const SceGxmProgramParameter* m_paramWVP = nullptr;  // world-view-projection matrix

    // Texture shader program
    SceGxmShaderPatcherId  m_textureVertexProgramId;
    SceGxmShaderPatcherId  m_textureFragmentProgramId;
    SceGxmVertexProgram*   m_textureVertexProgram = nullptr;
    SceGxmFragmentProgram* m_textureFragmentProgram = nullptr;
    SceGxmFragmentProgram* m_uiTextureFragmentProgram = nullptr;
    const SceGxmProgramParameter* m_paramTextureWVP = nullptr;

    // Clear shader program
    SceGxmShaderPatcherId  m_clearVertexProgramId;
    SceGxmShaderPatcherId  m_clearFragmentProgramId;
    SceGxmVertexProgram*   m_clearVertexProgram = nullptr;
    SceGxmFragmentProgram* m_clearFragmentProgram = nullptr;
    const SceGxmProgramParameter* m_paramClearColor = nullptr;

    // Clear geometry
    BasicVertex*           m_clearVertices = nullptr;
    SceUID                 m_clearVerticesUid = 0;
    uint16_t*              m_clearIndices = nullptr;
    SceUID                 m_clearIndicesUid = 0;

    // Mesh data on GPU
    BasicVertex*           m_meshVertices = nullptr;
    SceUID                 m_meshVerticesUid = 0;
    uint16_t*              m_meshIndices = nullptr;
    SceUID                 m_meshIndicesUid = 0;
    uint32_t               m_meshIndexCount = 0;
    uint32_t               m_meshVertexCount = 0;
    std::vector<GxmMeshPart> m_meshParts;
    bool                   m_hasMesh = false;

    // Texture state on GPU
    SceGxmTexture          m_gxmTexture;
    void*                  m_textureData = nullptr;
    SceUID                 m_textureDataUid = 0;
    bool                   m_hasTexture = false;
    bool                   m_textureEnabled = true;
    bool                   m_flashlight = false;

    // Camera
    Mat4                   m_viewMatrix = Mat4::identity();
    Mat4                   m_projMatrix = Mat4::identity();
    Mat4                   m_viewProjMatrix = Mat4::identity();
    Mat4                   m_wvpMatrix = Mat4::identity();
};
