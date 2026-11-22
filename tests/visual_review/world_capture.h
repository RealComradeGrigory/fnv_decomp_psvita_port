// CPU rasterization of the real renderer's queued GXM draw calls. This checks
// geometry/UV submission; it does not emulate Vita shaders, filtering or timing.
#include <array>
#include <cmath>
static GxmRenderer* captureRenderer=nullptr;
static const SceGxmTexture* captureTexture=nullptr;
static Mat4 captureMatrix;
static const SceGxmVertexProgram* captureVertex=nullptr;
static const SceGxmFragmentProgram* captureFragment=nullptr;
static SceGxmDepthFunc captureFrontFunc{},captureBackFunc{};
static SceGxmDepthWriteMode captureBackWrite{};static SceGxmCullMode captureCull{};
static bool captureEnabled=false,captureTextured=false,captureWrite=true;
static uint64_t captureStateDigest=1469598103934665603ull;
static constexpr int CW=480,CH=272;
static std::array<float,CW*CH> captureDepth;
static std::array<uint8_t,CW*CH*3> capturePixels;
static std::array<const char*,CW*CH> captureOwners;
static std::unordered_map<const SceGxmTexture*,GxmTexture> captureExtraTextures;
extern "C" {
void sceGxmSetFragmentProgram(SceGxmContext*,const SceGxmFragmentProgram* p){captureFragment=p;}
void sceGxmSetCullMode(SceGxmContext*,SceGxmCullMode mode){captureCull=mode;}
void sceGxmSetBackDepthFunc(SceGxmContext*,SceGxmDepthFunc mode){captureBackFunc=mode;}
void sceGxmSetFrontDepthFunc(SceGxmContext*,SceGxmDepthFunc mode){captureFrontFunc=mode;}
void sceGxmSetBackDepthWriteEnable(SceGxmContext*,SceGxmDepthWriteMode mode){captureBackWrite=mode;}
int sceGxmTextureInitLinear(SceGxmTexture* texture,const void* data,SceGxmTextureFormat,unsigned width,unsigned height,unsigned) {
    GxmTexture t{};t.data=const_cast<void*>(data);t.width=width;t.height=height;captureExtraTextures[texture]=t;return 0;
}
int sceGxmSetFragmentTexture(SceGxmContext*,unsigned,const SceGxmTexture* t) {captureTexture=t;return 0;}
void sceGxmSetFrontDepthWriteEnable(SceGxmContext*,SceGxmDepthWriteMode mode) {captureWrite=mode==SCE_GXM_DEPTH_WRITE_ENABLED;}
int sceGxmSetUniformDataF(void*,const SceGxmProgramParameter*,unsigned,unsigned n,const float* data) {if(n==16)memcpy(&captureMatrix,data,64);return 0;}
void sceGxmSetVertexProgram(SceGxmContext*,const SceGxmVertexProgram* program) {captureVertex=program;captureTextured=captureRenderer && program==captureRenderer->m_textureVertexProgram;}
}
static void startCapture(const MapWorld& info) {
    captureDepth.fill(0);for(unsigned i=0;i<CW*CH;++i)for(unsigned c=0;c<3;++c)capturePixels[i*3+c]=info.sky[c]*255;
    captureOwners.fill("sky");
    captureEnabled=true;
}
static void saveCapture(const std::string& path) {
    captureEnabled=false;FILE* f=fopen(path.c_str(),"wb");require(f,"CPU preview file opens");
    fprintf(f,"P6\n%d %d\n255\n",CW,CH);fwrite(capturePixels.data(),1,capturePixels.size(),f);fclose(f);
    for(auto xy:std::vector<std::pair<int,int>>{{350,30},{450,30},{100,180}})printf("preview %s pixel %d,%d texture=%s\n",path.c_str(),xy.first,xy.second,captureOwners[xy.second*CW+xy.first]);
}
void hostCaptureDraw(const BasicVertex* vertices,const uint16_t* indices,unsigned count) {
    auto addState=[&](const void* pointer,size_t size){auto p=static_cast<const unsigned char*>(pointer);while(size--){captureStateDigest^=*p++;captureStateDigest*=1099511628211ull;}};
    addState(&captureVertex,sizeof(captureVertex));addState(&captureFragment,sizeof(captureFragment));
    addState(&captureFrontFunc,sizeof(captureFrontFunc));addState(&captureBackFunc,sizeof(captureBackFunc));addState(&captureBackWrite,sizeof(captureBackWrite));addState(&captureCull,sizeof(captureCull));
    addState(&captureMatrix,sizeof(captureMatrix));addState(&captureTextured,sizeof(captureTextured));addState(&captureWrite,sizeof(captureWrite));
    const SceGxmTexture* used=captureTextured?captureTexture:nullptr;addState(&used,sizeof(used));addState(&count,sizeof(count));
    if(!captureEnabled)return;
    const GxmTexture* tex=nullptr;const char* owner="untextured";
    if(captureTextured)for(const auto& pair:captureRenderer->m_materialTextures)
        if(&pair.second.texture.texture==captureTexture){tex=&pair.second.texture;owner=pair.first.c_str();break;}
    if(captureTextured && !tex){auto found=captureExtraTextures.find(captureTexture);if(found!=captureExtraTextures.end()){tex=&found->second;owner="UI atlas";}}
    struct V {float x,y,iw,u,v,c[4];};
    for(unsigned i=0;i+2<count;i+=3) {
        V v[3];bool valid=true;
        for(unsigned k=0;k<3;++k) {
            const auto& in=vertices[indices[i+k]];float clip[4];
            for(unsigned r=0;r<4;++r)clip[r]=captureMatrix.m[3][r]+captureMatrix.m[0][r]*in.position[0]+captureMatrix.m[1][r]*in.position[1]+captureMatrix.m[2][r]*in.position[2];
            if(clip[3]<=0 || !std::isfinite(clip[3])){valid=false;break;}
            auto& o=v[k];o.iw=1/clip[3];o.x=(clip[0]*o.iw+1)*CW*.5f;o.y=(1-clip[1]*o.iw)*CH*.5f;
            o.u=in.uv[0];o.v=in.uv[1];memcpy(o.c,in.color,sizeof(o.c));
        }
        if(!valid)continue;
        auto edge=[](const V& a,const V& b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
        float area=edge(v[0],v[1],v[2].x,v[2].y);if(fabsf(area)<1e-8f)continue;
        int x0=std::max(0,(int)floorf(std::min({v[0].x,v[1].x,v[2].x}))),x1=std::min(CW-1,(int)ceilf(std::max({v[0].x,v[1].x,v[2].x})));
        int y0=std::max(0,(int)floorf(std::min({v[0].y,v[1].y,v[2].y}))),y1=std::min(CH-1,(int)ceilf(std::max({v[0].y,v[1].y,v[2].y})));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            float w[3]={edge(v[1],v[2],x+.5f,y+.5f)/area,edge(v[2],v[0],x+.5f,y+.5f)/area,edge(v[0],v[1],x+.5f,y+.5f)/area};
            if(w[0]<-1e-5f || w[1]<-1e-5f || w[2]<-1e-5f)continue;
            unsigned pixel=y*CW+x;float iw=0;for(unsigned k=0;k<3;++k){w[k]*=v[k].iw;iw+=w[k];}
            if(iw<captureDepth[pixel])continue;
            float rgba[4]{};float u=0,t=0;
            for(unsigned k=0;k<3;++k){w[k]/=iw;u+=w[k]*v[k].u;t+=w[k]*v[k].v;for(unsigned c=0;c<4;++c)rgba[c]+=w[k]*v[k].c[c];}
            if(tex) {
                unsigned tx=std::min(tex->width-1,(unsigned)((u-floorf(u))*tex->width)),ty=std::min(tex->height-1,(unsigned)((t-floorf(t))*tex->height));
                const uint8_t* p=static_cast<const uint8_t*>(tex->data)+(ty*tex->width+tx)*4;
                for(unsigned c=0;c<4;++c)rgba[c]*=p[c]/255.f;
            }
            float alpha=captureWrite?1:rgba[3];
            for(unsigned c=0;c<3;++c)capturePixels[pixel*3+c]=std::clamp(rgba[c]*255*alpha+capturePixels[pixel*3+c]*(1-alpha),0.f,255.f);
            if(captureWrite)captureDepth[pixel]=iw;
            if(alpha>.5f)captureOwners[pixel]=owner;
        }
    }
}
