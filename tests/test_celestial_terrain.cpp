#include "celestial_terrain.h"
#include "world_mesh_pages.h"
#include "shadow_terrain.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#include <random>
#include <limits>
#include <algorithm>
struct V{float x,y,z;};
static void project(V p,const float* m,double* out){
    for(unsigned c=0;c<4;++c)out[c]=double(p.x)*m[c]+double(p.y)*m[4+c]+double(p.z)*m[8+c]+m[12+c];
}
static bool cornerReject(V lo,V hi,const float* m){
    bool outside[6]={true,true,true,true,true,true};
    for(int bits=0;bits<8;++bits){double q[4];project({bits&1?hi.x:lo.x,bits&2?hi.y:lo.y,bits&4?hi.z:lo.z},m,q);
        const double planes[]={q[3]+q[0],q[3]-q[0],q[3]+q[1],q[3]-q[1],q[2],q[3]-q[2]};
        for(unsigned k=0;k<6;++k)outside[k]&=planes[k]<0;
    }
    return std::any_of(outside,outside+6,[](bool v){return v;});
}
static void realPages(const std::string& root);
static void syntheticRuns();
static void reuseShift();
int main(int argc,char** argv){
    reuseShift();
    using namespace NorthlightCelestialTerrain;
    std::mt19937 random(114);std::uniform_real_distribution<float> point(-6000,6000),extent(0,120);
    unsigned cases=0,retained=0;
    const float camera[]={-9000,-3500,40},color[]={1,1,1};
    for(float elevation:{.2f,2.f,5.f,15.f,43.f,85.f})for(float halo:{2.5f,10.f}){
        const float e=elevation*3.14159265f/180;float direction[]={std::cos(e)*.70710678f,std::cos(e)*.70710678f,std::sin(e)};
        NorthlightCelestialDisc::Disc disc;assert(NorthlightCelestialDisc::prepare(direction,color,1,.02f,disc));
        float m[16];matrix(disc,camera,halo,1024,m);Frustum frustum(m);
        // Distant viewer rays map to the SAME celestial pixel at every depth.
        // Include mountains beyond both the old 928 square and native far clip.
        for(float distance:{16.f,900.f,1500.f,4096.f,5800.f})for(float x:{-.95f,0.f,.95f})for(float y:{-.95f,0.f,.95f}){
            float p[3];for(int k=0;k<3;++k)p[k]=camera[k]+distance*(disc.direction[k]+disc.tangentRadius*(disc.right[k]*x+disc.up[k]*y));
            double q[4];project({p[0],p[1],p[2]},m,q);
            // D3D9's -half-texel clip translation + texture-center addressing.
            const double u=q[0]/q[3]*.5+.5+.5/1024,v=-q[1]/q[3]*.5+.5+.5/1024;
            const double tolerance=distance<100?.001:.0001; // <=1 near-mask texel; <.11 texel for distant mountains
            assert(std::fabs(u-(.5+.5*x/halo))<tolerance&&std::fabs(v-(.5-.5*y/halo))<tolerance);
            assert(q[2]>=0&&q[2]<=q[3]);
            assert(!frustum.reject(V{p[0]-.1f,p[1]-.1f,p[2]-.1f},V{p[0]+.1f,p[1]+.1f,p[2]+.1f}));++cases;
        }
        V back{camera[0]-100*direction[0],camera[1]-100*direction[1],camera[2]-100*direction[2]};assert(frustum.reject(back,back));
        V beyond{camera[0]+20000*direction[0],camera[1]+20000*direction[1],camera[2]+20000*direction[2]};assert(frustum.reject(beyond,beyond));
        for(unsigned i=0;i<20000;++i){
            V lo{camera[0]+point(random),camera[1]+point(random),camera[2]+point(random)},hi{lo.x+extent(random),lo.y+extent(random),lo.z+extent(random)};
            const bool rejected=frustum.reject(lo,hi);
            if(rejected)assert(cornerReject(lo,hi,m));else ++retained;
            ++cases;
        }
        // A near-plane-crossing box cannot disappear merely because some of
        // its corners are behind the camera. Unknown inputs fail open.
        assert(!frustum.reject(V{camera[0]-2,camera[1]-2,camera[2]-2},V{camera[0]+2,camera[1]+2,camera[2]+2}));
        float invalid[16];std::copy(m,m+16,invalid);invalid[0]=NAN;assert(!Frustum(invalid).reject(back,back));
    }
    assert(retained>0);
    std::printf("PASS celestial terrain: %u perspective/ray/culling cases, %u retained random boxes; far mountains, behind camera, near-plane crossing and invalid input checked\n",cases,retained);
    syntheticRuns();
    if(argc>1)realPages(argv[1]);
}
// ---- 0.3.175 S1: merged mask draws ------------------------------------------------------------
namespace P=NorthlightWorldMeshPages;
struct Mask {unsigned size;std::vector<std::uint8_t> bits;std::uint64_t triangles=0,hash=0;explicit Mask(unsigned n):size(n),bits(std::size_t(n)*n,0){}
    bool operator==(const Mask& o)const{return bits==o.bits&&triangles==o.triangles&&hash==o.hash;}};
// CPU raster of one DrawIndexedPrimitive (triangle list): pixel centres inside the projected triangle
// (all w > 0) are set to 1, as the mask PS writes 1 without depth or blending. Triangles with a vertex
// behind the eye are clipped by the GPU; they still enter the order-independent triangle hash.
static void drawRange(Mask& mask,const P::Page& page,const float* m,std::uint32_t minVertex,std::uint32_t vertexCount,std::uint32_t start,std::uint32_t count){
    for(std::uint32_t t=0;t<count;++t){double x[3],y[3];bool front=true;std::uint64_t h=1469598103934665603ull;
        for(unsigned k=0;k<3;++k){const std::uint32_t index=page.indices.at(std::size_t(start)+3*t+k);assert(index>=minVertex&&index<std::uint64_t(minVertex)+vertexCount); /* the DIP vertex range covers every index */
            const auto& v=page.vertices.at(index).position;double q[4];project({v.x,v.y,v.z},m,q);front=front&&q[3]>1e-6;
            x[k]=(q[0]/q[3]*.5+.5)*mask.size;y[k]=(-q[1]/q[3]*.5+.5)*mask.size;
            std::uint32_t bits[3];std::memcpy(bits,&v,12);for(auto b:bits)h=(h^b)*1099511628211ull;}
        ++mask.triangles;mask.hash+=h*0x9e3779b97f4a7c15ull; /* multiset of triangles */
        if(!front)continue;
        const double area=(x[1]-x[0])*(y[2]-y[0])-(x[2]-x[0])*(y[1]-y[0]);if(area==0)continue;
        auto clamp=[&](double v){return int(std::min(double(mask.size),std::max(-1.,v)));};
        const int x0=std::max(0,clamp(std::floor(std::min({x[0],x[1],x[2]})))),x1=std::min(int(mask.size)-1,clamp(std::ceil(std::max({x[0],x[1],x[2]}))));
        const int y0=std::max(0,clamp(std::floor(std::min({y[0],y[1],y[2]})))),y1=std::min(int(mask.size)-1,clamp(std::ceil(std::max({y[0],y[1],y[2]}))));
        for(int py=y0;py<=y1;++py)for(int px=x0;px<=x1;++px){const double cx=px+.5,cy=py+.5;
            const double e0=(x[1]-x[0])*(cy-y[0])-(cx-x[0])*(y[1]-y[0]),e1=(x[2]-x[1])*(cy-y[1])-(cx-x[1])*(y[2]-y[1]),e2=(x[0]-x[2])*(cy-y[2])-(cx-x[2])*(y[0]-y[2]);
            if(area>0?(e0>=0&&e1>=0&&e2>=0):(e0<=0&&e1<=0&&e2<=0))mask.bits[std::size_t(py)*mask.size+px]=1;}}
}
enum class Merge {Exact,IgnorePage,IgnoreGap};
// The renderer's grouping (Exact) or a broken one (counterfactuals).
template<class Accept> static NorthlightCelestialTerrain::RunStats draw(const P::Plan& plan,const std::vector<std::uint32_t>& list,Accept accept,const float* m,Merge merge,Mask& mask){
    NorthlightCelestialTerrain::RunStats stats;
    if(merge==Merge::Exact){NorthlightCelestialTerrain::forEachRun(plan.batches,list,P::PageIndexLimit,accept,[&](const NorthlightCelestialTerrain::Run& r){
            drawRange(mask,plan.pages.at(r.page),m,r.minVertex,std::uint32_t(r.vertexEnd-r.minVertex),r.start,r.count);return true;},stats);return stats;}
    NorthlightCelestialTerrain::Run run;bool open=false;
    auto flush=[&]{if(open){++stats.runs;drawRange(mask,plan.pages.at(run.page),m,0,std::uint32_t(plan.pages.at(run.page).vertices.size()),run.start,run.count);}};
    for(auto i:list){const auto& b=plan.batches[i];if(!accept(b))continue;++stats.accepted;
        const bool page=merge==Merge::IgnorePage||b.page==run.page,contiguous=merge==Merge::IgnoreGap||run.start+3*run.count==b.start;
        if(open&&page&&contiguous){run.count=merge==Merge::IgnoreGap?(b.start-run.start)/3+b.count:run.count+b.count;continue;}
        flush();run.page=b.page;run.start=b.start;run.count=b.count;open=true;}
    flush();return stats;
}
static void perBatch(const P::Plan& plan,const std::vector<std::uint32_t>& list,const NorthlightCelestialTerrain::Frustum& f,const float* m,Mask& mask,unsigned& accepted){
    for(auto i:list){const auto& b=plan.batches[i];if(!b.terrain||f.reject(b.boundsLow,b.boundsHigh))continue;++accepted;drawRange(mask,plan.pages.at(b.page),m,b.minVertex,b.vertexCount,b.start,b.count);}
}
// Two pages whose index ranges touch (page 0 ends where page 1's batch starts): merging across the
// page draws page 1's indices from page 0's vertices.
static void syntheticRuns(){
    P::Plan plan;plan.pages.resize(2);
    for(unsigned pg=0;pg<2;++pg){auto& page=plan.pages[pg];for(unsigned v=0;v<6;++v){NorthlightGI::WorldVertex w{};w.position=NorthlightGI::Vec3(float(v%3)+pg*10.f,float(v/3),float(pg));page.vertices.push_back(w);}
        for(std::uint32_t i:{0u,1u,3u,1u,4u,3u})page.indices.push_back(i);}
    for(unsigned pg=0;pg<2;++pg)for(unsigned h=0;h<2;++h){P::Batch b;b.terrain=true;b.page=pg;b.start=pg?6u:3u*h;b.count=1;b.minVertex=0;b.vertexCount=6;
        b.boundsLow=NorthlightGI::Vec3(-100,-100,-100);b.boundsHigh=NorthlightGI::Vec3(100,100,100);if(pg&&h)continue;plan.batches.push_back(b);}
    plan.pages[0].indices.insert(plan.pages[0].indices.end(),{2u,5u,4u});plan.pages[1].indices.insert(plan.pages[1].indices.end(),{0u,1u,3u}); /* page 0's next triangle is not drawn */
    const float m[16]={.02f,0,0,0, 0,.02f,0,0, 0,0,0,0, 0,0,.5f,1};const std::vector<std::uint32_t> list={0,1,2};auto all=[](const P::Batch&){return true;};
    Mask exact(64),page(64);const auto e=draw(plan,list,all,m,Merge::Exact,exact);const auto c=draw(plan,list,all,m,Merge::IgnorePage,page);
    assert(e.runs==2&&e.accepted==3&&c.runs==1&&!(exact==page)); /* the page-crossing counterfactual draws other geometry */
    std::puts("PASS mask runs (synthetic): contiguous same-page batches merge, a page boundary ends a run (counterfactual differs)");
}
// Real world-cache terrain pages (Stormwind, Elwynn, Stranglethorn): per-batch submission against merged
// runs for random mask frusta around the fixture: identical mask bits and triangle multisets; merging
// across index gaps differs. Prints the merge ratio (accepted batches per DIP).
static void realPages(const std::string& root){
    using namespace NorthlightGI;std::string error;
    struct Fixture{const char* name;Vec3 center;};std::mt19937 random(175);
    std::uint64_t accepted=0,runs=0;unsigned frusta=0,gapDiffers=0,pageDiffers=0;
    for(const auto& fixture:{Fixture{"Stormwind",{-8833,628,97}},Fixture{"Elwynn",{-9450,-200,65}},Fixture{"Stranglethorn",{-12400,180,30}}}){
        const auto c=fixture.center;
        int tx=int(std::floor((17066.6666667-c.y)/533.3333333)),ty=int(std::floor((17066.6666667-c.x)/533.3333333));
        std::vector<std::string> nearFiles,farFiles;
        for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){auto p=root+"/Azeroth/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
            if(!std::filesystem::exists(p))continue;farFiles.push_back(p);if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)nearFiles.push_back(p);}
        if(nearFiles.empty()){std::printf("  %s: no world-cache tiles\n",fixture.name);continue;}
        WorldScene local,far;
        assert(loadInstancedScenes(nearFiles,root+"/models",c-Vec3(288,288,320),c+Vec3(288,288,320),local,error));
        const float radius=NorthlightShadowTerrain::Radius;
        assert(loadInstancedScenes(farFiles,root+"/models",c-Vec3(radius,radius,radius),c+Vec3(radius,radius,radius),far,error,1));
        NorthlightWorldMesh::WorldMeshUploadPlan input;assert(NorthlightShadowTerrain::build(local,far,c,input,error));
        P::Plan plan;assert(P::build(input,plan,error));
        std::vector<std::uint32_t> list;for(std::uint32_t i=0;i<plan.batches.size();++i)if(plan.batches[i].terrain)list.push_back(i);
        std::vector<std::uint32_t> everything(plan.batches.size());for(std::uint32_t i=0;i<everything.size();++i)everything[i]=i;
        std::uniform_real_distribution<float> azimuth(0,6.2831853f),elevation(.004f,.35f),offset(-40,40),halo(0,1);
        std::uint64_t fixtureAccepted=0,fixtureRuns=0;
        for(unsigned trial=0;trial<24;++trial){
            const float a=azimuth(random),e=elevation(random);float direction[3]={std::cos(e)*std::cos(a),std::cos(e)*std::sin(a),std::sin(e)};const float color[]={1,1,1};
            NorthlightCelestialDisc::Disc disc;assert(NorthlightCelestialDisc::prepare(direction,color,1,.02f,disc));
            const float camera[]={c.x+offset(random),c.y+offset(random),c.z+5+std::fabs(offset(random))*.2f};float m[16];
            NorthlightCelestialTerrain::matrix(disc,camera,halo(random)<.5f?2.5f:10.f,512,m); /* the moon's and the sun's mask extent (celestial_glow.h) */const NorthlightCelestialTerrain::Frustum f(m);
            auto accept=[&](const P::Batch& b){return b.terrain&&!f.reject(b.boundsLow,b.boundsHigh);};
            Mask reference(512),merged(512),fromAll(512);unsigned perBatchAccepted=0;
            perBatch(plan,list,f,m,reference,perBatchAccepted);
            const auto r=draw(plan,list,accept,m,Merge::Exact,merged);
            const auto everyBatch=draw(plan,everything,accept,m,Merge::Exact,fromAll); /* the fallback's batch set: the same runs */
            assert(merged==reference&&fromAll==reference&&r.accepted==perBatchAccepted&&r.runs<=r.accepted&&everyBatch.runs==r.runs);
            Mask gapMerged(512),pageMerged(512);draw(plan,list,accept,m,Merge::IgnoreGap,gapMerged);draw(plan,list,accept,m,Merge::IgnorePage,pageMerged);
            gapDiffers+=!(gapMerged==reference);pageDiffers+=!(pageMerged==reference);
            fixtureAccepted+=r.accepted;fixtureRuns+=r.runs;++frusta;}
        accepted+=fixtureAccepted;runs+=fixtureRuns;
        std::printf("  %s: %zu batches (%zu terrain) on %zu pages; 24 frusta: accepted %llu, DIPs %llu (%.1f batches per DIP)\n",fixture.name,plan.batches.size(),list.size(),plan.pages.size(),
            (unsigned long long)fixtureAccepted,(unsigned long long)fixtureRuns,fixtureRuns?double(fixtureAccepted)/double(fixtureRuns):0.);}
    assert(frusta>0&&gapDiffers>0); /* a page change almost never meets index contiguity on real pages: the synthetic case covers it */
    std::printf("PASS mask runs (real pages): %u random frusta, merged == per-batch mask bits and triangle multisets, DIP ranges cover every index; merge ratio %.1f accepted batches per DIP (%llu -> %llu); counterfactuals differ (merging across gaps: %u frusta; across pages with contiguous indices: %u)\n",
        frusta,runs?double(accepted)/double(runs):0.,(unsigned long long)accepted,(unsigned long long)runs,gapDiffers,pageDiffers);
}

// rayShiftTexels: the reuse metric of prepareTerrain. It ignores camera translation by design (prepareTerrain
// compares the eye bitwise and separately), so only the body direction is varied here.
static void reuseShift(){
    using namespace NorthlightCelestialTerrain;
    const float camera[]={-9000,-3500,40},tint[]={1,1,1};
    struct Body {float extent;unsigned size;float angular;} bodies[]={{10.f,2048,std::atan(.506736f/24)},{2.5f,1024,std::atan(.46176318f*1.75f/24)}};
    auto make=[&](float elevation,float azimuth,float angular,NorthlightCelestialDisc::Disc& disc){
        const float e=elevation*3.14159265f/180,a=azimuth*3.14159265f/180;
        const float d[]={std::cos(e)*std::cos(a),std::cos(e)*std::sin(a),std::sin(e)};
        assert(NorthlightCelestialDisc::prepare(d,tint,1,angular,disc));
    };
    for(const auto& b:bodies)for(float elevation:{.5f,2.f,60.f}){
        NorthlightCelestialDisc::Disc base;make(elevation,40,b.angular,base);
        float m0[16];matrix(base,camera,b.extent,b.size,m0);
        assert(rayShiftTexels(m0,m0,base,b.extent,b.size)==0); // identical matrices: the old memcmp fast path
        const double texel=2.*base.tangentRadius*b.extent/b.size; // radians per mask texel at the centre
        for(float k:{.1f,1.f,4.f})for(int axis=0;axis<3;++axis){ // drift along right, along up, and diagonally
            // Rotate the whole basis rigidly: prepare()'s own gauge may roll right/up by a few micro-radians between
            // neighbouring directions, which the corner probes would (correctly) count as extra shift.
            double axisv[3]={},len=0;for(int i=0;i<3;++i){axisv[i]=(axis==1?0:base.up[i])-(axis==0?0:base.right[i]);len+=axisv[i]*axisv[i];}
            for(double& v:axisv)v/=std::sqrt(len);
            const double angle=k*texel*(axis==2?std::sqrt(2.):1.),c=std::cos(angle),sn=std::sin(angle);
            NorthlightCelestialDisc::Disc moved=base;
            for(float* vec:{moved.direction,moved.right,moved.up}){
                const double v[3]={vec[0],vec[1],vec[2]},dot=axisv[0]*v[0]+axisv[1]*v[1]+axisv[2]*v[2];
                const double cr[3]={axisv[1]*v[2]-axisv[2]*v[1],axisv[2]*v[0]-axisv[0]*v[2],axisv[0]*v[1]-axisv[1]*v[0]};
                for(int i=0;i<3;++i)vec[i]=float(v[i]*c+cr[i]*sn+axisv[i]*dot*(1-c));
            }
            float m1[16];matrix(moved,camera,b.extent,b.size,m1);
            const float shift=rayShiftTexels(m0,m1,moved,b.extent,b.size);
            std::printf("shift body=%u elev=%.1f axis=%d k=%.1f -> %.3f texels\n",b.size,elevation,axis,k,shift);
            if(axis<2)assert(std::fabs(shift-k)<=.1f*k);else assert(shift<=1.1f*k&&shift>=.7f*k); // max(|du|,|dv|) of a diagonal step
            if(k<=.1f)assert(shift<=ReuseTexels);
            if(k>=1.f)assert(shift>ReuseTexels);
        }
        float bad[16];std::copy(m0,m0+16,bad);bad[5]=std::numeric_limits<float>::quiet_NaN();
        assert(std::isinf(rayShiftTexels(m0,bad,base,b.extent,b.size)));
        std::copy(m0,m0+16,bad);for(unsigned k=0;k<3;++k)bad[k*4+3]=-m0[k*4+3]; // w<=0: the body is behind the mask
        assert(std::isinf(rayShiftTexels(m0,bad,base,b.extent,b.size)));
    }
}
