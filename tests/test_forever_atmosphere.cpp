#include "forever_atmosphere.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightForever;
int main(int argc,char** argv){
    Table t;Key a,b;a.param=b.param=7;a.time=120;b.time=2760;
    a.layer[0].p[1]=1;b.layer[0].p[1]=3;a.layer[0].flag[3]=b.layer[0].flag[3]=1;t.keys={a,b};
    assert(std::fabs(t.at(7,0).layer[0].p[1]-2)<1e-6);assert(!t.at(8,0).valid);
    t.skies.resize(2);t.skies[0].color[0][0]=.2f;t.skies[1].color[0][0]=.8f;
    assert(t.at(7,0).skyValid&&std::fabs(t.at(7,0).sky.color[0][0]-.5f)<1e-6);
    Frame absent,half;absent.valid=true;auto full=t.at(7,120);add(half,absent,.5f);add(half,full,.5f);
    assert(std::fabs(sigma(half,100,0,0)-.005f)<1e-7);
    auto f=t.at(7,120);assert(std::fabs(sigma(f,100,0,0)-.01f)<1e-7);
    f.layer[0].p[3]=1;assert(std::fabs(sigma(f,100,100,0)-.01f/std::exp(1.f))<1e-7);
    f.layer[0].flag[1]=1;assert(std::fabs(sigma(f,100,150,50)-.01f/std::exp(1.f))<1e-7);
    f.layer[0].p[0]=101;assert(sigma(f,100,0,0)==0);
    Light l;l.params[0]=l.params[2]=7;t.lights.push_back(l);float p[3]={};
    assert(t.sample("Azeroth",p,0,0).valid);assert(!t.sample("Northrend",p,0,0).valid);
    assert(!t.sample("Azeroth",p,-1,0).valid);assert(!t.sample("Azeroth",p,std::numeric_limits<float>::quiet_NaN(),0).valid);
    assert(!t.load("missing-forever-atmosphere.bin"));assert(t.keys.size()==2);
    if(argc==2){
        assert(t.load(argv[1]));assert(t.lights.size()==216&&t.keys.size()==2374&&t.zones.size()==17);
        const auto hills=t.at(7245,2184);assert(hills.valid);
        // Exact local DB2 keys 70349 (18:00) and 70350 (19:30), not an artistic density gain.
        assert(std::fabs(hills.layer[2].p[1]-(.14f+(.15f-.14f)*24/180))<1e-6);
        assert(std::fabs(hills.layer[0].p[10]-(.9f+(.6f-.9f)*24/180))<1e-6);
        const auto tirisfal=t.at(7858,2168);assert(tirisfal.valid);
        assert(std::fabs(tirisfal.layer[2].p[1]-(.65f+(.6f-.65f)*8/180))<1e-6);
        for(const auto& k:t.keys){auto v=t.at(k.param,float(k.time));assert(v.valid);
            for(unsigned i=0;i<3;++i)assert(std::fabs(v.layer[i].p[1]-(k.layer[i].flag[3]>0?k.layer[i].p[1]:0))<1e-6);}
        float pos[3]={2274,240,45};auto v=t.sample("Azeroth",pos,1084.f/1440,0);assert(v.valid);
        std::printf("Tirisfal camera density %.8f layers %.4f %.4f %.4f\n",sigma(v,100,45,43),v.layer[0].p[1],v.layer[1].p[1],v.layer[2].p[1]);
        // All weather endpoints and intermediate times must stay finite.
        for(const auto& light:t.lights)for(float time:{0.f,.25f,.5f,.7555556f,.9999f})for(float storm:{0.f,.5f,1.f}){
            auto v=t.condition(light,time*2880,storm);float c[38][4];constants(v,c,1277,1);
            for(const auto& row:c)for(float value:row)assert(std::isfinite(value));
            for(float height:{-100.f,0.f,45.f,500.f})assert(std::isfinite(sigma(v,100,height,40)));}
    }
    std::puts("PASS Forever import, DB2 key fidelity, cyclic time, weather blending, height and extinction");
}
