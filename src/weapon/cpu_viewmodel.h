#pragma once
#include "renderer/gxm_renderer.h"

// CPU-owned animation buffers; the original meshes retain GPU/texture ownership.
// Dynamic submission copies these views into the immutable GPU frame arena.
struct CpuViewmodelMesh {
    std::vector<BasicVertex> vertices;
    std::vector<uint16_t> indices;
    GxmMesh view;
    void clear() {view={};vertices.clear();indices.clear();}
    bool init(const GxmMesh& source) {
        clear();if(!source.isValid())return false;
        vertices.assign(source.vertices,source.vertices+source.vertexCount);
        indices.assign(source.indices,source.indices+source.indexCount);
        view.vertices=vertices.data();view.indices=indices.data();view.vertexCount=source.vertexCount;view.indexCount=source.indexCount;
        view.cpuDynamicVertices=vertices.data();view.cpuDynamicIndices=indices.data();
        view.parts=source.parts;view.transparent=source.transparent;view.dynamic=true;view.clustersValid=false;
        return true;
    }
};
