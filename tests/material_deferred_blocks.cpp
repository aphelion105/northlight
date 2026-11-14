#include "material_packet_bindings.h"
#include "northlight_mem.h"
#include <chrono>
extern "C" int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}

struct DeferredBlockDevice:MaterialBindingDevice {
    bool measure=false;unsigned submitted=0;
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT)override{
        ++submitted;return measure?D3D_OK:record(false);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT)override{
        ++submitted;if(!measure)return record(true);state.streams[0]=nullptr;state.indices=nullptr;return D3D_OK;
    }
};
void MaterialCaptureStateTestAccess::deferredBlocks(){
    using B=NorthlightAutoBump;using Mesh=NorthlightDrawSnapshot::Mesh;
    for(unsigned shape=0;shape<3;++shape){
        DeferredBlockDevice d;const auto initial=d.state;B b(&d);
        b.width=b.heightPixels=64;b.target=&d.capture;b.target->AddRef();b.captureDepth=&d.privateDepth;b.captureDepth->AddRef();
        b.occlusionTarget=&d.auxiliary;b.occlusionTarget->AddRef();b.independentCaptureWrites=true;
        b.sparePackets.reserve(512);b.deferred.reserve(512);d.floatCalls.reserve(8192);
        std::vector<std::shared_ptr<const Mesh>> meshes;
        for(unsigned i=0;i<16;++i){auto m=std::make_shared<Mesh>();m->topology=D3DPT_TRIANGLELIST;m->vertexCount=3;m->primitiveCount=1;m->streams[0].stride=12;m->streams[0].bytes.resize(36);meshes.push_back(m);}
        auto queue=[&](unsigned count){
            for(unsigned i=0;i<count;++i){
                auto p=b.sparePackets.empty()?std::make_unique<B::Deferred>():std::move(b.sparePackets.back());if(!b.sparePackets.empty())b.sparePackets.pop_back();
                p->mesh=meshes[i%meshes.size()];p->vertex=&d.vs;p->vertex->AddRef();p->pixel=&d.ps;p->pixel->AddRef();p->declaration=&d.decl;p->declaration->AddRef();
                p->tag=19;p->viewport={0,0,64,64,0,1};p->first=20;p->vertexFirst=0;p->vertexCount=32;p->pixelConstants.assign(20,1.f);
                std::fill(p->vertexConstants,p->vertexConstants+128,1.f);p->vertexConstants[12]=float(i%7);
                p->vertexFloatOnly=p->pixelFloatOnly=true;p->occlusion=i%5==0;p->addressU=p->addressV=D3DTADDRESS_WRAP;
                std::fill(std::begin(p->states),std::end(p->states),DWORD(1));std::fill(std::begin(p->raster),std::end(p->raster),DWORD(1));
                // Every member changes in turn; mixed sequences still exercise
                // whole-block equality on the draws between changes.
                if(shape){p->states[(i/2)%14]=DWORD(i/2+2);p->raster[(i/2)%B::DeferredRasterCount]=DWORD(i/2+3);}
                if(shape==2){p->occlusion=i%2;p->vertexCount=256;std::fill(p->vertexConstants,p->vertexConstants+1024,float(i/4));}
                b.deferred.push_back(std::move(p));
            }
        };
        NorthlightReplayGPU::createBudgetMs()=1e9;
        for(unsigned frame=0;frame<4;++frame){b.deferredGPU.beginFrame();queue(143);assert(b.finishCaptures());d.draws.clear();d.floatCalls.clear();}
        d.submitted=0;b.replaySkipped=b.replaySetters=0;queue(143);assert(b.finishCaptures()&&d.state==initial&&d.submitted==143);
        uint64_t digest=1469598103934665603ull;for(auto h:d.draws)digest=(digest^h)*1099511628211ull;
        printf("DEFERRED_BLOCK_CASE shape=%u draws=%u hash=%llu issued=%u skipped=%u restored=1\n",shape,d.submitted,(unsigned long long)digest,b.replaySetters,b.replaySkipped);
        d.measure=true;std::vector<double> samples;
        for(unsigned round=0;round<20;++round){double elapsed=0;
            for(unsigned frame=0;frame<64;++frame){b.deferredGPU.beginFrame();d.floatCalls.clear();queue(143);const auto start=std::chrono::steady_clock::now();
                assert(b.finishCaptures());elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();assert(d.state==initial);}
            if(round>=4)samples.push_back(elapsed/64);
        }
        std::sort(samples.begin(),samples.end());printf("DEFERRED_BLOCK_TIMING shape=%u draws=143 medianMs=%.9f\n",shape,(samples[7]+samples[8])*.5);
    }
    puts("DEFERRED_BLOCKS_PASS");
}
int main(){MaterialCaptureStateTestAccess::deferredBlocks();}
