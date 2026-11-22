#include "scene_support.h"
#include "renderer/near_clip.h"
#include "npc/head_attachments.h"
#include <limits>

static std::vector<uint8_t> bytes(const std::string& path) {
    std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}
int main(int argc,char** argv) {
    require(argc==3,"004 arguments");
    std::ifstream manifest(argv[1]);std::string line;
    while(std::getline(manifest,line)) {auto tab=line.find('\t');fixturePaths[line.substr(0,tab)]=line.substr(tab+1);}
    GxmRenderer r;r.m_context=reinterpret_cast<SceGxmContext*>(1);BSAReader archive;r.setTextureArchives(&archive,nullptr);
    BasicVertex tri[3]{},polygon[4];float distances[]={-1,1,1};
    tri[0].position[2]=-1;tri[1].position[0]=2;tri[1].position[2]=1;tri[1].uv[0]=1;tri[2].position[1]=2;tri[2].position[2]=1;
    require(clipNearTriangle(tri,distances,polygon)==4,"crossing triangle clipped to quad");
    require(fabsf(polygon[0].position[0]-1)<1e-6f && fabsf(polygon[0].uv[0]-.5f)<1e-6f,"clipped intersection interpolates positions and UVs");
    float outside[]={-1,-1,-1};require(clipNearTriangle(tri,outside,polygon)==0,"fully behind triangle omitted");
    float inside[]={1,1,1};require(clipNearTriangle(tri,inside,polygon)==3,"fully front triangle preserved");

    // A triangle with one vertex behind the eye must keep the visible polygon.
    NifMesh source;source.vertices={{-2,-2,-4},{2,-2,-4},{0,2,1}};source.indices={0,1,2};
    source.vertexCount=3;source.normals.resize(3);source.uvs.resize(3);
    GxmMesh gpu;require(r.createGxmMesh(source,gpu),"clipping mesh upload");
    Mat4 projection=Mat4::perspective(1.f,1.f,2.f,1000.f);r.setViewProjection(Mat4::identity(),projection);
    r.beginFrame();r.drawMeshInstance(gpu,nullptr,Mat4::identity());
    require(!pending.empty(),"camera-crossing triangle still produces geometry");
    for(const auto& draw:pending) for(auto index:draw.indexCopy) {
        const auto& v=draw.vertexCopy[index];require(v.position[2]<-2.f,"submitted triangle lies in front of near plane");
    }
    r.endFrame();r.waitForGpu();r.destroyGxmMesh(gpu);

    // Exercise clipping with real geometry from every packaged room and 12 camera directions.
    unsigned roomDraws=0, submittedTriangles=0;
    for(const char* name:{"GSDocMitchellHouse","GSGasStation","GSGeneralStore","GSProspectorSaloonInterior","GSSchoolHouse"}) {
        CellScene scene;std::string path=std::string(argv[2])+"/cells/"+name+".cell";
        require(scene.load(path.c_str(),archive,archive,nullptr,r),"room loads for clipping audit");
        float x,y,z,yaw,pitch;scene.getSpawnPoint(x,y,z,yaw,pitch);
        for(unsigned direction=0;direction<12;++direction) {
            r.setCameraFreecam(x,y,z+60,(direction/4-1.f)*.8f,(direction%4)*1.5707963f,65);
            r.beginFrame();
            for(const auto& instance:scene.m_objects) if(instance.mesh) {
                size_t before=pending.size();r.drawMeshInstance(*instance.mesh,instance.texture,instance.worldMatrix);
                Mat4 wvp=r.m_viewProjMatrix*instance.worldMatrix;
                for(size_t i=before;i<pending.size();++i) for(auto index:pending[i].indexCopy) {
                    const auto& v=pending[i].vertexCopy[index];float d=wvp.m[3][2]+wvp.m[3][3];
                    for(unsigned k=0;k<3;++k)d+=(wvp.m[k][2]+wvp.m[k][3])*v.position[k];
                    require(d>=-.005f,"real room GPU submission never crosses behind near plane");
                }
            }
            roomDraws+=pending.size();for(const auto& d:pending)submittedTriangles+=d.indexCopy.size()/3;
            r.endFrame();r.waitForGpu();
        }
        scene.cleanup(r);
    }

    require(overflows==0,"room clipping fits immutable frame arenas");
    for(const char* file:{"eyelefthuman.nif","eyerighthuman.nif","mouthhuman.nif","teethupperhuman.nif","teethlowerhuman.nif","tonguehuman.nif","hairbaldingold.nif"}) {
        bool hair=strstr(file,"hair")!=nullptr;std::vector<uint8_t> data;
        require(archive.extractFile(hair?"meshes\\characters\\hair":"meshes\\characters\\head",file,data),"original face attachment fixture");
        NifLoader nif;require(nif.load(data.data(),data.size()),"face attachment parses");
        Mat4 bind=hair?docHairBind():docHeadBoneBind();
        for(const auto& v:nif.getMesh().vertices) {
            float z=bind.m[0][2]*v.x+bind.m[1][2]*v.y+bind.m[2][2]*v.z+bind.m[3][2];
            require(z>104 && z<132,"hair, eyes and mouth sit on head rather than at feet");
        }
    }

    const char* models[]={"meshes\\weapons\\1handpistol\\10mmpistol.nif","meshes\\weapons\\2handautomatic\\servicerifle.nif","meshes\\weapons\\1handpistol\\laserpistol.nif","meshes\\weapons\\2handrifle\\caravanshotgun.nif","meshes\\weapons\\2handrifle\\varmintrifle.nif"};
    for(unsigned weapon=0;weapon<5;++weapon) {
        NifLoader nif;GxmMesh meshes[4];
        for(unsigned i=0;i<4;++i) {
            std::vector<uint8_t> data;
            if(i==0) {std::string path=models[weapon];auto slash=path.find_last_of('\\');require(archive.extractFile(path.substr(0,slash).c_str(),path.substr(slash+1).c_str(),data),"original weapon fixture");}
            else if(i==3) data=bytes(std::string(argv[2])+"/assets/player_arms.nif");
            else data=bytes(std::string(argv[2])+"/assets/hands"+std::to_string(weapon)+(i==1?"_right.nif":"_left.nif"));
            require(nif.load(data.data(),data.size()),"animation target parses");require(r.createGxmMesh(nif.getMesh(),meshes[i]),"animation target upload");
        }
        uint32_t counts[]={meshes[0].vertexCount,meshes[1].vertexCount,meshes[2].vertexCount,meshes[3].vertexCount};
        std::string animationFile=std::string(argv[2])+"/assets/viewmodel"+std::to_string(weapon)+".vma";
        ViewmodelAnimation animation;require(animation.load(animationFile.c_str(),counts),"compressed original animations match target topology");
        GxmMesh* targets[]={&meshes[0],&meshes[1],&meshes[2],&meshes[3]};float muzzle[3];
        require(animation.apply(targets,0,0,1000,false,0,muzzle),"original hip pose");
        BasicVertex initial=meshes[1].vertices[0];
        require(muzzle[1]>20 && fabsf(muzzle[0])<25 && fabsf(muzzle[2])<30,"barrel is forward and upright in original camera coordinates");
        for(unsigned state=0;state<5;++state)for(unsigned step=0;step<=24;++step) {
            float time=step/24.f;
            require(animation.apply(targets,time*animation.duration(0),state==1||state==3?1:0,state==2||state==3?time*animation.duration(2):1000,state==4,time,muzzle),"sample all original clips");
            for(const auto& mesh:meshes)for(unsigned v=0;v<mesh.vertexCount;++v)for(float c:mesh.vertices[v].position)require(std::isfinite(c) && fabsf(c)<512,"animation coordinates stay finite/bounded");
        }
        require(animation.apply(targets,0,0,1000,true,.5f,muzzle),"reload middle frame");
        float motion=0;for(unsigned k=0;k<3;++k)motion+=fabsf(initial.position[k]-meshes[1].vertices[0].position[k]);require(motion>1,"reload is animated rather than static pose dip");
        r.setCameraFreecam(0,0,0,0,0,65);r.beginFrame();unsigned before=r.m_frameVertexCount;
        r.drawMeshInstance(meshes[0],nullptr,Mat4::identity());
        require(r.m_frameVertexCount-before<meshes[0].vertexCount*2,"multipart animated weapon snapshot is shared");
        for(auto& mesh:meshes)mesh.vertices[0].position[0]=999; // Original buffers may change after queueing.
        r.endFrame();r.waitForGpu();
        uint32_t wrong[]={counts[0]+1,counts[1],counts[2],counts[3]};require(!animation.load(animationFile.c_str(),wrong),"wrong animation topology rejected");
        for(auto& mesh:meshes)r.destroyGxmMesh(mesh);
    }
    // Exercise the shipping load/update/draw path, with app0 mapped to the test project.
    WeaponSystem system;require(system.init(r),"integrated animation weapon init");
    require(system.initHands(archive,archive,nullptr,r),"integrated hands init");
    require(system.m_pipboyMesh.isValid(),"original Pip-Boy device loaded");
    SceCtrlData pad{};
    for(unsigned id=0;id<5;++id) {
        require(system.loadWeapon((WeaponID)id,archive,archive,nullptr,r),"integrated original weapon load");
        require(system.m_animation.valid(),"shipping weapon load found packaged animation/topology");
        r.setCameraFreecam(0,0,0,0,0,65);r.beginFrame();int before=drawCount;
        system.update(pad,0,1.f/60,0,0,0,0,0,false,false,nullptr);
        system.renderViewmodel(r,0,0,0,0,0);
        require(drawCount>before,"shipping original viewmodel submits geometry");
        r.endFrame();r.waitForGpu();
        pad.buttons=SCE_CTRL_SQUARE;system.m_currentMag=0;
        system.update(pad,SCE_CTRL_SQUARE,1.f/60,0,0,0,0,0,false,false,nullptr);
        require(system.m_isReloading,"shipping reload begins");
        r.beginFrame();system.renderViewmodel(r,0,0,0,0,0);r.endFrame();r.waitForGpu();pad.buttons=0;
    }
    system.cleanup(r);require(overflows==0,"full animation rendering fits immutable arenas");
    r.gpuFree(r.m_frameVerticesUid);r.gpuFree(r.m_frameIndicesUid);r.m_context=nullptr;
    require(blocks.empty(),"004 GPU resources released");
    std::printf("PASS: near-plane polygon/UV clipping; %u real room material draws / %u submitted triangles across 60 camera views; seven original head attachments; 25 original weapon clips and immutable multipart snapshots\n",roomDraws,submittedTriangles);
}
