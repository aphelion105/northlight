#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include "draw_gates.h"
using HRESULT=long;
using DWORD=unsigned;
constexpr unsigned D3DRS_ZENABLE=1,D3DRS_ZWRITEENABLE=2;
constexpr HRESULT D3D_OK=0;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
struct Shader {int releases=0,refs=0;void Release(){++releases;}void AddRef(){++refs;}};
using IDirect3DVertexShader9=Shader;
using IDirect3DPixelShader9=Shader;
template<class T> void drop(T*& p){if(p)p->Release();p=nullptr;}
struct CpuScope {explicit CpuScope(int){}};
struct SwapCalls {unsigned getRequests=0,getNative=0,setVS=0,setPS=0,setPSConstants=0,constantRegisters=0,failures=0;};
struct DeviceState{
    Shader* vertex=nullptr;Shader* pixel=nullptr;std::array<float,896> constants{};
    unsigned calls=0,failAt=0;bool cachedGet=false;std::uint64_t* forwarded=nullptr;std::vector<unsigned> trace;
    bool fail(unsigned api){trace.push_back(api);return ++calls==failAt;}
    HRESULT GetPixelShaderConstantF(unsigned base,float* out,unsigned count){if(!cachedGet)++*forwarded;if(fail(1))return -1;std::memcpy(out,constants.data()+base*4,count*16);return 0;}
    HRESULT SetPixelShaderConstantF(unsigned base,const float* in,unsigned count){if(fail(4))return -1;std::memcpy(constants.data()+base*4,in,count*16);return 0;}
    HRESULT SetVertexShader(Shader* shader){if(fail(2))return -1;vertex=shader;return 0;}
    HRESULT SetPixelShader(Shader* shader){if(fail(3))return -1;pixel=shader;return 0;}
};
struct Hook{
    DeviceState* ext=nullptr;
    bool gateFrame=false;struct {std::uint64_t forwarded=0;} mirrorState;SwapCalls terrainSwapCalls;
    Shader *shadowSwapOriginal=nullptr,*shadowSwapReplacement=nullptr,*shadowSwapVertexOriginal=nullptr,*shadowSwapVertexReplacement=nullptr;
    unsigned shadowSwapConstantBase=0,shadowSwapConstantCount=0,terrainShadowDraws=0;
    float shadowSwapConstants[24]={};
    int sampledHookTimer(){return 0;}
    template<class Draw> HRESULT materialNativeDraw(Draw draw,Shader* =nullptr,Shader* =nullptr){return draw();}
    // Exact current production method, extracted by the Python driver.
#include "terrain_shadow_draw.inc"
};
struct PlanWorld {
    bool native=true,active=false,coverage=true,patch=true;unsigned coverageCalls=0,patchCalls=0;
    Shader pixel,vertex;
    bool nativeTerrainShadowsEnabled()const{return native;}
    bool terrainShadowActive()const{return active;}
    bool terrainShadowCoverage(Shader*,Shader*,Shader*& v,Shader*& p,unsigned& base,unsigned& count,float*){
        ++coverageCalls;if(!coverage)return false;v=&vertex;p=&pixel;base=221;count=3;return true;
    }
    Shader* terrainShadowReplacement(Shader*){++patchCalls;return patch?&pixel:nullptr;}
};
struct PlanDevice {
    Shader pixel,vertex;unsigned reads=0,vsReads=0;bool fail=false,cached=true,vertexPresent=true,failVS=false;
    HRESULT GetPixelShader(Shader** p){++reads;if(fail)return -1;*p=&pixel;pixel.AddRef();return 0;}
    bool peekVertexShader(Shader*& p){p=cached&&vertexPresent?&vertex:nullptr;return cached;}
    HRESULT GetVertexShader(Shader** p){++vsReads;if(failVS)return -1;*p=vertexPresent?&vertex:nullptr;if(*p)(*p)->AddRef();return 0;}
};
struct PlanHook {
    PlanWorld* world=nullptr;PlanDevice* ext=nullptr;NorthlightDrawGates::Frame drawGates;
    bool applied=false,enabled=true,failed=false,terrain=true;unsigned debugMode=0,worldDebug=0;
    unsigned tag=1;static constexpr unsigned kTagMask=255;
    struct Class {unsigned entry;bool world=false,skinned=false;};using VsClass=Class;
    Class classifyVs(Shader* p)const{return {p?tag:0,p&&tag==1,false};}
    static constexpr unsigned kWaterTag=256;
    bool diagnostics=false;unsigned postEffectDepthWrites=0,postEffectDepthStateFailures=0,postEffectWorldDraws=0,postEffectSkinnedDraws=0,waterAfterEffects=0,observations=0;
    bool sampled()const{return diagnostics;}
    bool readDrawRenderState(unsigned,DWORD& value){value=1;return true;}
    void observeGrassReceiver(Shader*,bool){++observations;}
    Shader *shadowSwapOriginal=nullptr,*shadowSwapReplacement=nullptr,*shadowSwapVertexOriginal=nullptr,*shadowSwapVertexReplacement=nullptr;
    unsigned shadowSwapConstantBase=0,shadowSwapConstantCount=0;float shadowSwapConstants[24]={};
#include "terrain_shadow_plan.inc"
    void lateDraw(){if(failed||!enabled)return;
#include "terrain_shadow_late.inc"
    }
};
static void visibilityPlanning(){
    unsigned cases=0;
    for(unsigned flags=0;flags<128;++flags)for(bool native:{false,true})for(bool requested:{false,true})
    for(bool active:{false,true})for(bool coverage:{false,true})for(bool patch:{false,true})for(bool gates:{false,true}){
        PlanWorld w;w.native=native;w.active=requested&&active;w.coverage=coverage;w.patch=patch;
        PlanDevice device;PlanHook h;Shader vertex;h.world=flags&1?nullptr:&w;h.ext=&device;
        h.enabled=!(flags&2);h.failed=flags&4;h.applied=flags&8;h.terrain=!(flags&16);h.debugMode=flags&32?1:0;h.tag=flags&64?2:1;
        NorthlightDrawGates::Inputs inputs;inputs.world=h.world;inputs.enabled=h.enabled;inputs.failed=h.failed;inputs.debugOff=!h.debugMode;inputs.worldShadows=requested;inputs.hideTerrainShadows=!native;
        h.drawGates=NorthlightDrawGates::latch(gates,inputs);h.planTerrainShadowSwap(&vertex);
        const bool admitted=h.world&&h.enabled&&!h.failed&&h.terrain&&!h.debugMode&&h.tag==1&&(!native||(!h.applied&&w.active));
        const bool expected=admitted&&((native&&coverage)||patch);
        assert(device.reads==unsigned(admitted));assert(bool(h.shadowSwapReplacement)==expected);
        assert(w.coverageCalls==unsigned(admitted&&native));assert(w.patchCalls==unsigned(admitted&&(!native||!coverage)));
        assert(bool(h.shadowSwapVertexReplacement)==(expected&&native&&coverage));
        assert(device.pixel.releases==unsigned(admitted&&!expected));
        if(h.shadowSwapOriginal)h.shadowSwapOriginal->Release();if(h.shadowSwapVertexOriginal)h.shadowSwapVertexOriginal->Release();
        assert(device.pixel.releases==unsigned(admitted)&&vertex.refs==vertex.releases);++cases;
    }
    std::printf("TERRAIN_SHADOW_VISIBILITY_PASS cases=%u off_without_replacement=1 default_coverage=1 frame_gates=0/1\n",cases);
}
static void lateDrawPlanning(){
    unsigned cases=0;
    for(unsigned flags=0;flags<64;++flags)for(bool native:{false,true})for(bool sample:{false,true})for(bool cached:{false,true})
    for(bool present:{false,true})for(bool failVS:{false,true})for(bool failPS:{false,true})for(unsigned tag:{0u,1u,2u}){
        PlanWorld w;w.native=native;w.active=true;PlanDevice d;d.cached=cached;d.vertexPresent=present;d.failVS=failVS;d.fail=failPS;
        PlanHook h;h.world=flags&1?nullptr:&w;h.ext=&d;h.applied=!(flags&2);h.enabled=!(flags&4);h.failed=flags&8;h.terrain=!(flags&16);h.debugMode=flags&32;h.tag=tag;h.diagnostics=sample;
        h.lateDraw();
        const bool entered=h.applied&&h.enabled&&!h.failed;
        const bool hide=h.world&&h.terrain&&!h.debugMode&&!native;
        const bool query=entered&&(sample||hide);
        const bool vertex=query&&present&&(cached||!failVS);
        const bool planned=vertex&&hide&&tag==1;
        assert(d.vsReads==unsigned(query&&!cached));assert(d.reads==unsigned(planned));
        assert(bool(h.shadowSwapReplacement)==(planned&&!failPS));
        assert(!w.coverageCalls); // Automatic replacement never follows the composite.
        if(!sample)assert(!h.observations&&!h.postEffectDepthWrites);
        if(h.shadowSwapOriginal)h.shadowSwapOriginal->Release();if(h.shadowSwapVertexOriginal)h.shadowSwapVertexOriginal->Release();
        assert(d.vertex.refs==d.vertex.releases&&d.pixel.refs==d.pixel.releases);++cases;
    }
    std::printf("LATE_TERRAIN_SHADOW_PASS cases=%u explicitSuppression=late unknownUi=unchanged automaticCoverage=preComposite getters=bounded references=balanced diagnostics=optional\n",cases);
}
void checkCalls(const Hook& hook,const DeviceState& state,unsigned count,const std::vector<unsigned>& expected){
    assert(state.trace==expected&&state.calls==expected.size());
    const auto& c=hook.terrainSwapCalls;
    auto attempts=[&](unsigned api){return hook.gateFrame?unsigned(std::count(expected.begin(),expected.end(),api)):0u;};
    assert(c.getRequests==attempts(1)&&c.getNative==(state.cachedGet?0:attempts(1)));
    assert(c.setVS==attempts(2)&&c.setPS==attempts(3)&&c.setPSConstants==attempts(4));
    assert(c.constantRegisters==attempts(4)*count);
    assert(c.failures==unsigned(hook.gateFrame&&state.failAt&&state.failAt<=state.calls));
}
int main(){
    lateDrawPlanning();
    visibilityPlanning();
    unsigned cases=0;
    for(unsigned count:{3u,6u})for(unsigned fail=0;fail<=7;++fail)for(bool claimed:{false,true})for(HRESULT drawResult:{0l,-7l})for(bool profile:{false,true})for(bool cached:{false,true}){
        if(cached&&fail==1)continue; // A cached success cannot simulate a native getter failure.
        Shader v,p,newV,newP;DeviceState state;state.vertex=&v;state.pixel=&p;state.failAt=fail;state.cachedGet=cached;
        for(unsigned i=0;i<state.constants.size();++i)state.constants[i]=float(i)*.03125f;
        const auto before=state.constants;
        Hook hook;hook.ext=&state;hook.gateFrame=profile;state.forwarded=&hook.mirrorState.forwarded;
        hook.shadowSwapOriginal=&p;hook.shadowSwapReplacement=&newP;hook.shadowSwapVertexOriginal=&v;hook.shadowSwapVertexReplacement=&newV;
        hook.shadowSwapConstantBase=224-count;hook.shadowSwapConstantCount=count;std::fill_n(hook.shadowSwapConstants,24,-10.f);
        unsigned draws=0;HRESULT result=hook.terrainShadowDraw(claimed,[&](){
            ++draws;
            if(fail&&fail<=4){assert(state.pixel==&p&&state.vertex==&v&&state.constants==before);}
            else{assert(state.pixel==&newP&&state.vertex==&newV);for(unsigned i=0;i<count*4;++i)assert(state.constants[(224-count)*4+i]==-10.f);}
            return drawResult;
        });
        assert(draws==unsigned(!claimed)&&result==(claimed?D3D_OK:drawResult));
        assert(state.pixel==(!claimed&&fail==6?&newP:&p));
        assert(state.vertex==(!claimed&&fail==7?&newV:&v));
        auto after=before;if(!claimed&&fail==5)std::fill_n(after.data()+(224-count)*4,count*4,-10.f);
        assert(state.constants==after);
        assert(v.releases==1&&p.releases==1&&!newV.releases&&!newP.releases);
        assert(!hook.shadowSwapOriginal&&!hook.shadowSwapReplacement&&!hook.shadowSwapVertexOriginal&&!hook.shadowSwapVertexReplacement);
        std::vector<unsigned> trace;
        if(!claimed){if(fail==1)trace={1};else if(fail==2)trace={1,2,4,3,2};else if(fail==3)trace={1,2,3,4,3,2};else trace={1,2,3,4,4,3,2};}
        checkCalls(hook,state,count,trace);++cases;
    }
    for(unsigned fail=0;fail<=2;++fail)for(bool profile:{false,true})for(bool claimed:{false,true}){
        Shader p,newP;DeviceState state;state.pixel=&p;state.failAt=fail;
        Hook hook;hook.ext=&state;hook.gateFrame=profile;state.forwarded=&hook.mirrorState.forwarded;hook.shadowSwapOriginal=&p;hook.shadowSwapReplacement=&newP;
        unsigned draws=0;const auto hr=hook.terrainShadowDraw(claimed,[&]{++draws;assert(state.pixel==(fail==1?&p:&newP));return -7l;});
        assert(draws==unsigned(!claimed)&&hr==(claimed?D3D_OK:-7l));
        assert(state.pixel==(!claimed&&fail==2?&newP:&p)&&p.releases==1&&!newP.releases);
        assert(!hook.shadowSwapOriginal&&!hook.shadowSwapReplacement);
        checkCalls(hook,state,0,claimed?std::vector<unsigned>{}:std::vector<unsigned>{3,3});++cases;
    }
    for(bool planned:{false,true})for(bool profile:{false,true})for(bool claimed:{false,true}){
        Shader v,p;DeviceState state;state.vertex=&v;state.pixel=&p;
        Hook hook;hook.ext=&state;hook.gateFrame=profile;state.forwarded=&hook.mirrorState.forwarded;
        if(planned){hook.shadowSwapOriginal=&p;hook.shadowSwapVertexOriginal=&v;}
        unsigned draws=0;const auto hr=hook.terrainShadowDraw(claimed,[&]{++draws;assert(state.vertex==&v&&state.pixel==&p);return -7l;});
        assert(draws==unsigned(!claimed)&&hr==(claimed?D3D_OK:-7l));assert(v.releases==int(planned)&&p.releases==int(planned));
        checkCalls(hook,state,0,{});++cases;
    }
    std::printf("PASS %u actual draw-wrapper state/refcount/failure/claimed/profile/native-call cases\n",cases);
}
