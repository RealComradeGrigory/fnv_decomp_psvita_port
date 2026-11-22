#pragma once
#include "original_font.h"
#include <psp2/ctrl.h>
#include <cstdio>

struct GraphicsSettings {
    int preset=0,distance=1,props=1,plants=0,lod=0,distantObjects=0,fog=1,showFps=1,parallelCpu=1;
    float viewDistance() const {const float v[]={4096,6144,8192,12288,24576,600000};return v[distance];}
    float propDistance() const {const float v[]={384,768,1536,3072,600000};return v[props];}
    float plantDistance() const {const float v[]={512,1536,4096,600000};return v[plants];}
    void applyPreset(int index) {
        preset=index;
        if(index==0){distance=1;props=1;plants=0;lod=0;distantObjects=0;fog=1;}
        if(index==1){distance=2;props=2;plants=1;lod=1;distantObjects=1;fog=1;}
        if(index==2){distance=3;props=3;plants=2;lod=2;distantObjects=1;fog=1;}
        if(index==3){distance=5;props=4;plants=3;lod=3;distantObjects=1;fog=0;}
    }
    bool valid() const {return preset>=-1 && preset<=3 && distance>=0 && distance<6 && props>=0 && props<5 && plants>=0 && plants<4 && lod>=0 && lod<4 && distantObjects>=0 && distantObjects<=1 && fog>=0 && fog<=1 && showFps>=0 && showFps<=2 && parallelCpu>=0 && parallelCpu<=1;}
    bool load(const char* path) {
        FILE* f=fopen(path,"r");if(!f)return false;GraphicsSettings s;int version=0;
        int n=fscanf(f,"FNVGRAPHICS %d %d %d %d %d %d %d %d %d",&version,&s.preset,&s.distance,&s.props,&s.plants,&s.lod,&s.distantObjects,&s.fog,&s.showFps);
        bool ok=n==9 && (version>=1 && version<=3);
        if(ok && version>=2)ok=fscanf(f," %d",&s.parallelCpu)==1;
        fclose(f);
        if(!ok || !s.valid())return false;
        *this=s;return true;
    }
    bool save(const char* path) const {
        if(!valid())return false;
        FILE* f=fopen(path,"w");if(!f)return false;
        bool ok=fprintf(f,"FNVGRAPHICS 3 %d %d %d %d %d %d %d %d %d\n",preset,distance,props,plants,lod,distantObjects,fog,showFps,parallelCpu)>0;
        if(fclose(f))ok=false;
        return ok;
    }
};

class GraphicsMenu {
    OriginalFont m_font;int m_row=0;bool m_open=false;
    float m_timings[8]{};bool m_hasTimings=false;
public:
    bool init(GxmRenderer& r,BSAReader* a,BSAReader* b){return m_font.init(r,a,b);}
    void cleanup(GxmRenderer& r){m_font.cleanup(r);}
    bool isOpen()const{return m_open;}
    void toggle(){m_open=!m_open;}
    // Returns true when settings changed. Exit requires an explicit menu choice.
    bool update(uint32_t pressed,GraphicsSettings& s,bool& quit) {
        if(!m_open)return false;
        if(pressed&SCE_CTRL_CIRCLE){m_open=false;return false;}
        if(pressed&SCE_CTRL_UP)m_row=(m_row+10)%11;
        if(pressed&SCE_CTRL_DOWN)m_row=(m_row+1)%11;
        if(!(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS)))return false;
        if(m_row==0){if(pressed&SCE_CTRL_CROSS)m_open=false;return false;}
        if(m_row==10){if(pressed&SCE_CTRL_CROSS)quit=true;return false;}
        int step=(pressed&SCE_CTRL_LEFT)?-1:1;
        auto cycle=[&](int& n,int count){n=(n+step+count)%count;};
        if(m_row==1)s.applyPreset((std::max(0,s.preset)+step+4)%4);
        else {
            if(m_row!=8 && m_row!=9)s.preset=-1;
            if(m_row==2)cycle(s.distance,6);
            if(m_row==3)cycle(s.props,5);
            if(m_row==4)cycle(s.plants,4);
            if(m_row==5)cycle(s.lod,4);
            if(m_row==6)s.distantObjects^=1;
            if(m_row==7)s.fog^=1;
            if(m_row==8)cycle(s.showFps,3);
            if(m_row==9)s.parallelCpu^=1;
        }
        return true;
    }
    void setTimings(float update,float scene,float weapon,float wait,float present,float pose=0,float viewmodel=0,float ui=0) {
        const float values[]={update,scene,weapon,pose,viewmodel,ui,wait,present};
        for(unsigned i=0;i<8;++i)m_timings[i]=m_hasTimings?m_timings[i]*.9f+values[i]*.1f:values[i];
        m_hasTimings=true;
    }
    void fps(GxmRenderer& r,float value,int mode=1) {
        char label[64];snprintf(label,sizeof(label),"%.1f FPS",value);m_font.text(label,810,16,22,1,.72f,.18f);
        if(mode==2) {
            const char* names[]={"Update CPU","Scene CPU","Weapon/UI CPU","Pose CPU","Weapon draw CPU","HUD/UI CPU","GPU wait","Present wait"};
            for(unsigned i=0;i<8;++i){snprintf(label,sizeof(label),"%s: %.1f ms",names[i],m_timings[i]);m_font.text(label,16,16+i*22,18,1,.72f,.18f);}
        }
        m_font.flush(r);
    }
    void render(GxmRenderer& r,const GraphicsSettings& s,float measuredFps,bool saved) {
        BasicVertex v[4]{};alignas(4) uint16_t indices[]={0,1,2,2,1,3};
        for(unsigned i=0;i<4;++i){v[i].position[0]=(i&1)?960:0;v[i].position[1]=(i>>1)?544:0;v[i].color[3]=.96f;}
        r.draw2D(v,4,indices,6);
        const char* names[]={"Resume","Preset","View distance","Clutter distance","Vegetation distance","World detail","Distant object LOD","Distance fog","Show FPS","CPU workers","Exit to LiveArea"};
        const char* presets[]={"Performance","Balanced","Quality","00.06 distance"};
        const char* levels[]={"Very low","Low","Medium","Original"};
        const char* distances[]={"4096","6144","8192","12288","24576","00.06 (600000)"};
        const char* props[]={"384","768","1536","3072","Unlimited"};
        const char* plants[]={"512","1536","4096","Unlimited"};
        const char* fpsModes[]={"Off","FPS","FPS + timings"};
        const char* values[]={"",s.preset<0?"Custom":presets[s.preset],distances[s.distance],props[s.props],plants[s.plants],levels[s.lod],s.distantObjects?"On":"Off",s.fog?"On":"Off",fpsModes[s.showFps],s.parallelCpu?"On":"Off",""};
        m_font.text("Settings",180,32,34,1,.72f,.18f);
        for(int i=0;i<11;++i){float y=84+i*31;float a=i==m_row?1:.68f;m_font.text(i==m_row?">":"",150,y,24,1,.72f,.18f,a);m_font.text(names[i],180,y,24,1,.72f,.18f,a);m_font.text(values[i],570,y,24,1,.72f,.18f,a);}
        char info[80];snprintf(info,sizeof(info),"Last gameplay rate: %.1f FPS",measuredFps);m_font.text(info,180,444,20,1,.72f,.18f);
        m_font.text(saved?"Changes saved":"Settings could not be saved",180,472,20,1,.72f,.18f);
        m_font.text("Up/Down: select   Left/Right: change   Circle/START: resume",180,503,18,1,.72f,.18f);m_font.flush(r);
    }
};
