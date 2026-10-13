// 0.3.209 (task 29): actor_shadow_map.h. The matrix maps the pivot to (0,0,.5), a point Radius away along the light-space x axis
// to x=1, the mapping is continuous in the pivot (no texel snap), and the inset test accepts a small centred box and rejects
// a box crossing the border, a box behind the depth planes and invalid bounds.
#include "actor_shadow_map.h"
#include "actor_side_tracker.h"
#include "actor_side_hold.h"
#include "replay_bounds_near.h"
#include <chrono>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
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
    // The matrix is checked once per frame (finiteMatrix), not per box.
    assert(M::finiteMatrix(m)&&!M::finiteMatrix(nullptr));
    {float bad[16];for(int i=0;i<16;++i)bad[i]=m[i];bad[13]=INFINITY;assert(!M::finiteMatrix(bad));bad[13]=NAN;assert(!M::finiteMatrix(bad));}
    // Tiles: 64x64 of 16x16 texels; the shader's tile early out needs its 2x2 tile window (searchReach < TileTexels/2, ~4.8 texels).
    static_assert(M::Size%M::TileTexels==0&&M::Size/M::TileTexels==64,"ActorTiles is 64x64");
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
     const Vec3 deeper=centre+f.z*(M::Leash*.9f);r=M::leash(centre,true,deeper,sun);assert(exact(r,centre)); // a depth lag within Leash: bit for bit
     {float md[16],mc[16];M::matrix(r,deeper,sun,md);M::matrix(centre,sun,mc); // same XY origin, depth origin .9 Leash deeper
      assert(md[12]==mc[12]&&md[13]==mc[13]&&std::fabs((md[14]-mc[14])-M::Leash*.9f*NorthlightWorldMath::InverseShadowDepth)<1e-5f);}
     // Depth beyond Leash: the centre's depth follows the pivot, its light-space XY stays (no pop).
     const Vec3 deep3=centre+f.z*3.f;r=M::leash(centre,true,deep3,sun);
     assert(std::fabs(dot(r-centre,f.x))<1e-3f&&std::fabs(dot(r-centre,f.y))<1e-3f&&std::fabs(dot(deep3-r,f.z))<1e-3f);
     // A long move along the light direction (flying under a high sun, running toward a low one) never re-centres: 60 yd along the
     // light in 1 yd steps leaves the centre's light-space XY where it was (only the depth follows).
     {Vec3 c=centre;for(int step=1;step<=60;++step){c=M::leash(c,true,centre+f.z*float(step),sun);
        assert(std::fabs(dot(c-centre,f.x))<2e-3f&&std::fabs(dot(c-centre,f.y))<2e-3f);}
      // The depth offset never builds up (at most Leash), so a stepped light direction cannot rotate a large one into XY.
      assert(std::fabs(dot(centre+f.z*60.f-c,f.z))<=M::Leash+1e-3f);
      r=M::leash(centre,true,centre+f.z*60.f,sun);assert(std::fabs(dot(r-centre,f.x))<2e-3f&&std::fabs(dot(r-centre,f.y))<2e-3f);} // one 60 yd step too
     assert(M::leash(centre,false,out,sun).x==out.x);                       // no stored centre: the pivot
     const Vec3 far=centre+f.x*(M::JumpDistance+1.f);r=M::leash(centre,true,far,sun);assert(r.x==far.x&&r.y==far.y&&r.z==far.z); // teleport
     const Vec3 nanPivot(NAN,centre.y,centre.z);r=M::leash(centre,true,nanPivot,sun);assert(std::isnan(r.x));          // non-finite: the pivot (the caller's finiteMatrix turns the map off)
    }
    {namespace S=NorthlightActorSide; // the side tracker: flips between the actor map and the near cascade
     const float root[3]={10.2f,-3.7f,1.f};int snapshot=0,declaration=0,other=0;const auto k=S::key(&snapshot,&declaration,300,true,root);
     assert(k==S::key(&snapshot,&declaration,300,true,root));
     const float moved[3]={10.9f,-3.1f,1.5f},crossed[3]={11.1f,-3.7f,1.f};
     assert(k==S::key(&snapshot,&declaration,300,true,moved)&&k!=S::key(&snapshot,&declaration,300,true,crossed)); // 1 yd cells: animation noise keeps the key
     assert(k!=S::key(&other,&declaration,300,true,root)&&k!=S::key(&snapshot,&other,300,true,root)&&k!=S::key(&snapshot,&declaration,301,true,root)); // snapshot, declaration, count are identity
     // The ring slot offset, start and base are not parameters: a bulk replay whose slot moves every frame keeps its key (nothing else to vary).
     S::Tracker t;
     auto frame=[&](bool a,bool b,bool self,bool invalid=false){t.begin();t.note(1,a,self);t.note(2,b);t.note(3,true);if(invalid)t.invalidNear();t.endFrame();};
     frame(true,true,true);assert(t.sideFlips==0&&t.sideMatched==0&&t.frames==1&&t.selfSide==S::SelfActor);   // first frame: nothing to compare
     frame(true,true,true);assert(t.sideFlips==0&&t.sideMatched==3&&t.selfFlips==0);                          // steady
     frame(false,true,true,true);assert(t.sideFlips==1&&t.sideFlipFrames==1&&t.selfSide==S::SelfNear&&t.selfFlips==1&&t.invalidBoundsNear==1&&t.tracked==3);
     frame(true,false,false);assert(t.sideFlips==3&&t.sideFlipFrames==2&&t.selfSide==S::SelfNone&&t.selfFlips==1); // two back-and-forth; no self drawn: no self flip
     frame(true,false,true);assert(t.sideFlips==3&&t.selfFlips==2);                         // the self is back on the other side than it was last seen (near -> actor)
     t.begin();t.note(5,true);t.note(5,false);t.endFrame();t.begin();t.note(5,false);t.note(5,true);t.endFrame();assert(t.sideFlips==3); // clones swapping places: no net move
     t.begin();t.note(5,true);t.note(5,true);t.endFrame();assert(t.sideFlips==4);           // one clone joined the actor map
     t.begin();t.unkeyed();t.unsupportedNear();t.unsupportedNear();t.endFrame();assert(t.unkeyedNow==1&&t.unsupportedNow==2&&t.tracked==0);
     t.resetWindow();assert(t.sideFlips==0&&t.sideMatched==0&&t.frames==0&&t.selfFlips==0);
     S::Tracker mixed;mixed.begin();mixed.note(1,true,true);mixed.note(2,false,true);mixed.endFrame();assert(mixed.selfSide==S::SelfMixed&&std::string(S::name(mixed.selfSide))=="mixed");}
    {namespace H=NorthlightActorSideHold; // inset hysteresis and the near-first visit list
     // A box of half width w (clip units) around the map centre column: an extent of w. Matrix m maps world (x,y) to clip x,y with 12 yd = 1.
     auto extentOf=[&](float x,float y){const float lo[3]={x-.1f,y-.1f,0},hi[3]={x+.1f,y+.1f,1};return M::insetExtent(true,lo,hi,m,M::Stay);};
     const float r0[3]={0,0,0};
     // matrix whose clip x = world x/12, clip y = world y/12, depth in range
     float id[16]={};id[0]=id[5]=1.f/12;id[10]=.5f;id[12]=0;id[13]=0;id[14]=.25f;id[15]=1;
     auto ext=[&](float yards){const float lo[3]={yards-.1f,0,0},hi[3]={yards,.1f,.1f};return M::insetExtent(true,lo,hi,id,M::Stay);};
     (void)extentOf;(void)r0;
     assert(std::fabs(ext(9.f)-9.f/12)<1e-4f&&ext(10.2f)<=M::Stay&&std::isinf(ext(10.5f))&&std::isinf(M::insetExtent(false,nullptr,nullptr,id,1.f)));
     assert(M::Stay>M::Inset&&M::Stay<.88f); // before the shader's fade to lit (border .88)
     H::Hold hold;const std::uint64_t model=7;float root[3]={2,3,0};
     const float inside=9.f/12,band=10.f/12,out=10.6f/12;static_assert(H::Deep<NorthlightActorShadowMap::Inset,"");
     hold.begin();assert(hold.decide(model,root,.5f)&&!hold.decide(model,root,band)); // deep inside: no table access, so a jump from there into the band is not remembered (the near cascade for that frame)
     hold.begin();assert(!hold.decide(model,root,band));            // in the band, not remembered: near cascade (enters only inside Inset)
     hold.begin();assert(hold.decide(model,root,inside));           // enters
     hold.begin();assert(hold.decide(model,root,band));             // stays in the band
     hold.begin();assert(hold.decide(model,root,band));
     hold.begin();assert(!hold.decide(model,root,INFINITY));        // beyond Stay (or no bounds): near cascade
     hold.begin();assert(hold.decide(model,root,band));             // back within Gap frames: still held (a capture gap keeps the state)
     hold.begin();hold.begin();hold.begin();assert(!hold.decide(model,root,band)); // away for more than Gap frames: forgotten, must enter again
     hold.begin();assert(hold.decide(model,root,inside)&&!hold.decide(model,root,out));
     // Not rooted: today's rule, no memory.
     hold.begin();assert(hold.decide(model,nullptr,inside));hold.begin();assert(!hold.decide(model,nullptr,band));
     // Clones of one snapshot at different roots keep their own states.
     {H::Hold h;const float a[3]={0,0,0},b[3]={8,0,0};h.begin();assert(h.decide(1,a,inside)&&!h.decide(1,b,band));
      h.begin();assert(h.decide(1,a,band)&&!h.decide(1,b,band));h.begin();assert(h.decide(1,b,inside));h.begin();assert(h.decide(1,a,band)&&h.decide(1,b,band));}
     // Two clones within Reach of each other: each claims one slot (a slot matches once per pass).
     {H::Hold h;const float a[3]={0,0,0},b[3]={.5f,0,0};h.begin();assert(h.decide(1,a,inside)&&h.decide(1,b,inside));
      h.begin();assert(h.decide(1,a,band)&&h.decide(1,b,band));assert(h.remembered()==2);}
     // A root walking across a cell border (8 yd cells) and on keeps its state, also fast (0.9 yd per pass).
     {H::Hold h;float w[3]={3.2f,1.5f,0};h.begin();assert(h.decide(1,w,inside));
      for(int i=0;i<40;++i){w[0]+=.9f;w[1]-=.3f;h.begin();assert(h.decide(1,w,band));}
      assert(h.remembered()==1);}
     // The root moves farther than Reach in one pass: a different caster (not held).
     {H::Hold h;float w[3]={0,0,0};h.begin();assert(h.decide(1,w,inside));w[0]=1.6f;h.begin();assert(!h.decide(1,w,band));}
     // invalidate(): everything expires at once.
     {H::Hold h;h.begin();assert(h.decide(1,root,inside));h.invalidate();h.begin();assert(!h.decide(1,root,band));}
     // Near list: only rooted casters within Yards of the pivot, in index order; off without a pivot.
     {namespace N=NorthlightReplayBoundsNear;N::List l;const float pivot[3]={100,50,10};const float nearRoot[3]={110,60,10},farRoot[3]={130,50,10};
      l.begin(pivot,false);l.consider(0,true,nearRoot);assert(l.index.empty());
      l.begin(pivot,true);l.consider(0,true,farRoot);l.consider(1,true,nearRoot);l.consider(2,false,nearRoot);l.consider(3,true,pivot);
      assert(l.index.size()==2&&l.index[0]==1&&l.index[1]==3);
      // The walk rotates like pass(): each phase keeps its own start across frames, honours the budget stop, and the list is rebuilt between frames.
      for(unsigned i=4;i<10;++i)l.consider(i,true,pivot); // near list 1,3,4..9
      std::vector<std::size_t> seen;unsigned budget=2;
      assert(N::nearFirst(&l,0,[&]{return budget>0;},[&](std::size_t i){seen.push_back(i);--budget;return false;})&&seen.size()==2&&seen[0]==1&&seen[1]==3);
      assert(l.cursor[0]==2&&l.cursor[1]==0&&l.cursor[2]==0);
      seen.clear();budget=2;N::nearFirst(&l,0,[&]{return budget>0;},[&](std::size_t i){seen.push_back(i);--budget;return false;});assert(seen[0]==4&&seen[1]==5); // continues, not from the start
      l.begin(pivot,true);l.consider(0,true,nearRoot);assert(l.cursor[0]==4); // begin() keeps the cursors
      // the first packet spends the whole phase (Budget): the next frame starts past it
      {N::List one;one.begin(pivot,true);for(unsigned i=0;i<5;++i)one.consider(i,true,pivot);bool spent=false;
       assert(N::nearFirst(&one,1,[&]{return !spent;},[&](std::size_t){spent=true;return true;})&&one.cursor[1]==1);}
      // walking the whole list without a stop returns false
      {unsigned n=0;N::List one;one.begin(pivot,true);one.consider(0,true,pivot);one.consider(1,true,pivot);assert(!N::nearFirst(&one,2,[]{return true;},[&](std::size_t){++n;return false;})&&n==2);}
      N::nearFirst(nullptr,0,[]{return true;},[](std::size_t){assert(false);return false;});}
     // Cost: per-replay ns of the old inset test against the hysteresis decision (about 500 replays, 1 in 12 near the player (a crowded scene), half of all skinned), and of the near list inside the pinning loop.
     {const unsigned n=512;std::vector<float> lo(n*3),hi(n*3);std::vector<float> roots(n*3);std::vector<unsigned char> skinned(n);
      for(unsigned i=0;i<n;++i){float x=float((i*37)%60)-30.f,y=float((i*13)%24)-12.f;
          if(i%12==0){const float u=float((i*2654435761u>>8)%1000)/1000.f,a=float((i*40503u)%628)/100.f,r=12.f*std::sqrt(u)*.86f;x=r*std::cos(a);y=r*std::sin(a);} /* uniform over the disc inside Stay */
          lo[i*3]=x;lo[i*3+1]=y;lo[i*3+2]=0;hi[i*3]=x+.6f;hi[i*3+1]=y+.6f;hi[i*3+2]=1;roots[i*3]=x;roots[i*3+1]=y;roots[i*3+2]=0;skinned[i]=i%2==0;}
      H::Hold h;volatile unsigned sink=0;const unsigned rounds=4000;
      for(unsigned i=0;i<n;++i){const float e=M::insetExtent(true,&lo[i*3],&hi[i*3],id,M::Stay);if(skinned[i]&&e<=M::Stay)h.decide(i,&roots[i*3],e);} // warm
      auto time=[&](auto body){double best=1e30;for(int rep=0;rep<5;++rep){const auto t0=std::chrono::steady_clock::now();for(unsigned r=0;r<rounds;++r)body();
          best=std::min(best,std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t0).count()/double(rounds)/double(n));}return best;};
      const double before=time([&]{unsigned c=0;for(unsigned i=0;i<n;++i)c+=M::insideInset(true,&lo[i*3],&hi[i*3],id);sink=sink+c;});
      const double extentOnly=time([&]{unsigned c=0;for(unsigned i=0;i<n;++i)c+=M::insetExtent(true,&lo[i*3],&hi[i*3],id,M::Stay)<=M::Inset;sink=sink+c;});
      const double after=time([&]{h.begin();unsigned c=0;for(unsigned i=0;i<n;++i){bool in=false;const float e=M::insetExtent(true,&lo[i*3],&hi[i*3],id,M::Stay);
          if(e<=M::Stay)in=skinned[i]?h.decide(i,&roots[i*3],e):e<=M::Inset;c+=in;}sink=sink+c;});
      NorthlightReplayBoundsNear::List l;const float pivot[3]={0,0,0};
      const double none=time([&]{unsigned c=0;for(unsigned i=0;i<n;++i)c+=roots[i*3]>1e9f;sink=sink+c;});
      const double listed=time([&]{l.begin(pivot,true);for(unsigned i=0;i<n;++i)l.consider(i,skinned[i],&roots[i*3]);sink=sink+unsigned(l.index.size());});
      std::printf("cost per replay: inset test before %.2f ns, hysteresis decision after %.2f ns (the extent alone %.2f ns); near list %.2f ns (empty loop %.2f ns)\n",before,after,extentOnly,listed,none);
      (void)sink;}}
    std::puts("PASS actor shadow map");
    return 0;
}
