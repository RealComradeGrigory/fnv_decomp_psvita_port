#define FNV_REAL_BSA 1
#define logMsg ignoredBaseLog
#define main ignoredUiMain
#include "../ui_gpu_checks.cpp"
#undef main
#undef logMsg
#include <cstdarg>
#include <psp2/io/fcntl.h>
#define private public
#include "scene/world_scene.h"
#include "physics/physics_engine.h"
#undef private
#ifdef FNV_WORLD_CAPTURE
#include "world_capture.h"
#endif
static std::unordered_map<int,FILE*> realFiles;static int fileID=100000;
void logMsg(const char* message,...) {
    if(strstr(message,"capacity"))++overflows;
    if(strstr(message,"WorldScene:")) {va_list args;va_start(args,message);vprintf(message,args);va_end(args);puts("");fflush(stdout);}
}
extern "C" {
SceUID sceIoOpen(const char* path,int,SceMode) {FILE* f=fopen(path,"rb");if(!f)return -1;int id=fileID++;realFiles[id]=f;return id;}
int sceIoClose(SceUID id) {auto found=realFiles.find(id);if(found==realFiles.end())return -1;fclose(found->second);realFiles.erase(found);return 0;}
SceSSize sceIoRead(SceUID id,void* buf,SceSize count) {return fread(buf,1,count,realFiles.at(id));}
SceOff sceIoLseek(SceUID id,SceOff offset,int whence) {if(fseek(realFiles.at(id),(long)offset,whence))return -1;return ftell(realFiles.at(id));}
}
static uint64_t workerJobsTotal[3]{};static unsigned parallelViews=0;static size_t peakScratch=0;
static uint64_t referenceTriangleTests=0,optimizedTriangleTests=0,referenceStateCalls=0,optimizedStateCalls=0,referenceUniforms=0,optimizedUniforms=0;
static uint64_t geometryDigest() {
    uint64_t hash=1469598103934665603ull;
    auto add=[&](const void* pointer,size_t size){auto p=static_cast<const unsigned char*>(pointer);while(size--){hash^=*p++;hash*=1099511628211ull;}};
    for(const auto& draw:pending) {
        uint32_t count=draw.indexCopy.size();add(&count,sizeof(count));
        add(draw.indexCopy.data(),count*sizeof(uint16_t));add(draw.vertexCopy.data(),draw.vertexCopy.size()*sizeof(BasicVertex));
    }
    return hash;
}
template<class Render> static void checkParallelScene(GxmRenderer& r,Render render) {
    uint64_t digest=0;uint32_t triangles=0,draws=0;
#ifdef FNV_WORLD_CAPTURE
    uint64_t stateDigest=0;
#endif
    for(unsigned mode=0;mode<3;++mode) {
        r.setSubmissionOptimization(mode!=0);r.setParallelPreparation(mode==2);r.beginFrame();
#ifdef FNV_WORLD_CAPTURE
        captureStateDigest=1469598103934665603ull;
#endif
        render();
#ifdef FNV_WORLD_CAPTURE
        if(mode==0)stateDigest=captureStateDigest;
        else require(captureStateDigest==stateDigest,"optimized draws retain exact WVP/sampler/depth-write state");
#endif
        const auto& stats=r.frameStats();
        if(mode==0){digest=geometryDigest();printf("REFERENCE_VIEW %u %016llx\n",parallelViews,(unsigned long long)digest);triangles=stats.triangles;draws=stats.draws;referenceTriangleTests+=stats.triangleTests;referenceStateCalls+=stats.stateCalls;referenceUniforms+=stats.uniformWrites;}
        else {
            require(geometryDigest()==digest && triangles==stats.triangles && draws==stats.draws,
                "real BSA serial and parallel render submissions are byte-identical and ordered");
            if(mode==2) {
                optimizedTriangleTests+=stats.triangleTests;optimizedStateCalls+=stats.stateCalls;optimizedUniforms+=stats.uniformWrites;
                for(unsigned lane=0;lane<3;++lane)workerJobsTotal[lane]+=stats.workerJobs[lane];
                peakScratch=std::max(peakScratch,r.preparationScratchBytes());++parallelViews;
            }
        }
        r.endFrame();r.waitForGpu();
    }
}
int main(int argc,char** argv) {
    require(argc==3 || argc==4,"world check project/game-data/optional-preview-dir args");
    BSAReader meshes,tex1,tex2,update;std::string game=argv[2];
    require(meshes.open((game+"/Fallout - Meshes.bsa").c_str()),"actual mesh BSA opens");
    require(tex1.open((game+"/Fallout - Textures.bsa").c_str()),"actual texture BSA opens");
    require(tex2.open((game+"/Fallout - Textures2.bsa").c_str()),"actual texture2 BSA opens");
    require(update.open((game+"/Update.bsa").c_str()),"original updated meshes archive opens");
    meshes.setOverrideArchive(&update);tex1.setOverrideArchive(&update);tex2.setOverrideArchive(&update);
    require(meshes.containsFile("meshes\\architecture\\wasteland","woodsteps01nv.nif"),"base BSA lookup retained");
    std::vector<uint8_t> building;
    require(meshes.extractFile("meshes\\architecture\\novac","novac_motel03.nif",building),"Update.bsa-only Novac building resolves through overlay");
    // A real room tile has a -90-degree authoring root. The placed reference
    // replaces that root; child-node transforms must still survive.
    std::vector<uint8_t> roomBytes;NifLoader standalone,placed;
    require(meshes.extractFile("meshes\\dungeons\\nv_craftsmanhomesinterior","nvcraftsmanrmendb.nif",roomBytes),"original rotated room tile extracts");
    require(standalone.load(roomBytes.data(),roomBytes.size()) && placed.load(roomBytes.data(),roomBytes.size(),true),"standalone and placed tile modes load");
    const auto& raw=standalone.getMesh();const auto& ref=placed.getMesh();require(raw.indices==ref.indices && raw.vertices.size()==ref.vertices.size(),"root replacement preserves exact topology");
    for(unsigned i=0;i<ref.vertices.size();++i)require(fabsf(ref.vertices[i].x+raw.vertices[i].y)<.003f && fabsf(ref.vertices[i].y-raw.vertices[i].x)<.003f && fabsf(ref.vertices[i].z-raw.vertices[i].z)<.003f,"REFR excludes authoring root rotation but preserves child geometry");
    GxmRenderer r;r.m_context=reinterpret_cast<SceGxmContext*>(1);r.setTextureArchives(&tex1,&tex2);
    r.m_basicVertexProgram=reinterpret_cast<SceGxmVertexProgram*>(1);r.m_textureVertexProgram=reinterpret_cast<SceGxmVertexProgram*>(2);
    r.m_basicFragmentProgram=reinterpret_cast<SceGxmFragmentProgram*>(3);r.m_textureFragmentProgram=reinterpret_cast<SceGxmFragmentProgram*>(4);r.m_uiTextureFragmentProgram=reinterpret_cast<SceGxmFragmentProgram*>(5);
#ifdef FNV_WORLD_CAPTURE
    captureRenderer=&r;
#endif
    MapDatabase maps;require(maps.open((std::string(argv[1])+"/world").c_str()),"full world catalog opens");
    GraphicsSettings graphics,loadedGraphics;
    std::string configPath=std::string(argv[1])+"/tests/graphics-host-test.cfg";
    require(graphics.valid() && graphics.viewDistance()==6144 && graphics.fog,"fresh settings default to short-distance Performance");
    graphics.applyPreset(2);require(graphics.save(configPath.c_str()) && loadedGraphics.load(configPath.c_str()) && loadedGraphics.preset==2 && loadedGraphics.viewDistance()==graphics.viewDistance(),"graphics selection persists across restarts");
    graphics.applyPreset(0);require(graphics.save(configPath.c_str()) && loadedGraphics.load(configPath.c_str()) && loadedGraphics.preset==0,"subsequent settings changes replace previous values");
    {FILE* bad=fopen(configPath.c_str(),"w");require(bad,"invalid config fixture opens");fputs("FNVGRAPHICS 1 0 999 0 0 0 0 0 0",bad);fclose(bad);}
    require(!loadedGraphics.load(configPath.c_str()) && loadedGraphics.valid() && loadedGraphics.preset==0,"malformed graphics values cannot escape safe defaults");remove(configPath.c_str());
    GraphicsMenu menu;require(menu.init(r,&tex1,&tex2),"settings use the original FNV font atlas");menu.toggle();bool quit=false;
    require(!menu.update(SCE_CTRL_DOWN,graphics,quit) && menu.isOpen(),"menu navigation leaves game paused");
    require(menu.update(SCE_CTRL_RIGHT,graphics,quit) && graphics.preset==1,"preset can be changed with the controls");
    for(unsigned row=1;row<10;++row) {
        r.beginFrame();int before=drawCount;
#ifdef FNV_WORLD_CAPTURE
        if(argc==4 && row==1){MapWorld blank{};startCapture(blank);}
#endif
        menu.render(r,graphics,5,true);require(drawCount>before+1,"settings rows and original font submit GPU geometry");
#ifdef FNV_WORLD_CAPTURE
        if(argc==4 && row==1)saveCapture(std::string(argv[3])+"/graphics009.ppm");
#endif
        r.endFrame();r.waitForGpu();
        menu.update(SCE_CTRL_DOWN,graphics,quit);
    }
    menu.update(SCE_CTRL_UP,graphics,quit);int originalCpu=graphics.parallelCpu,originalPreset=graphics.preset;
    require(menu.update(SCE_CTRL_RIGHT,graphics,quit) && graphics.parallelCpu!=originalCpu && graphics.preset==originalPreset,"CPU workers toggle is selectable without changing the graphics preset");
    require(graphics.save(configPath.c_str()) && loadedGraphics.load(configPath.c_str()) && loadedGraphics.parallelCpu==graphics.parallelCpu,"CPU workers selection persists");
    {FILE* legacy=fopen(configPath.c_str(),"w");require(legacy,"legacy config opens");fputs("FNVGRAPHICS 1 1 2 2 1 1 1 1 1",legacy);fclose(legacy);}
    require(loadedGraphics.load(configPath.c_str()) && loadedGraphics.preset==1 && loadedGraphics.parallelCpu==1,"00.07 graphics configuration migrates with workers enabled");
    {FILE* bad=fopen(configPath.c_str(),"w");require(bad,"invalid workers config opens");fputs("FNVGRAPHICS 2 1 2 2 1 1 1 1 1 99",bad);fclose(bad);}
    require(!loadedGraphics.load(configPath.c_str()),"invalid CPU worker mode is rejected");remove(configPath.c_str());
    menu.update(SCE_CTRL_UP,graphics,quit);
    require(menu.update(SCE_CTRL_RIGHT,graphics,quit) && graphics.showFps==2,"FPS + timings can be selected through the menu");
    require(graphics.save(configPath.c_str()) && loadedGraphics.load(configPath.c_str()) && loadedGraphics.showFps==2,"timing overlay selection persists in version 3 config");
    {FILE* legacy=fopen(configPath.c_str(),"w");require(legacy,"version 2 config opens");fputs("FNVGRAPHICS 2 1 2 2 1 1 1 1 1 0",legacy);fclose(legacy);}
    require(loadedGraphics.load(configPath.c_str()) && loadedGraphics.preset==1 && loadedGraphics.parallelCpu==0 && loadedGraphics.showFps==1,"00.08 config retains disabled worker choice");remove(configPath.c_str());
    r.beginFrame();menu.setTimings(2,35,3,4,5);
#ifdef FNV_WORLD_CAPTURE
    if(argc==4){MapWorld blank{};startCapture(blank);}
#endif
    menu.fps(r,18,2);require(r.m_frameVertexCount>100,"timing overlay submits all stage labels using original font");
#ifdef FNV_WORLD_CAPTURE
    if(argc==4)saveCapture(std::string(argv[3])+"/timings009.ppm");
#endif
    r.endFrame();r.waitForGpu();
    require(!quit && menu.isOpen(),"navigation alone cannot exit the application");menu.update(SCE_CTRL_CIRCLE,graphics,quit);require(!menu.isOpen() && !quit,"Circle resumes without exiting");menu.cleanup(r);
    size_t tileCount=0,refCount=0;
    for(const auto& info:maps.m_worlds) {
        require(maps.selectWorld(info.first),"every exported original world index opens");
        for(const auto& entry:maps.m_tiles) {
            MapTile tile;require(maps.readTile(entry.second.x,entry.second.y,tile),"every exported tile decodes in runtime reader");
            ++tileCount;refCount+=tile.references.size();
        }
    }
    printf("All %zu world tiles decoded; %zu placed model references\n",tileCount,refCount);fflush(stdout);
    require(maps.interiorID("GSDocMitchellHouse")==0x103DF9,"original interior editor-ID lookup");
    CellScene doc;require(doc.load(maps.interiorPath(0x103DF9).c_str(),meshes,tex1,&tex2,r),"full imported Doc cell loads");
    PhysicsEngine corridor;doc.buildCollision(corridor);float cx,cy,cz,cyaw,cpitch;doc.getSpawnPoint(cx,cy,cz,cyaw,cpitch);
    require(corridor.findStandingPosition(cx,cy,cz),"original Doc entrance has standing room");
    float startX=cx,velocity=0;bool grounded=true;
    for(unsigned tick=0;tick<60;++tick)corridor.stepPlayer(cx,cy,cz,velocity,-1,0,240,grounded,false,1.f/60);
    printf("Doc hallway walk: x %.2f -> %.2f, y %.2f, feet %.2f\n",startX,cx,cy,cz);
    require(cx<startX-160 && grounded,"original Doc hallway is traversable, not blocked by a misrotated wall");
#ifdef FNV_WORLD_CAPTURE
    uint64_t transitionDigest=0,transitionGeometry=0;
    for(unsigned mode=0;mode<2;++mode) {
        r.setSubmissionOptimization(mode!=0);r.setParallelPreparation(true);r.beginFrame();r.setCameraFreecam(cx,cy,cz+124,0,cyaw,65,15000);
        captureStateDigest=1469598103934665603ull;
        doc.render(r);
        BasicVertex v[3]{};alignas(4) uint16_t ix[]={0,1,2};
        r.draw2D(v,3,ix,3);doc.render(r);
        r.setTextureEnabled(false);doc.render(r);
        r.setTextureEnabled(true);doc.render(r);
        if(mode==0){transitionDigest=captureStateDigest;transitionGeometry=geometryDigest();}
        else require(transitionDigest==captureStateDigest && transitionGeometry==geometryDigest(),"UI/scene and textured/untextured transitions restore all captured GPU draw state");
        r.endFrame();r.waitForGpu();
    }
    puts("PASS: cached GPU WVP/sampler/depth state survives UI and texture mode transitions");
#endif
#ifdef FNV_WORLD_CAPTURE
    if(argc==4) {
        float sx,sy,sz,yaw,pitch;doc.getSpawnPoint(sx,sy,sz,yaw,pitch);
        PhysicsEngine interiorPhysics;doc.buildCollision(interiorPhysics);
        require(interiorPhysics.findStandingPosition(sx,sy,sz),"catalog interior spawn lies on original geometry");
        MapWorld interiorSky{};interiorSky.sky[0]=interiorSky.sky[1]=interiorSky.sky[2]=.08f;
        for(unsigned dir=0;dir<4;++dir) {
            r.setCameraFreecam(sx,sy,sz+124,0,yaw+dir*1.5707963f,65,15000);r.beginFrame();startCapture(interiorSky);doc.render(r);
            saveCapture(std::string(argv[3])+"/doc009-"+std::to_string(dir)+".ppm");r.endFrame();r.waitForGpu();
        }
        printf("Doc catalog spawn feet %.2f %.2f %.2f yaw %.3f\n",sx,sy,sz,yaw);
    }
#endif
    for(unsigned direction=0;direction<4;++direction) {
        r.setCameraFreecam(cx,cy,cz+124,0,cyaw+direction*1.5707963f,65,15000);
        checkParallelScene(r,[&]{doc.render(r);});
    }
    require(doc.m_doors.size()==1,"original Doc exit loaded");MapDoor exit=doc.m_doors[0];
    require(exit.targetWorld==0xDA726,"Doc door targets actual Mojave");doc.cleanup(r);
    PhysicsEngine physics;WorldScene world;
    require(world.load(maps,exit.targetWorld,exit.target[0],exit.target[1],meshes,r,physics),"original Goodsprings exterior loads");
    require(world.m_tiles.size()==9 && world.m_instances.size()>100,"nearby original cells and objects streamed");
    require(maps.model(0x1788D8) && world.m_models.count(0x1788D8),"original SCOL Doc house is imported and uploaded, not just its windows");
    require(physics.hasWorldGeometry(),"world physics contains original LAND terrain");
    float x=exit.target[0],y=exit.target[1],z=exit.target[2];
    require(physics.findStandingPosition(x,y,z),"original door destination has walkable exterior surface");
    std::printf("Exterior door standing point: %.2f %.2f %.2f; triangles=%zu; GPU buffers %.1f MiB\n",x,y,z,physics.m_triangles.size(),allocatedBytes/1048576.);fflush(stdout);
    const MapDoor* returnDoor=nullptr;
    for(const auto& tile:world.m_tiles)for(const auto& d:tile.data.doors)if(d.targetCell==0x103DF9 && d.targetWorld==0)returnDoor=&d;
    require(returnDoor && world.nearbyDoor(returnDoor->source[0],returnDoor->source[1],returnDoor->source[2]+60),"original return door is reachable at its actual position");
    uint64_t performanceTriangles=0,performanceDraws=0;
    for(unsigned dir=0;dir<4;++dir) {
        r.setCameraFreecam(x,y,z+124,0,dir*1.5707963f,65,world.m_graphics.viewDistance());r.beginFrame();
#ifdef FNV_WORLD_CAPTURE
        if(argc==4)startCapture(*maps.world(exit.targetWorld));
#endif
        world.render(r);
        performanceTriangles+=r.frameStats().triangles;performanceDraws+=r.frameStats().draws;
        if(world.m_graphics.fog)r.distanceFog(world.m_graphics.viewDistance()*.5f,world.m_graphics.viewDistance()*.98f,maps.world(exit.targetWorld)->sky);
#ifdef FNV_WORLD_CAPTURE
        if(argc==4)saveCapture(std::string(argv[3])+"/goodsprings009-"+std::to_string(dir)+".ppm");
#endif
        require(!pending.empty(),"actual world GPU geometry submits");
        std::printf("world view %u: %u draws %u triangles, frame vertices=%u indices=%u\n",dir,r.frameStats().draws,r.frameStats().triangles,r.m_frameVertexCount,r.m_frameIndexCount);fflush(stdout);
        r.endFrame();r.waitForGpu();
    }
    for(unsigned direction=0;direction<4;++direction) {
        r.setCameraFreecam(x,y,z+124,0,direction*1.5707963f,65,world.m_graphics.viewDistance());
        checkParallelScene(r,[&]{world.render(r);});
    }
    GraphicsSettings legacy;legacy.applyPreset(3);world.setGraphics(legacy);require(world.update(x,y,r,physics),"world settings apply without leaving the current world");
    uint64_t legacyTriangles=0,legacyDraws=0;
    for(unsigned dir=0;dir<4;++dir) {
        r.setCameraFreecam(x,y,z+124,0,dir*1.5707963f,65,legacy.viewDistance());r.beginFrame();world.render(r);
        legacyTriangles+=r.frameStats().triangles;legacyDraws+=r.frameStats().draws;r.endFrame();r.waitForGpu();
    }
    printf("Comparable Goodsprings workload: Performance %llu triangles/%llu draws; legacy distance %llu triangles/%llu draws\n",(unsigned long long)performanceTriangles,(unsigned long long)performanceDraws,(unsigned long long)legacyTriangles,(unsigned long long)legacyDraws);
    require(performanceTriangles<legacyTriangles*.6 && performanceDraws<legacyDraws*.65,"Performance substantially reduces real submitted world geometry and draws");
    GraphicsSettings performance;world.setGraphics(performance);require(world.update(x,y,r,physics),"return to Performance keeps original world playable");
    r.setCameraFreecam(x,y,z+124,0,0,65,performance.viewDistance());
    Mat4 savedView=r.m_viewMatrix,savedProjection=r.m_projMatrix;
    r.beginFrame();r.distanceFog(performance.viewDistance()*.5f,performance.viewDistance()*.98f,maps.world(exit.targetWorld)->sky);
    require(!pending.empty() && memcmp(&savedView,&r.m_viewMatrix,sizeof(Mat4))==0 && memcmp(&savedProjection,&r.m_projMatrix,sizeof(Mat4))==0,"fog submits bounded geometry and restores the world camera");r.endFrame();r.waitForGpu();
    require(world.update(x+4096,y,r,physics),"cross original exterior cell boundary");
    require(world.m_x==(int)floorf((x+4096)/4096),"stream center follows signed original world coordinates");
    r.setCameraFreecam(x+4096,y,z+124,0,0,65,world.m_graphics.viewDistance());r.beginFrame();world.render(r);r.endFrame();r.waitForGpu();
    world.cleanup(r);physics.clearColliders();
    // Real malformed-alpha source model must preserve its staircase geometry.
    std::vector<uint8_t> stairBytes;NifLoader stair;
    require(meshes.extractFile("meshes\\architecture\\wasteland","woodsteps01nv.nif",stairBytes) && stair.load(stairBytes.data(),stairBytes.size()),"original wood stairs with invalid vertex alpha remain visible");
    require(stair.getMesh().triangleCount>0,"original stairs contain rendered triangles");
    for(const auto& c:stair.getMesh().colors)require(std::isfinite(c.a) && c.a>=0 && c.a<=1,"vertex alpha sanitized for actual GPU");
    struct Location {const char* name;uint32_t world;int x,y;};
    const Location locations[]={{"Primm",0xDA726,-14,-14},{"Nipton",0xDA726,-8,-23},{"Novac",0xDA726,5,-9},{"Boulder City",0xDA726,12,5},{"Strip",0x13B308,0,0}};
    for(const auto& loc:locations) {
        float px=(loc.x+.5f)*4096,py=(loc.y+.5f)*4096;
        require(world.load(maps,loc.world,px,py,meshes,r,physics),"distant original world location loads");
        MapTile center;require(maps.readTile(loc.x,loc.y,center),"landmark center tile decodes");
        float ground=center.hasTerrain?center.heights[16*33+16]:maps.world(loc.world)->defaultLand;
        float surface;
        require(physics.groundHeight(px,py,ground+2000,surface),"landmark camera has actual standing surface");ground=surface;
        printf("%s camera feet %.2f %.2f %.2f\n",loc.name,px,py,ground);fflush(stdout);
        for(unsigned dir=0;dir<4;++dir) {
            r.setCameraFreecam(px,py,ground+124,0,dir*1.5707963f,65,world.m_graphics.viewDistance());r.beginFrame();world.render(r);
            require(!pending.empty(),"distant original world GPU geometry submits");
            printf("%s view %u: %u draws %u triangles, frame vertices=%u indices=%u, GPU buffers %.1f MiB\n",loc.name,dir,r.frameStats().draws,r.frameStats().triangles,r.m_frameVertexCount,r.m_frameIndexCount,allocatedBytes/1048576.);fflush(stdout);
            r.endFrame();r.waitForGpu();
        }
        for(unsigned direction=0;direction<4;++direction) {
            r.setCameraFreecam(px,py,ground+124,0,direction*1.5707963f,65,world.m_graphics.viewDistance());
            checkParallelScene(r,[&]{world.render(r);});
        }
        world.cleanup(r);physics.clearColliders();
    }
    require(doc.load(maps.interiorPath(0x103DF9).c_str(),meshes,tex1,&tex2,r),"return to original Doc interior after exterior streaming");
    doc.cleanup(r);
    require(parallelViews==28 && workerJobsTotal[0]>0 && workerJobsTotal[1]>0 && workerJobsTotal[2]>0,"all three execution lanes process real scene jobs");
    require(optimizedTriangleTests<referenceTriangleTests && optimizedStateCalls<referenceStateCalls && optimizedUniforms<referenceUniforms,"grouped visibility and state reuse reduce real scene CPU work without changing submissions");
    printf("CPU workload across 28 views: triangle tests %llu -> %llu; state setters %llu -> %llu; uniform writes %llu -> %llu\n",
        (unsigned long long)referenceTriangleTests,(unsigned long long)optimizedTriangleTests,(unsigned long long)referenceStateCalls,(unsigned long long)optimizedStateCalls,(unsigned long long)referenceUniforms,(unsigned long long)optimizedUniforms);
    printf("PASS: %u real scene views serial/parallel byte-identical; jobs main=%llu core1=%llu core2=%llu; peak retained CPU scratch %.2f MiB\n",parallelViews,
        (unsigned long long)workerJobsTotal[0],(unsigned long long)workerJobsTotal[1],(unsigned long long)workerJobsTotal[2],peakScratch/1048576.);
    require(overflows==0,"full-world views fit frame geometry capacity");
    r.gpuFree(r.m_frameVerticesUid);r.gpuFree(r.m_frameIndicesUid);r.m_context=nullptr;
    require(blocks.empty() && r.m_materialTextures.empty(),"all streamed world GPU resources released");
    meshes.close();tex1.close();tex2.close();update.close();require(realFiles.empty(),"all original BSA handles closed");
    std::printf("PASS: real BSA/catalog/Doc exterior door/terrain/streaming/draw/cleanup; peak GPU buffers %.1f MiB\n",peakBytes/1048576.);
}
