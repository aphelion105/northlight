// SPDX-License-Identifier: MIT
// Deterministic allocation failure, not process memory exhaustion.
#include <atomic>
#include <cstdlib>
#include <new>
static std::atomic<std::size_t> failSizeA{0},failSizeB{0};
static std::atomic<unsigned> failures{0};
static bool fail(std::size_t n){if((n==failSizeA.load()||n==failSizeB.load())&&n){++failures;return true;}return false;}
void* operator new(std::size_t n){if(!fail(n))if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{if(fail(n))return nullptr;return std::malloc(n?n:1);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
#define main original_stream_suite_main
#include "test_command_stream.cpp"
#undef main
struct RejectOnceSurface:TSurface {
    bool reject=true;
    RejectOnceSurface():TSurface(512,512,0x31545844,0,1){}
    HRESULT LockRect(D3DLOCKED_RECT* lr,const RECT* rect,DWORD flags)override{if(reject){reject=false;return E_OUTOFMEMORY;}return TSurface::LockRect(lr,rect,flags);}
};

int main(){
    // 0: shadow allocation fails -> staged write. 1: written level allocation
    // fails -> raw lock. 2: admission and staging fail -> raw lock.
    for(unsigned mode=0;mode<3;++mode){
        gTrace.clear();Rig rig(true);auto& core=rig.core();IDirect3DTexture9* tex=nullptr;
        CHECK(rig.dev->CreateTexture(512,512,1,0,(D3DFORMAT)0x31545844,(D3DPOOL)1,&tex,nullptr)==D3D_OK);rig.sync();
        auto* target=gLastTexture;auto& sub=static_cast<StreamTexture*>(ProxyBase::of(tex))->subs[0];
        if(mode==1){D3DLOCKED_RECT first{};RECT r{0,0,64,64};CHECK(tex->LockRect(0,&first,&r,0)==D3D_OK);CHECK(tex->UnlockRect(0)==D3D_OK);rig.sync();dropSubShadow(core,sub);trimTexSpares(core);}
        if(mode==2)core.q.addTexShadowBytes(std::int64_t(core.q.texShadowCap()));
        failures=0;failSizeA=131072+kLockSlack;failSizeB=mode==2?sizeof(Block)+8192:0;
        RECT rect{64,128,128,192};D3DLOCKED_RECT lock{};
        CHECK(tex->LockRect(0,&lock,&rect,0x8000)==D3D_OK);
        failSizeA=failSizeB=0;
        CHECK(failures.load()==1);
        CHECK(sub.mode==(mode?SubRes::Pass:SubRes::Staged));
        if(mode){CHECK(lock.Pitch==INT(target->surf[0]->pitch));CHECK(target->surf[0]->lockFlags==0x8000);CHECK(target->surf[0]->locked.left==64&&target->surf[0]->locked.top==128);}
        for(unsigned y=0;y<16;++y)std::memset(static_cast<unsigned char*>(lock.pBits)+std::size_t(y)*lock.Pitch,0x5a+y,128);
        CHECK(tex->UnlockRect(0)==D3D_OK);rig.sync();
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<128;++x)CHECK(target->surf[0]->mem[(32+y)*target->surf[0]->pitch+128+x]==0x5a+y);
        if(mode==2)core.q.addTexShadowBytes(-std::int64_t(core.q.texShadowCap()));
        tex->Release();rig.finish();checkClean();
    }
    {
        gTrace.clear();NorthlightAtlasProof::enabled=true;Rig rig(true);IDirect3DTexture9* tex=nullptr;
        CHECK(rig.dev->CreateTexture(512,512,1,0,(D3DFORMAT)0x31545844,(D3DPOOL)1,&tex,nullptr)==D3D_OK);rig.sync();
        auto* target=gLastTexture;delete target->surf[0];auto* rejecting=new RejectOnceSurface;rejecting->owner=target;target->surf[0]=rejecting;
        RECT rect{0,0,64,64};D3DLOCKED_RECT lock{};CHECK(tex->LockRect(0,&lock,&rect,0)==D3D_OK);
        for(unsigned y=0;y<16;++y)std::memset(static_cast<unsigned char*>(lock.pBits)+std::size_t(y)*lock.Pitch,0x5a,128);
        CHECK(tex->UnlockRect(0)==D3D_OK);rig.sync(); // game already got success, backend write was rejected
        auto* proxy=ProxyBase::of(tex);auto* proof=NorthlightAtlasProof::cache().find(proxy->raw);
        CHECK(proof&&proof->known==1&&proof->failedUploads==1&&proof->lastFailure==unsigned(E_OUTOFMEMORY));
        CHECK(proof->tiles[0]!=NorthlightAtlasProof::hashRows(rejecting->mem,rejecting->pitch,16,128));
        CHECK(get(rig.core().q.stats.replayFailures)==1);
        const void* raw=proxy->raw;tex->Release();rig.sync();CHECK(!NorthlightAtlasProof::cache().find(raw));rig.finish();checkClean();
        NorthlightAtlasProof::enabled=false;
    }
    std::puts("PASS: fresh shadow OOM -> staging; written shadow OOM -> raw; admission + staging OOM -> raw; original rect/flags/pitch and bytes preserved");
    std::puts("PASS: asynchronous backend E_OUTOFMEMORY leaves game Unlock successful; diagnostic retains attempted hash, exact HRESULT, mismatch and lifetime cleanup");
}
