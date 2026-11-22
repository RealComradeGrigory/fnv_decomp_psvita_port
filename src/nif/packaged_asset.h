#pragma once
#include <cstdio>
#include <vector>
#include <cstdint>
#ifndef FNV_APP_ROOT
#define FNV_APP_ROOT "app0:"
#endif
inline bool readPackagedAsset(const char* name, std::vector<uint8_t>& out) {
    char path[512]; snprintf(path,sizeof(path),FNV_APP_ROOT "assets/%s",name);
    FILE* f=fopen(path,"rb"); if(!f) return false;
    if(fseek(f,0,SEEK_END)!=0) {fclose(f);return false;}
    long size=ftell(f); if(size<=0 || size>8*1024*1024 || fseek(f,0,SEEK_SET)!=0) {fclose(f);return false;}
    out.resize(size);bool ok=fread(out.data(),1,out.size(),f)==out.size();fclose(f);
    if(!ok) out.clear();
    return ok;
}
