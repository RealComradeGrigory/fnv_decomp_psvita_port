#include "physics/physics_engine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

void logMsg(const char*, ...) {}
static void require(bool value, const char* name) {
    if (!value) { std::printf("FAIL: %s\n", name); std::exit(1); }
}
static void plane(PhysicsEngine& p, float z, float x0=-1000, float x1=1000) {
    p.addTriangle({x0,-1000,z}, {x1,-1000,z}, {x1,1000,z});
    p.addTriangle({x0,-1000,z}, {x1,1000,z}, {x0,1000,z});
}
int main() {
    PhysicsEngine p;
    p.init(); plane(p, 0);
    p.addTriangle({100,-500,0}, {100,500,0}, {100,500,400});
    p.addTriangle({100,-500,0}, {100,500,400}, {100,-500,400});
    float x=0,y=0,z=200,v=0; bool grounded=false;
    for(int i=0;i<180;i++) p.stepPlayer(x,y,z,v,0,0,120,grounded,false,1.f/60);
    require(grounded && fabsf(z)<.01f, "fall onto triangle floor");
    for(int i=0;i<120;i++) p.stepPlayer(x,y,z,v,1,0,180,grounded,false,1.f/60);
    require(x>77 && x<78.1f, "stop at wall capsule radius");
    float oldY=y;
    for(int i=0;i<30;i++) p.stepPlayer(x,y,z,v,.707f,.707f,180,grounded,false,1.f/60);
    require(x<78.1f && y>oldY+55, "slide along wall");
    x=y=z=v=0; grounded=true;
    p.stepPlayer(x,y,z,v,0,0,120,grounded,true,1.f/60);
    require(z>0 && !grounded, "jump leaves ground");
    for(int i=0;i<180;i++) p.stepPlayer(x,y,z,v,0,0,120,grounded,false,1.f/60);
    require(grounded && fabsf(z)<.01f, "jump lands");
    plane(p, 160);
    float maximum=0;
    for(int i=0;i<90;i++) {
        p.stepPlayer(x,y,z,v,0,0,120,grounded,i==0,1.f/60);
        maximum=fmaxf(maximum,z);
    }
    require(maximum<=25.01f && grounded, "ceiling stops jump");
    float hx,hy,hz,nx,ny,nz;
    require(p.raycast(0,0,124,1,0,0,500,hx,hy,hz,nx,ny,nz) && fabsf(hx-100)<.01f && nx<-.9f, "wall raycast");
    require(!p.raycast(0,0,124,0,1,0,500,hx,hy,hz,nx,ny,nz), "raycast miss");
    require(!p.raycast(0,0,124,0,0,0,500,hx,hy,hz,nx,ny,nz), "zero direction rejected");
    p.clearColliders(); plane(p,0); plane(p,20,50,250);
    p.addTriangle({50,-500,0},{50,500,0},{50,500,20});
    p.addTriangle({50,-500,0},{50,500,20},{50,-500,20});
    x=y=z=v=0; grounded=true;
    for(int i=0;i<60;i++) p.stepPlayer(x,y,z,v,1,0,100,grounded,false,1.f/60);
    require(x>90 && fabsf(z-20)<.01f && grounded, "climb low step");
    p.clearColliders(); p.buildCollidersFromCell(0,-1000,1000,-1000,1000);
    p.addBoxCollider(20,20,0,30,30,50);
    require(!p.raycast(0,0,100,0,1,0,500,hx,hy,hz,nx,ny,nz), "parallel slab miss");
    std::puts("PASS: 11 physics regressions");
}
