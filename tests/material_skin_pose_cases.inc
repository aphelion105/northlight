// Match the production DLL's variable-length memory comparison.
#include "northlight_mem.h"
extern "C" int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}

#ifdef NORTHLIGHT_COMPARE_MATERIAL_BOUNDS
#include "previous_material_bounds.h"
#endif
static void testMaterialSkinPose(Program program,bool benchmark){
    if(!program.kernel.skin){
        std::swap(program.operations[0].source[0],program.operations[0].source[1]);
        buildKernel(program);
    }
    assert(SkinEnvelope::supports(program));
    auto mesh=geometry(1200);
    // Sparse palette with both end bones. Changing a row in a gap is harmless.
    for(unsigned v=0;v<mesh.vertexCount;++v){
        auto* indices=mesh.streams[0].bytes.data()+32*v+28;
        for(unsigned k=0;k<4;++k)indices[k]=indices[k]%12==11?74:indices[k]%12;
    }
    struct Declaration final : IDirect3DVertexDeclaration9 {
        ULONG refs=1;
        ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
        ULONG STDMETHODCALLTYPE Release()override{return --refs;}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
        HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
        HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9*,UINT*)override{return E_FAIL;}
    } decl;
    using Owner=NorthlightMaterialSkinBounds;
    auto owner=Owner::build(program,mesh,declaration,4,27,&decl);
    assert(owner&&owner->matches(27,&decl)&&!owner->matches(28,&decl)&&decl.refs==2);
    float constants[1024],basis[16]={0,1,0,0,-1,0,0,0,0,0,1,0,0,0,0,1};
    const float angle=.67f;
    basis[0]=basis[5]=std::cos(angle);
    basis[1]=std::sin(angle);
    basis[4]=-basis[1];
    NorthlightMaterialViewBounds camera;
    camera.set(basis);
    palette(constants,4);
    Owner::Stats stats;
    auto check=[&](const Program& p,bool hit){
        Bounds actual,view,reference;
        const auto hits=stats.reused;
        const bool expected=Owner::evaluateUncached(owner->geometry(),p,constants,camera.inverse(),reference,true);
        assert(owner->evaluate(p,constants,camera,actual,view,stats)==expected);
        const auto expectedView=expected?camera.inView(reference):Bounds{};
        assert(view.valid==expectedView.valid&&!std::memcmp(view.low,expectedView.low,12)&&!std::memcmp(view.high,expectedView.high,12));
        assert(actual.valid==reference.valid&&stats.reused==hits+unsigned(hit));
        assert(!std::memcmp(actual.low,reference.low,sizeof actual.low)&&!std::memcmp(actual.high,reference.high,sizeof actual.high));
    };
    check(program,false);check(program,true);
    constants[4*(31+3*43)+3]+=41;check(program,true);
    constants[4*255+3]+=1;check(program,false);
    camera.set(nullptr);check(program,false);
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    camera.set(identity);check(program,false);
    camera.set(basis);check(program,false);
    const auto mode=std::fegetround();
    assert(std::fesetround(FE_DOWNWARD)==0);check(program,false);
    assert(std::fesetround(mode)==0);check(program,false);
    auto overridden=program;
    overridden.definitions.push_back({255,{0,0,1,77}});
    check(overridden,false);check(program,true);
    const float value=constants[4*255+3];
    constants[4*255+3]=NAN;check(program,false);
    constants[4*255+3]=value;check(program,true);
    assert(owner->bytes()>=sizeof(Owner)+12*12*sizeof(float));

#ifdef NORTHLIGHT_COMPARE_MATERIAL_BOUNDS
    if(benchmark){
        PreviousMaterialBounds before;
        before.setDistanceView(basis);
        const auto referenceView=[&](const Bounds& b){
            const auto reference=before.distanceBoundsInView(b);
            const auto current=camera.inView(b);
            assert(reference.valid==current.valid&&!std::memcmp(reference.low,current.low,12)&&!std::memcmp(reference.high,current.high,12));
        };
        Bounds reference;
        assert(before.evaluateLocalSkin(owner->geometry(),program,constants,before.distanceInverseView,reference,true));
        referenceView(reference);
        using Clock=std::chrono::steady_clock;
        constexpr unsigned draws=800,frames=64;
        volatile float sink=0;
        for(unsigned period:{1u,2u,16u,800u}){
            double elapsed[2][8]={};
            for(unsigned block=0;block<8;++block)for(unsigned step=0;step<2;++step){
                const unsigned cached=step^(block&1u);
                const auto start=Clock::now();
                for(unsigned frame=0;frame<frames;++frame)for(unsigned draw=0;draw<draws;++draw){
                    constants[4*31+3]=float((draw/period+frame)%29);
                    Bounds result,view;
                    bool valid;
                    if(cached)valid=owner->evaluate(program,constants,camera,result,view,stats);
                    else{
                        valid=before.evaluateLocalSkin(owner->geometry(),program,constants,before.distanceInverseView,result,true);
                        view=before.distanceBoundsInView(result);
                    }
                    assert(valid);
                    sink=result.low[0]+view.high[2];
                }
                elapsed[cached][block]=std::chrono::duration<double,std::milli>(Clock::now()-start).count()/frames;
            }
            for(auto& samples:elapsed)std::sort(std::begin(samples),std::end(samples));
            std::printf("MATERIAL_SKIN_POSE_BENCH draws=%u bones=%u drawsPerPose=%u beforeMs=%.6f afterMs=%.6f\n",
                draws,owner->geometry().usedBones,period,(elapsed[0][3]+elapsed[0][4])*.5,(elapsed[1][3]+elapsed[1][4])*.5);
        }
        assert(std::isfinite(sink));
    }
#endif
    owner.reset();assert(decl.refs==1);
    std::puts("MATERIAL_SKIN_POSE_PASS sparse_palette basis active_unused_rows FP_mode DEF invalid source_lifetime exact_bounds");
}
