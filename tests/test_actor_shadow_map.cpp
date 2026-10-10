// 0.3.208 (task 29): actor_shadow_map.h. The matrix maps the pivot to (0,0,.5), a point Radius away along the light-space x axis
// to x=1, the mapping is continuous in the pivot (no texel snap), and the inset test accepts a small centred box and rejects
// a box crossing the border, a box behind the depth planes and invalid bounds.
#include "actor_shadow_map.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace NorthlightGI;
namespace M=NorthlightActorShadowMap;
static void project(const float* m,Vec3 p,float* c){for(int a=0;a<3;++a)c[a]=p.x*m[a]+p.y*m[4+a]+p.z*m[8+a]+m[12+a];}
int main(){
    const Vec3 sun=normalized(Vec3(.3f,-.4f,.8f));
    const Vec3 pivot(1234.567f,-876.543f,52.25f);
    float m[16];M::matrix(pivot,sun,m);
    float c[3];project(m,pivot,c);
    assert(std::fabs(c[0])<1e-4f&&std::fabs(c[1])<1e-4f&&std::fabs(c[2]-.5f)<1e-5f);
    assert(m[3]==0&&m[7]==0&&m[11]==0&&m[15]==1);
    // Radius away along the frame's x axis: x=1 (and -Radius: -1), y and z unchanged.
    const auto f=M::frame(pivot,pivot,sun);
    project(m,pivot+f.x*M::Radius,c);assert(std::fabs(c[0]-1)<1e-4f&&std::fabs(c[1])<1e-4f&&std::fabs(c[2]-.5f)<1e-5f);
    project(m,pivot-f.y*M::Radius,c);assert(std::fabs(c[0])<1e-4f&&std::fabs(c[1]+1)<1e-4f);
    // Unsnapped: a pivot move of 1/10 texel moves the mapping of a fixed point by 1/10 texel in clip space (a snapped frame would not move at all).
    const float texelClip=2.f/float(M::Size);const Vec3 fixed(1236.f,-875.f,53.f);
    float m2[16];M::matrix(pivot+f.x*(.1f*(2*M::Radius/float(M::Size))),sun,m2);
    float a[3],b[3];project(m,fixed,a);project(m2,fixed,b);
    assert(std::fabs((a[0]-b[0])-.1f*texelClip)<2e-5f);
    // The unsnapped centre follows the pivot exactly: texel units of the pivot along x are not integers.
    assert(std::fabs(f.cx-dot(pivot,f.x))<1e-3f&&std::fabs(f.cy-dot(pivot,f.y))<1e-3f);
    // Inset test.
    const float lo[3]={pivot.x-.5f,pivot.y-.5f,pivot.z-.5f},hi[3]={pivot.x+.5f,pivot.y+.5f,pivot.z+.5f};
    assert(M::insideInset(true,lo,hi,m));
    assert(!M::insideInset(false,lo,hi,m));                       // invalid bounds
    const float wide[3]={pivot.x-20.f,pivot.y-20.f,pivot.z-20.f},wideHi[3]={pivot.x+20.f,pivot.y+20.f,pivot.z+20.f};
    assert(!M::insideInset(true,wide,wideHi,m));                  // larger than the map
    // A box crossing the inset border (.8*12 = 9.6 yd) along the frame x axis: centre 9.4 yd out boxes of .1 yd half extent.
    const Vec3 edge=pivot+f.x*9.0f;const float el[3]={edge.x-.1f,edge.y-.1f,edge.z-.1f},eh[3]={edge.x+.1f,edge.y+.1f,edge.z+.1f};
    assert(M::insideInset(true,el,eh,m));                         // 9.0 + extent stays below 9.6
    const Vec3 over=pivot+f.x*9.6f;const float ol[3]={over.x-.1f,over.y-.1f,over.z-.1f},oh[3]={over.x+.1f,over.y+.1f,over.z+.1f};
    assert(!M::insideInset(true,ol,oh,m));                        // crosses the border
    const Vec3 deep=pivot-f.z*700.f;const float dl[3]={deep.x-.5f,deep.y-.5f,deep.z-.5f},dh[3]={deep.x+.5f,deep.y+.5f,deep.z+.5f};
    assert(!M::insideInset(true,dl,dh,m));                        // behind the depth planes (z outside 0..1)
    const float inverted[3]={hi[0]+.1f,lo[1],lo[2]};assert(!M::insideInset(true,inverted,hi,m)); // low > high
    const float nanLow[3]={NAN,lo[1],lo[2]};assert(!M::insideInset(true,nanLow,hi,m));
    // Leash: inside it the centre is returned bit for bit; outside the light-space XY distance becomes exactly Leash in the same direction; a depth-only move
    // keeps the centre and the matrix takes its depth origin from the pivot; a jump re-centres.
    {const Vec3 centre=pivot;
     auto exact=[](Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;};
     assert(exact(M::leash(centre,true,centre,sun),centre));
     const Vec3 inside=centre+f.x*(M::Leash*.9f);Vec3 r=M::leash(centre,true,inside,sun);assert(exact(r,centre));
     const Vec3 nearEdge=centre+f.x*(M::Leash*.6f)+f.y*(M::Leash*.6f);r=M::leash(centre,true,nearEdge,sun);assert(exact(r,centre)); // .85 Leash diagonal
     const Vec3 out=centre+f.x*6.f+f.y*8.f; // 10 yd along (.6,.8)
     r=M::leash(centre,true,out,sun);const Vec3 d=out-r;
     assert(std::fabs(std::sqrt(dot(d,f.x)*dot(d,f.x)+dot(d,f.y)*dot(d,f.y))-M::Leash)<2e-3f);
     assert(dot(d,f.x)*8.f>0&&std::fabs(dot(d,f.x)/dot(d,f.y)-.75f)<1e-3f); // direction preserved
     const Vec3 deeper=centre+f.z*3.f;r=M::leash(centre,true,deeper,sun);assert(exact(r,centre));
     {float md[16],mc[16];M::matrix(r,deeper,sun,md);M::matrix(centre,sun,mc); // same XY origin, depth origin 3 yd deeper
      assert(md[12]==mc[12]&&md[13]==mc[13]&&std::fabs((md[14]-mc[14])-3.f*NorthlightWorldMath::InverseShadowDepth)<1e-5f);}
     assert(M::leash(centre,false,out,sun).x==out.x);                       // no stored centre: the pivot
     const Vec3 far=centre+f.x*(M::JumpDistance+1.f);r=M::leash(centre,true,far,sun);assert(r.x==far.x&&r.y==far.y&&r.z==far.z); // teleport
    }
    std::puts("PASS actor shadow map");
    return 0;
}
