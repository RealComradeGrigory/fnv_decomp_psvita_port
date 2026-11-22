#include "nif/nif_loader.h"
#include "dds/dds_loader.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

void logMsg(const char*, ...) {}

extern "C" __declspec(dllexport) int dump_full(const uint8_t* data,size_t size,const char* path) {
    NifLoader loader;if(!loader.load(data,size))return 0;const auto& mesh=loader.getMesh();
    FILE* f=fopen(path,"wb");if(!f)return 0;
    uint32_t counts[]={(uint32_t)mesh.vertices.size(),(uint32_t)mesh.indices.size(),(uint32_t)mesh.parts.size()};
    fwrite(counts,4,3,f);
    fwrite(mesh.vertices.data(),sizeof(Vec3),mesh.vertices.size(),f);
    fwrite(mesh.normals.data(),sizeof(Vec3),mesh.normals.size(),f);
    fwrite(mesh.uvs.data(),sizeof(Vec2),mesh.uvs.size(),f);
    fwrite(mesh.colors.data(),sizeof(NifColor),mesh.colors.size(),f);
    fwrite(mesh.indices.data(),2,mesh.indices.size(),f);
    for(const auto& part:mesh.parts) {
        uint32_t info[]={part.firstIndex,part.indexCount,(uint32_t)part.diffuseTexture.size(),part.transparent?1u:0u};
        fwrite(info,4,4,f);fwrite(part.diffuseTexture.data(),1,part.diffuseTexture.size(),f);
    }
    fclose(f);return 1;
}

extern "C" __declspec(dllexport) int dump_nif(const uint8_t* data, size_t size, const char* path) {
    NifLoader loader;
    if (!loader.load(data, size)) return 0;
    const auto& mesh=loader.getMesh();
    FILE* f=fopen(path,"wb"); if(!f) return 0;
    uint32_t counts[]={static_cast<uint32_t>(mesh.vertices.size()),static_cast<uint32_t>(mesh.indices.size())};
    fwrite(counts,4,2,f); fwrite(mesh.vertices.data(),sizeof(Vec3),mesh.vertices.size(),f);
    fwrite(mesh.indices.data(),2,mesh.indices.size(),f);
    for(const auto& p:mesh.parts) {
        uint32_t info[]={p.firstIndex,p.indexCount,static_cast<uint32_t>(p.diffuseTexture.size())};
        fwrite(info,4,3,f);fwrite(p.diffuseTexture.data(),1,p.diffuseTexture.size(),f);
    }
    fclose(f); return 1;
}

struct MeshStats {
    uint32_t vertices, indices, parts, materials;
    float center[3], radius;
};

extern "C" __declspec(dllexport) int check_nif(const uint8_t* data, size_t size, MeshStats* stats) {
    NifLoader loader;
    if (!loader.load(data, size)) return 0;
    const auto& mesh = loader.getMesh();
    stats->vertices = mesh.vertices.size();
    stats->indices = mesh.indices.size();
    stats->parts = mesh.parts.size();
    stats->materials = 0;
    for (const auto& part : mesh.parts) stats->materials += !part.diffuseTexture.empty();
    stats->center[0] = mesh.center.x;
    stats->center[1] = mesh.center.y;
    stats->center[2] = mesh.center.z;
    stats->radius = mesh.radius;
    return 1;
}

extern "C" __declspec(dllexport) int check_dds(const uint8_t* data, size_t size,
                                               uint32_t* width, uint32_t* height, uint8_t* firstPixel) {
    DDSLoader loader;
    if (!loader.load(data, size)) return 0;
    const auto& texture = loader.getTexture();
    *width = texture.width;
    *height = texture.height;
    memcpy(firstPixel, texture.pixels.data(), 4);
    return 1;
}
