// Offline visual fixture: production BVH, diffuse probe transport and SH evaluation.
#include "world_gi.h"
#include "probe_local_visibility.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace NorthlightGI;
constexpr float Pi=3.14159265359f,Spacing=8;
constexpr int X0=-8,X1=8,Y0=-8,Y1=10,Z0=0,Z1=5;
constexpr unsigned W=960,H=640;
unsigned Rays=128;
struct Fixture {
    WorldScene scene;
    std::vector<Vec3> emission;
    uint32_t material(Vec3 color,unsigned pattern=0,Vec3 glow={}){
        WorldMaterial m;m.albedo=color;m.alphaCutoff=0;m.width=m.height=64;m.rgba.resize(64*64*4);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
            const unsigned noise=(x*92837111u+y*689287499u)^(x*y*283923481u);
            float v=.92f+.08f*float(noise%251)/250;
            if(pattern==1){const unsigned shift=(y/16)%2?16:0;if(y%16<2||(x+shift)%32<2)v=.57f;}
            if(pattern==2)v*=.87f+.13f*std::sin(x*.7f+std::sin(y*.17f));
            if(pattern==3&&((x/16+y/16)%2))v*=.8f;
            const auto i=(y*64+x)*4;m.rgba[i]=m.rgba[i+1]=m.rgba[i+2]=uint8_t(v*255);m.rgba[i+3]=255;
        }
        scene.materials.push_back(std::move(m));emission.push_back(glow);return uint32_t(emission.size()-1);
    }
    void quad(Vec3 a,Vec3 b,Vec3 c,Vec3 d,uint32_t material){
        const auto n=normalized(cross(b-a,c-a));const uint32_t first=uint32_t(scene.vertices.size());
        for(auto p:{a,b,c,d}){
            const float u=(std::fabs(n.x)>.5f?p.y:p.x)*.18f;
            const float v=(std::fabs(n.z)>.5f?p.y:p.z)*.18f;
            scene.vertices.push_back({p,n,u,v});
        }
        scene.triangles.push_back({first,first+1,first+2,material});scene.triangles.push_back({first,first+2,first+3,material});
    }
    void triangle(Vec3 a,Vec3 b,Vec3 c,uint32_t m){
        const auto n=normalized(cross(b-a,c-a));const uint32_t first=uint32_t(scene.vertices.size());
        for(auto p:{a,b,c})scene.vertices.push_back({p,n,p.x*.18f,p.z*.18f});
        scene.triangles.push_back({first,first+1,first+2,m});
    }
    void box(Vec3 a,Vec3 b,uint32_t m){
        quad({a.x,a.y,a.z},{a.x,a.y,b.z},{a.x,b.y,b.z},{a.x,b.y,a.z},m);
        quad({b.x,a.y,a.z},{b.x,b.y,a.z},{b.x,b.y,b.z},{b.x,a.y,b.z},m);
        quad({a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,a.y,b.z},{a.x,a.y,b.z},m);
        quad({a.x,b.y,a.z},{a.x,b.y,b.z},{b.x,b.y,b.z},{b.x,b.y,a.z},m);
        quad({a.x,a.y,a.z},{a.x,b.y,a.z},{b.x,b.y,a.z},{b.x,a.y,a.z},m);
        quad({a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z},m);
    }
    void build(){
        const auto plaster=material({.72f,.64f,.49f}),wood=material({.28f,.13f,.055f},2),floor=material({.43f,.26f,.12f},2);
        const auto stone=material({.48f,.47f,.43f},1),roof=material({.38f,.09f,.045f},1),grass=material({.19f,.30f,.10f});
        const auto blue=material({.07f,.19f,.29f},3),red=material({.40f,.065f,.035f},3),rug=material({.35f,.12f,.055f},3);
        const auto bulb=material({.8f,.45f,.12f},0,{3,1.3f,.28f});
        quad({-180,-180,-1},{180,-180,-1},{180,180,-1},{-180,180,-1},grass);
        box({-5,-100,-.99f},{5,3,-.86f},stone);
        box({-15.8f,1.2f,-1},{15.8f,34.8f,-.3f},stone);
        box({-15,2,-.3f},{15,34,0},floor);
        box({-15,2,0},{-14.3f,34,13},plaster);box({14.3f,2,0},{15,34,13},plaster);
        box({-15,33.3f,0},{15,34,13},plaster);
        box({-15,2,0},{-4,2.7f,13},plaster);box({4,2,0},{15,2.7f,13},plaster);
        box({-4,2,9.2f},{4,2.7f,13},plaster);box({-15,2,13},{15,34,13.5f},plaster);
        triangle({-15,2,13},{15,2,13},{0,2,18},plaster);
        triangle({-15,34,13},{0,34,18},{15,34,13},plaster);
        // Gabled roof and structural timber give a recognisable closed house.
        quad({-17,0,13.6f},{0,0,19},{0,36,19},{-17,36,13.6f},roof);
        quad({0,0,19},{17,0,13.6f},{17,36,13.6f},{0,36,19},roof);
        for(float x:{-15.f,-4.25f,4.25f,15.f})box({x-.22f,1.65f,-.2f},{x+.22f,2.85f,13.3f},wood);
        box({-15.4f,1.65f,9.1f},{15.4f,2.85f,9.6f},wood);
        box({-15.4f,1.65f,12.8f},{15.4f,2.85f,13.4f},wood);
        for(float y:{4.f,18.f,32.f})box({-14.4f,y-.3f,11.8f},{14.4f,y+.3f,12.6f},wood);
        box({-3.5f,8,.03f},{7,24,.09f},rug);
        // A table, chairs, shelf and coloured books expose diffuse colour bleed.
        box({-9,18,3.6f},{1,24,4.3f},wood);
        for(float x:{-8.4f,.4f})for(float y:{18.6f,23.4f})box({x-.3f,y-.3f,0},{x+.3f,y+.3f,3.6f},wood);
        for(float x:{-5.f,0.f}){box({x,14,2.1f},{x+2.5f,16.5f,2.6f},wood);box({x,13.8f,2.1f},{x+2.5f,14.2f,5.6f},wood);
            for(float dx:{.25f,2.25f})for(float y:{14.25f,16.25f})box({x+dx-.15f,y-.15f,0},{x+dx+.15f,y+.15f,2.1f},wood);}
        box({8,30,0},{13,33.1f,9},wood);
        for(float z:{1.f,3.6f,6.2f})for(int i=0;i<5;++i)box({8.3f+i*.85f,29.6f,z},{8.9f+i*.85f,30.1f,z+2.1f},i%2?red:blue);
        box({8,11,0},{12,15,2.8f},blue);box({9,12,2.8f},{11,14,3.3f},wood);
        box({-14.2f,20,7.7f},{-13.9f,21.5f,9.5f},bulb);box({5.1f,1.3f,7},{6.3f,1.7f,8.8f},bulb);
        // Low outside walls and blocks cast visible moon/sun shadows through the door.
        box({-35,-16,-1},{-27,-10,4},stone);box({19,-29,-1},{25,-23,3},stone);
        for(float y:{-28.f,-12.f,4.f}){box({-32,y,-1},{-30,y+1,7},wood);box({-37,y-2,6},{-25,y+3,12},grass);}
        // Keep the world-aligned z=0 probe layer above the walkable floor.
        for(auto& vertex:scene.vertices)vertex.position.z-=1;
    }
};
template<class Job> void parallel(unsigned count,Job job){
    std::atomic<unsigned> next{0};std::vector<std::thread> workers;
    for(unsigned i=0;i<std::min(12u,std::max(1u,std::thread::hardware_concurrency()));++i)workers.emplace_back([&]{for(unsigned k;(k=next.fetch_add(1))<count;)job(k);});
    for(auto& worker:workers)worker.join();
}
struct Field {
    static constexpr unsigned Nx=X1-X0+1,Ny=Y1-Y0+1,Nz=Z1-Z0+1;
    std::vector<Probe> probes=std::vector<Probe>(Nx*Ny*Nz);
    std::vector<std::shared_ptr<const ProbeLocalVisibility>> visibility=decltype(visibility)(Nx*Ny*Nz);
    ProbeVisibilityCache geometryCache;
    const BVH* geometry=nullptr;
    void solve(const BVH& bvh,const Lighting& lighting){
        const auto prepared=prepareLighting(lighting);assert(prepared.valid());
        if(geometry!=&bvh){geometryCache.reset();geometry=&bvh;std::fill(visibility.begin(),visibility.end(),nullptr);}
        parallel(unsigned(probes.size()),[&](unsigned i){const int x=int(i%Nx)+X0,y=int(i/Nx%Ny)+Y0,z=int(i/(Nx*Ny))+Z0;
            const Vec3 position={x*Spacing,y*Spacing,z*Spacing};
            probes[i]=solveProbePrepared(bvh,position,prepared,Rays,0x7639u+i*171u);
            if(probes[i].valid&&!visibility[i])visibility[i]=geometryCache.get(bvh,{x,y,z},position);});
    }
    Vec3 sample(Vec3 point,Vec3 normal,float& coverage,bool legacy=false)const{
        coverage=0;
        point=point+normal*.25f;const Vec3 grid=point/Spacing;
        const int x=int(std::floor(grid.x)),y=int(std::floor(grid.y)),z=int(std::floor(grid.z));
        const Vec3 f=grid-Vec3(float(x),float(y),float(z));Vec3 sum;float total=0;
        for(unsigned i=0;i<8;++i){const int dx=i&1,dy=i>>1&1,dz=i>>2;const int xx=x+dx,yy=y+dy,zz=z+dz;
            if(xx<X0||xx>X1||yy<Y0||yy>Y1||zz<Z0||zz>Z1)continue;
            const size_t index=(zz-Z0)*Nx*Ny+(yy-Y0)*Nx+xx-X0;
            const auto& p=probes[index];if(!p.valid)continue;
            float weight=(dx?f.x:1-f.x)*(dy?f.y:1-f.y)*(dz?f.z:1-f.z);
            coverage+=weight;
            const Vec3 delta=point-p.position;const float distance=std::sqrt(dot(delta,delta));
            if(legacy){
                const Vec3 a={std::fabs(delta.x),std::fabs(delta.y),std::fabs(delta.z)};
                const unsigned axis=a.x>=a.y&&a.x>=a.z?(delta.x>=0?0:1):a.y>=a.z?(delta.y>=0?2:3):(delta.z>=0?4:5);
                const auto moment=p.moments[axis];const float variance=std::max(moment.meanSquare-moment.mean*moment.mean,.02f),excess=std::max(distance-moment.mean,0.f);
                const float visible=variance/(variance+excess*excess);weight*=visible*visible;
            }else weight*=LocalProbeVisibility::visibility(visibility[index],delta)*LocalProbeVisibility::facing(normal,delta);
            sum=sum+evaluateIrradiance(p,normal)*weight;total+=weight;
        }
        coverage=std::clamp(coverage,0.f,1.f);return sum/std::max(total,.00001f);
    }
};
void benchmark(const BVH& bvh,const std::filesystem::path& output){
    using Clock=std::chrono::steady_clock;
    const auto elapsed=[](auto start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
    ProbeVisibilityCache cache;std::vector<std::shared_ptr<const ProbeLocalVisibility>> maps(Field::Nx*Field::Ny*Field::Nz);
    Lighting light;light.maxBounces=3;light.maxDistance=256;light.sunDirection=normalized({-.55f,-.68f,.58f});
    light.sunIrradiance={5.4f,4.8f,3.9f};light.skyRadiance={.13f,.19f,.3f};const auto prepared=prepareLighting(light);
    std::vector<Probe> probes(maps.size());std::ofstream log(output/"cpu-timing.json");
    log<<"{\"scope\":\"Offline CPU batches, 12 threads maximum; not game frame time\",\"probes\":"<<maps.size()<<",\"rays\":"<<Rays<<",\"samples\":[";
    for(unsigned iteration=0;iteration<8;++iteration){cache.reset();
        const auto solve=[&](unsigned i,bool colour){const int x=int(i%Field::Nx)+X0,y=int(i/Field::Nx%Field::Ny)+Y0,z=int(i/(Field::Nx*Field::Ny))+Z0;
            const Vec3 p={x*Spacing,y*Spacing,z*Spacing};if(colour)probes[i]=solveProbePrepared(bvh,p,prepared,Rays,0x7639u+i*171u);
            else maps[i]=cache.get(bvh,{x,y,z},p);};
        auto start=Clock::now();parallel(unsigned(maps.size()),[&](unsigned i){solve(i,false);});const double cold=elapsed(start);
        const auto before=cache.stats();start=Clock::now();parallel(unsigned(maps.size()),[&](unsigned i){solve(i,false);});const double warm=elapsed(start);
        const auto after=cache.stats();assert(before.built==after.built&&after.reused-before.reused==maps.size());
        start=Clock::now();parallel(unsigned(maps.size()),[&](unsigned i){solve(i,true);});const double colour=elapsed(start);
        if(iteration)log<<",";
        log<<"{\"cold_visibility_ms\":"<<cold<<",\"warm_lookup_ms\":"<<warm<<",\"colour_solve_ms\":"<<colour<<"}";
    }
    log<<"],\"visibility_payload_bytes\":"<<maps.size()*sizeof(ProbeLocalVisibility)<<",\"rays_per_new_probe\":81,\"rays_per_cached_probe\":0}\n";
}
struct Camera {Vec3 eye,target;float fov;const char* name;};
// Independent receiver-path reference: no SH or probe interpolation.
Vec3 referenceIndirect(const BVH& bvh,Vec3 point,Vec3 normal,const Lighting& light,uint32_t seed){
    auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return float(seed>>8)*(1.f/16777216);};
    auto cosine=[&](Vec3 n){const float r=std::sqrt(random()),phi=2*Pi*random();
        const Vec3 t=normalized(cross(std::fabs(n.z)>.95f?Vec3(0,1,0):Vec3(0,0,1),n));
        return t*(r*std::cos(phi))+cross(n,t)*(r*std::sin(phi))+n*std::sqrt(std::max(0.f,1-r*r));};
    Vec3 result;
    for(unsigned sample=0;sample<64;++sample){Vec3 origin=point,direction=cosine(normal),throughput(1,1,1),radiance;
        for(unsigned bounce=0;bounce<=light.maxBounces;++bounce){
            const auto hit=bvh.trace(origin,direction,.0005f,light.maxDistance);
            if(!hit.hit){if(direction.z>0)radiance=radiance+throughput*light.skyRadiance;break;}
            if(bounce==light.maxBounces)break;
            const Vec3 offset=hit.position+hit.geometricNormal*light.rayBias;Vec3 incoming;
            auto illuminate=[&](Vec3 toward,Vec3 energy,float distance){const float c=std::max(0.f,dot(hit.normal,toward));
                if(c>0&&dot(hit.geometricNormal,toward)>0&&!bvh.occluded(offset,toward,distance,.0005f))incoming=incoming+energy*(c/Pi);};
            illuminate(light.sunDirection,light.sunIrradiance,light.maxDistance);
            for(const auto& source:light.points){const Vec3 delta=source.position-offset;const float distance=std::sqrt(dot(delta,delta));
                if(distance<=.0001f||distance>=source.attenuationEnd)continue;
                const float attenuation=std::clamp((source.attenuationEnd-distance)/(source.attenuationEnd-source.attenuationStart),0.f,1.f);
                illuminate(delta/distance,source.irradiance*attenuation,std::max(.0006f,distance-light.rayBias));}
            radiance=radiance+throughput*hit.albedo*incoming;throughput=throughput*hit.albedo;
            direction=cosine(hit.normal);if(dot(direction,hit.geometricNormal)<=0)break;origin=offset;
        }
        result=result+radiance;
    }
    return result/64;
}
void write(const std::filesystem::path& path,const std::vector<Vec3>& pixels){
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()*sizeof(Vec3)));assert(file.good());
}
int main(int argc,char** argv){
    if(argc<2)return 2;bool reference=false,referenceDay=false,timing=false;
    for(int i=2;i<argc;++i){const std::string arg=argv[i];
        if(arg=="--reference"||arg=="--reference-day"){reference=true;referenceDay=arg=="--reference-day";}
        else if(arg=="--benchmark")timing=true;
        else if(arg=="--rays"&&i+1<argc)Rays=unsigned(std::stoul(argv[++i]));else return 2;}
    if(Rays<8||Rays>4096)return 2;
    const std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
    Fixture fixture;fixture.build();BVH bvh;std::string error;
    if(!bvh.build(std::move(fixture.scene),error)||bvh.triangleCount()!=788){std::fprintf(stderr,"Invalid fixture BVH: %s\n",error.c_str());return 1;}
    // The offline field spans 19 cells horizontally; avoid wrapping collisions.
    if(!configureProbeLayout(probeLayoutFor(20)))return 1;
    if(timing){benchmark(bvh,output);return 0;}
    const Camera cameras[]={{{28,-43,16},{0,9,5},53,"outside"},{{-10,11,5.4f},{5,24,3.7f},75,"inside"},{{5,22,5.4f},{0,-18,4},69,"doorway"}};
    Field field;
    for(unsigned night=0;night<2;++night){
        if(reference&&night!=unsigned(!referenceDay))continue;
        Lighting light;light.maxBounces=3;light.maxDistance=256;light.sunDirection=normalized(night?Vec3(.35f,-.65f,.68f):Vec3(-.55f,-.68f,.58f));
        light.sunIrradiance=night?Vec3(.045f,.065f,.12f):Vec3(5.4f,4.8f,3.9f);
        light.skyRadiance=night?Vec3(.0015f,.0028f,.007f):Vec3(.13f,.19f,.30f);
        if(night){light.points.push_back({{-13.3f,20.8f,7.6f},{5.5f,2.5f,.75f},2,34});light.points.push_back({{5.7f,.6f,6.9f},{3.2f,1.5f,.45f},2,25});}
        field.solve(bvh,light);unsigned valid=0;for(const auto& p:field.probes)valid+=p.valid;
        std::printf("HOUSE_GI phase=%s probes=%zu valid=%u rays=%u bounces=%u spacing=%g triangles=%zu\n",night?"night":"day",field.probes.size(),valid,Rays,light.maxBounces,Spacing,bvh.triangleCount());std::fflush(stdout);
        std::ofstream metrics(output/(std::string(night?"night":"day")+"-irradiance.json"));metrics<<"[\n";
        const Vec3 samples[]={{10,-10,-1.98f},{6,9,-.88f},{0,26,-.98f},{-4,21,-.98f}};
        const char* names[]={"outside","sun_patch","back_room","under_table"};
        for(unsigned i=0;i<4;++i){float coverage=0;const Vec3 e=field.sample(samples[i],{0,0,1},coverage);
            const bool blocked=bvh.occluded(samples[i]+Vec3(0,0,.02f),light.sunDirection,256);
            assert(std::isfinite(e.x)&&std::isfinite(e.y)&&std::isfinite(e.z)&&e.x>=0&&e.y>=0&&e.z>=0);
            if(!night&&i<2)assert(!blocked);
            if(!night&&i==2)assert(blocked&&dot(e,e)>1e-9f);
            metrics<<(i?",\n":"")<<"{\"point\":\""<<names[i]<<"\",\"indirect_irradiance\":["<<e.x<<","<<e.y<<","<<e.z<<"],\"probe_coverage\":"<<coverage<<",\"directional_blocked\":"<<(blocked?"true":"false")<<"}";
        }metrics<<"\n]\n";
        for(const auto& camera:cameras){
            if(reference&&std::string(camera.name)=="outside")continue;
            const Vec3 forward=normalized(camera.target-camera.eye),right=normalized(cross(forward,{0,0,1})),up=cross(right,forward);
            const float scale=std::tan(camera.fov*Pi/360),aspect=float(W)/H;
            std::vector<Vec3> combined(W*H),without(W*H),indirect(W*H),legacyImage(W*H),probeImage(reference?W*H:0);
            parallel(H,[&](unsigned y){for(unsigned x=0;x<W;++x){Vec3 on,off,bounce,probed,legacy;
                for(unsigned sample=0;sample<4;++sample){const float sx=(sample&1)*.5f+.25f,sy=(sample>>1)*.5f+.25f;
                    const Vec3 ray=normalized(forward+right*((2*(x+sx)/W-1)*scale*aspect)+up*((1-2*(y+sy)/H)*scale));
                    const auto hit=bvh.trace(camera.eye,ray,.001f,500);
                    if(!hit.hit){const float horizon=std::pow(std::max(0.f,1-ray.z),3.f);
                        const Vec3 sky=night?Vec3(.0015f,.0026f,.006f)+Vec3(.001f,.0015f,.003f)*horizon:Vec3(.14f,.29f,.52f)+Vec3(.20f,.17f,.11f)*horizon;
                        on=on+sky;off=off+sky;probed=probed+sky;legacy=legacy+sky;continue;}
                    const Vec3 origin=hit.position+hit.geometricNormal*.012f;Vec3 direct;
                    const float cosine=std::max(0.f,dot(hit.normal,light.sunDirection));
                    if(cosine>0&&!bvh.occluded(origin,light.sunDirection,256))direct=light.sunIrradiance*(cosine/Pi);
                    for(const auto& point:light.points){const Vec3 delta=point.position-origin;const float distance=std::sqrt(dot(delta,delta));
                        const float facing=std::max(0.f,dot(hit.normal,delta/distance));const float attenuation=std::clamp((point.attenuationEnd-distance)/(point.attenuationEnd-point.attenuationStart),0.f,1.f);
                        if(facing>0&&attenuation>0&&!bvh.occluded(origin,delta/distance,distance-.012f))direct=direct+point.irradiance*(facing*attenuation/Pi);}
                    const Vec3 ambient=light.skyRadiance*std::clamp(.5f+.5f*hit.normal.z,0.f,1.f);
                    float coverage=0;const Vec3 measured=field.sample(hit.position,hit.normal,coverage)/Pi;
                    const Vec3 probeGI=measured*coverage+ambient*(1-coverage);
                    float oldCoverage=0;const Vec3 oldMeasured=field.sample(hit.position,hit.normal,oldCoverage,true)/Pi;
                    const Vec3 oldGI=oldMeasured*oldCoverage+ambient*(1-oldCoverage);
                    const Vec3 gi=reference?referenceIndirect(bvh,origin,hit.normal,light,0xa561d823u+(x+y*W)*0x9e3779b9u+sample*7309u):probeGI;
                    const uint32_t material=bvh.scene().triangles[hit.triangle].material;const Vec3 emission=night?fixture.emission[material]:Vec3();
                    on=on+hit.albedo*(direct+gi)+emission;off=off+hit.albedo*(direct+ambient)+emission;bounce=bounce+hit.albedo*gi;
                    probed=probed+hit.albedo*(direct+probeGI)+emission;
                    legacy=legacy+hit.albedo*(direct+oldGI)+emission;
                }
                const auto i=y*W+x;combined[i]=on*.25f;without[i]=off*.25f;indirect[i]=bounce*.25f;
                legacyImage[i]=legacy*.25f;
                if(reference)probeImage[i]=probed*.25f;
            }});
            const std::string prefix=std::string(night?"night-":"day-")+camera.name;
            write(output/(prefix+"-gi.rgb32f"),combined);write(output/(prefix+"-native-ambient.rgb32f"),without);write(output/(prefix+"-indirect.rgb32f"),indirect);
            write(output/(prefix+"-legacy.rgb32f"),legacyImage);
            if(reference)write(output/(prefix+"-probes.rgb32f"),probeImage);
            std::printf("HOUSE_VIEW %s complete\n",prefix.c_str());std::fflush(stdout);
        }
    }
    const auto stats=field.geometryCache.stats();
    std::printf("HOUSE_VISIBILITY maps=%llu reused=%llu failed=%llu bytesPerProbe=%zu\n",(unsigned long long)stats.built,(unsigned long long)stats.reused,(unsigned long long)stats.failed,sizeof(ProbeLocalVisibility));
    std::ofstream meta(output/"render.json");meta<<"{\"width\":"<<W<<",\"height\":"<<H<<",\"rays_per_probe\":"<<Rays<<",\"bounces\":3,\"probe_spacing\":8,\"probes_per_phase\":"<<Field::Nx*Field::Ny*Field::Nz<<",\"samples_per_pixel\":4,\"primary_visibility\":\"production CPU BVH\",\"GI\":\"production solveProbePrepared and evaluateIrradiance, experimental cached FP16 blocker planes, steady-state interpolation\",\"shadows\":\"exact CPU BVH rays, not raster shadow maps\",\"materials\":\"diffuse, no baked WMO illumination\",\"game_capture\":false}\n";
}
