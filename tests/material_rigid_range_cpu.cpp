#include "rigid_range_fixture.h"
#include <chrono>
struct RangeBuffer:ForwardIDirect3DVertexBuffer9 {
    unsigned refs=1,locks=0;std::vector<float> data;uint64_t revision=1;
    RangeBuffer():ForwardIDirect3DVertexBuffer9(nullptr),data(768*12){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* out)override{*out={};out->Size=UINT(data.size()*4);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT,void** out,DWORD)override{++locks;*out=reinterpret_cast<char*>(data.data())+offset;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Unlock()override{return D3D_OK;}
};
struct RangeDevice:CaptureBackend {
    RangeBuffer vb;Declaration layout;
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out)override{*out=&layout;layout.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT s,IDirect3DVertexBuffer9** out,UINT* offset,UINT* stride)override{if(s)return E_FAIL;*out=&vb;vb.AddRef();*offset=0;*stride=48;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT,UINT* out)override{*out=1;return D3D_OK;}
};
int main(int argc,char**){
    RangeDevice d;NorthlightAutoDisplacement g(&d);
    g.tracking([](void* p,bool){return uint64_t(uintptr_t(p));},[](void* p,bool){return static_cast<RangeBuffer*>(p)->revision;},nullptr);
    DisplacementStateTestAccess::rangeProgram(g,&d.vs);
    NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,0,0,256,false}};
    for(unsigned i=0;i<768;++i){d.vb.data[i*12]=float(i%7);d.vb.data[i*12+1]=float(i%11);d.vb.data[i*12+2]=2+float(i%13);}
    // Same declared range for nonindexed geometry: old and new depth must be
    // bit exact under current constants, writes, reset and unknown revisions.
    for(unsigned phase=0;phase<5;++phase){
        if(phase==1){d.vb.data[2]=-.5f;++d.vb.revision;}
        if(phase==2)g.reset();
        if(phase==3)d.vb.revision=0;
        if(phase==4){d.vb.revision=10;g.setMemoryPressure(true);}
        for(float translation:{-100.f,-48.f,-8.f,0.f,8.f,48.f,100.f}){
            d.bank[2]=translation;g.endFrame();float low=0,high=0;
            assert(g.nearestMaterialDepth(&d.vs,args,low,false,nullptr,&high));
            unsigned l,h;std::memcpy(&l,&low,4);std::memcpy(&h,&high,4);
            std::printf("RANGE_RESULT %u %g %u %u\n",phase,translation,l,h);
        }
    }
    g.setMemoryPressure(false);d.vb.revision=12;d.bank[2]=0;
    d.vb.data.resize(136*48*12);args.draw.primitives=16;
    for(unsigned i=0;i<136*48;++i){d.vb.data[i*12]=float(i%7);d.vb.data[i*12+1]=float(i%11);d.vb.data[i*12+2]=2+float(i%13);}
    for(unsigned i=0;i<16;++i){g.endFrame();for(unsigned draw=0;draw<136;++draw){float nearest;args.draw.start=draw*48;assert(g.nearestMaterialDepth(&d.vs,args,nearest));}}
    for(bool rewritten:{false,true}){
    const auto locks=d.vb.locks;std::vector<double> times;volatile float result=0;
    for(unsigned block=0;block<12;++block){
        auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<32;++frame){g.endFrame();if(rewritten){d.vb.data[2]+=0.00001f;++d.vb.revision;}for(unsigned draw=0;draw<136;++draw){float depthValue;args.draw.start=draw*48;d.bank[2]=float(draw%9);assert(g.nearestMaterialDepth(&d.vs,args,depthValue));result=depthValue;}}
        if(block>=2)times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/32);
    }
    std::sort(times.begin(),times.end());assert(rewritten||d.vb.locks==locks);
    std::printf("RANGE_CPU_PASS medianMs=%.6f rewritten=%u draws=136 verticesPerDraw=48 locks=%u result=%g\n",times[5],unsigned(rewritten),d.vb.locks-locks,double(result));
    }
}
