#define main uiChecksMain
#include "ui_gpu_checks.cpp"
#undef main
#include <sstream>
#include <chrono>
#define private public
#include "scene/cell_scene.h"
#include "physics/physics_engine.h"
#include "weapon/weapon_system.h"
#undef private
#include <psp2/io/fcntl.h>

BSAReader::~BSAReader()=default;
static std::unordered_map<int,FILE*> files;
extern "C" {
SceUID sceIoOpen(const char* path,int,SceMode) {
    FILE* f=fopen(path,"rb"); if(!f) return -1;
    int id=10000+files.size(); files[id]=f; return id;
}
int sceIoClose(SceUID id) { fclose(files.at(id)); files.erase(id); return 0; }
SceSSize sceIoRead(SceUID id,void* buf,SceSize size) { return fread(buf,1,size,files.at(id)); }
}

int main(int argc,char** argv) {
    require(argc==3,"scene test arguments");
    std::ifstream manifest(argv[1]); std::string line;
    while(std::getline(manifest,line)) {
        size_t tab=line.find('\t'); fixturePaths[line.substr(0,tab)]=line.substr(tab+1);
    }
    GxmRenderer r; r.m_context=reinterpret_cast<SceGxmContext*>(1);
    BSAReader archive; r.setTextureArchives(&archive,nullptr);
    CellScene scene; PhysicsEngine physics;
    const char* names[]={"GSDocMitchellHouse","GSGasStation","GSGeneralStore","GSProspectorSaloonInterior","GSSchoolHouse"};
    for(const char* name:names) {
        std::string path=std::string(argv[2])+"/"+name+".cell";
        require(scene.load(path.c_str(),archive,archive,nullptr,r),"load real cell fixtures");
        physics.clearColliders(); scene.buildCollision(physics);
        require(physics.hasWorldGeometry(),"cell produced collision geometry");
        float x,y,z,yaw,pitch,ground; scene.getSpawnPoint(x,y,z,yaw,pitch);
        require(physics.findStandingPosition(x,y,z),"find clear standing spawn");
        require(physics.groundHeight(x,y,z,ground),"spawn has a supporting floor");
        float feet=ground,v=0; bool grounded=true;
        for(int i=0;i<60;i++) physics.stepPlayer(x,y,feet,v,0,0,0,grounded,false,1.f/60);
        float settled=feet;
        for(int i=0;i<30;i++) physics.stepPlayer(x,y,feet,v,0,0,0,grounded,false,1.f/60);
        require(physics.groundHeight(x,y,feet+.1f,ground) && grounded && fabsf(feet-ground)<.1f && fabsf(feet-settled)<.1f,
                "spawn settles on supporting surface");
        r.beginFrame(); scene.render(r); r.endFrame(); r.waitForGpu();
        std::string oldName=scene.getCellName(); size_t oldBlocks=blocks.size();
        require(!scene.load("missing.cell",archive,archive,nullptr,r),"failed cell load rejected");
        require(scene.getCellName()==oldName && blocks.size()==oldBlocks,"failed cell load retains resources");
        size_t collisionBytes=physics.m_triangles.capacity()*sizeof(PhysicsEngine::Triangle);
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<120;i++) physics.stepPlayer(x,y,feet,v,.5f,.3f,240,grounded,false,1.f/60);
        auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        std::printf("%s: %u objects, %zu triangles, GPU buffers %.1f MiB, triangle storage %.1f MiB, host move %.0f us/step\n",
                    name,scene.getObjectCount(),physics.m_triangles.size(),allocatedBytes/1048576.,collisionBytes/1048576.,us/120.);
    }
    WeaponSystem weapon; require(weapon.init(r),"weapon init"); weapon.setPhysics(&physics);
    require(weapon.loadWeapon(WeaponID::Pistol10mm,archive,archive,nullptr,r),"pistol fixtures load");
    SceCtrlData pad{}; pad.buttons=SCE_CTRL_RTRIGGER;
    auto update=[&](uint32_t pressed,float dt=.2f) {
        weapon.update(pad,pressed,dt,0,0,124,0,0,false,false,nullptr);
    };
    update(SCE_CTRL_RTRIGGER); int mag=weapon.getCurrentMag();
    update(0); require(weapon.getCurrentMag()==mag,"semiauto needs a new press");
    require(weapon.loadWeapon(WeaponID::ServiceRifle,archive,archive,nullptr,r),"rifle fixtures load");
    update(SCE_CTRL_RTRIGGER); update(0); require(weapon.getCurrentMag()==18,"automatic fires while held");
    require(weapon.loadWeapon(WeaponID::Pistol10mm,archive,archive,nullptr,r),"switch back pistol");
    require(weapon.getCurrentMag()==mag,"magazine survives switching");
    require(weapon.loadWeapon(WeaponID::VarmintRifle,archive,archive,nullptr,r),"varmint fixtures load");
    int reserve=weapon.getReserveAmmo(); weapon.m_currentMag=0; pad.buttons=SCE_CTRL_SQUARE;
    update(SCE_CTRL_SQUARE); update(0,5.f);
    require(weapon.getReserveAmmo()==reserve-5,"reload consumes reserve");
    require(weapon.loadWeapon(WeaponID::ServiceRifle,archive,archive,nullptr,r),"switch rifle");
    require(weapon.getReserveAmmo()==reserve-5,"5.56 ammo pool shared");
    weapon.cleanup(r); scene.cleanup(r);
    r.gpuFree(r.m_frameVerticesUid); r.gpuFree(r.m_frameIndicesUid); r.m_context=nullptr;
    require(r.m_materialTextures.empty() && blocks.empty(),"all cell and weapon resources released");
    std::printf("PASS: 5 real cell loads/renders, stable spawns, rollback, weapon ammo/input; peak mapped buffers %.1f MiB\n",peakBytes/1048576.);
}
