#pragma once
#include "renderer/gxm_renderer.h"
#include <cstring>

inline Mat4 docHeadBoneBind() {
    // Original _male/skeleton.nif Bip01 Head bind transform, column-major.
    const float bind[16]={
        2.50940666e-8f,2.84650484e-3f,9.99996028e-1f,0,
        1.36517509e-6f,9.99995968e-1f,-2.84656354e-3f,0,
        -1.00000012f,1.31296477e-6f,-4.72336495e-8f,0,
        3.27634527e-6f,.402028106f,112.815664f,1};
    Mat4 result;memcpy(result.m,bind,sizeof(bind));return result;
}
inline Mat4 docHairBind() {
    const auto bind=docHeadBoneBind();
    // Hair is already Z-up; facial pieces use the full head-bone orientation.
    return Mat4::translate(bind.m[3][0],bind.m[3][1],bind.m[3][2]);
}
