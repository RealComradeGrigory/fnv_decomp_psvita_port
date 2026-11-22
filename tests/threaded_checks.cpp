#define main ignoredUiMain
#include "ui_gpu_checks.cpp"
#undef main
#include <chrono>

static uint64_t submissionHash() {
    uint64_t hash=1469598103934665603ull;
    auto bytes=[&](const void* pointer,size_t count) {
        auto p=static_cast<const unsigned char*>(pointer);
        while(count--){hash^=*p++;hash*=1099511628211ull;}
    };
    for(const auto& draw:pending) {
        unsigned count=draw.indexCopy.size();bytes(&count,sizeof(count));
        bytes(draw.indexCopy.data(),count*sizeof(uint16_t));
        bytes(draw.vertexCopy.data(),draw.vertexCopy.size()*sizeof(BasicVertex));
    }
    return hash;
}
int main() {
    for(unsigned workerCount=0;workerCount<=2;++workerCount) {
        CpuWorkers pool;require(pool.start(workerCount)==workerCount,"requested worker startup including serial fallback");
        struct Context {std::vector<unsigned> hits;std::atomic<unsigned> arrived{0};unsigned parties=0;};
        Context context;context.hits.resize(257);context.parties=workerCount+1;
        // Force all enabled execution lanes to overlap on the first barrier.
        pool.run(context.parties,[](void* pointer,unsigned index,unsigned lane) {
            auto& c=*static_cast<Context*>(pointer);c.hits[index]=lane+1;
            ++c.arrived;while(c.arrived.load()<c.parties)std::this_thread::yield();
        },&context);
        for(unsigned lane=0;lane<=workerCount;++lane)require(pool.jobs(lane)>0,"each enabled CPU lane actually executes work concurrently");
        for(unsigned round=0;round<2000;++round) {
            std::fill(context.hits.begin(),context.hits.end(),0);
            pool.run(context.hits.size(),[](void* pointer,unsigned index,unsigned) {
                auto& c=*static_cast<Context*>(pointer);++c.hits[index];
            },&context,round%3!=0);
            for(unsigned hit:context.hits)require(hit==1,"every parallel/serial task runs exactly once before barrier returns");
        }
        pool.shutdown();require(pool.start(workerCount)==workerCount,"workers restart after graceful shutdown");pool.shutdown();
    }
    GxmRenderer r;r.m_context=reinterpret_cast<SceGxmContext*>(1);
    NifMesh source;
    // An eye-crossing tile with both transparent and opaque material ranges.
    source.vertices={{-100,-5,-50},{100,100,-50},{0,100,100},{-10,200,0},{10,200,0},{0,200,30}};
    source.vertexCount=source.vertices.size();source.indices={0,1,2,3,4,5};source.triangleCount=2;
    NifMeshPart a,b;a.firstIndex=0;a.indexCount=3;b.firstIndex=3;b.indexCount=3;b.transparent=true;source.parts={a,b};
    GxmMesh mesh;require(r.createGxmMesh(source,mesh),"multipart clipping fixture uploads");
    auto render=[&](int mode) {
        r.setParallelPreparation(mode==2);r.beginFrame();r.setCameraFreecam(0,0,0,0,0,65,2000);
        if(mode)r.beginStaticBatch();
        for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<80;++i)
            r.drawMeshInstance(mesh,nullptr,Mat4::translate((int(i%8)-4)*20.f,(int(i/8)-2)*20.f,0),pass);
        r.endStaticBatch();
        auto hash=submissionHash();r.endFrame();r.waitForGpu();return hash;
    };
    uint64_t immediate=render(0),serial=render(1),parallel=render(2);
    require(immediate==serial && serial==parallel,"immediate/serial batch/parallel batches submit byte-identical ordered geometry");
    for(unsigned round=0;round<100;++round)require(render(round%2?1:2)==immediate,"repeated mode switches keep GPU geometry immutable and deterministic");
    // A draw kept below the auto-flush limit must be drained before UI/camera
    // changes, resource destruction, and endFrame.
    for(unsigned boundary=0;boundary<4;++boundary) {
        r.beginFrame();r.setCameraFreecam(0,0,0,0,0);r.beginStaticBatch();r.drawMeshInstance(mesh,nullptr,Mat4::identity());
        require(r.m_jobCount>0,"lifecycle fixture contains deferred work");
        if(boundary==0)r.setCameraFreecam(0,20,0,0,0);
        if(boundary==1){BasicVertex v[3]{};alignas(4) uint16_t ix[]={0,1,2};r.draw2D(v,3,ix,3);}
        if(boundary==2)r.endFrame();
        if(boundary==3)r.destroyGxmMesh(mesh);
        require(r.m_jobCount==0,"camera/UI/frame/free boundary drains pending preparation");
        r.endFrame();r.waitForGpu();
    }
    r.m_workers.shutdown();r.gpuFree(r.m_frameVerticesUid);r.gpuFree(r.m_frameIndicesUid);r.m_context=nullptr;
    require(blocks.empty() && overflows==0,"threaded rendering releases all GPU resources without arena overflows");
    puts("PASS: 0/1/2-worker concurrent execution; 6000 task barriers; restart/shutdown; byte-identical immediate/serial/parallel clipping; 100 mode switches; UI/camera/frame/free barriers");
}
