// The same source index remembers a pending disk map for this frame only.
#define NORTHLIGHT_NORMAL_REUSE_SUPPORT_ONLY
#include "auto_normal_reuse_gpu.cpp"
#undef NORTHLIGHT_NORMAL_REUSE_SUPPORT_ONLY
#include <chrono>
#ifdef NORTHLIGHT_PENDING_REFERENCE
#define NorthlightAutoNormalCache NorthlightAutoNormalCacheReference
#define GlobalMemoryStatusEx cacheMemory
#include "pending_reference.h"
#undef GlobalMemoryStatusEx
#undef NorthlightAutoNormalCache
#endif
struct MaterialDiskGpuTestAccess {
    template<class Cache> static void block(Cache& c){assert(c.disk);c.disk->captured=true;}
    template<class Cache> static void empty(const Cache& c){assert(c.entries.empty()&&c.lookup[0].empty()&&c.lookup[1].empty()&&!c.bytes&&!c.sourceBytes);}
    static void pending(const NorthlightAutoNormalCache& c,unsigned n){assert(c.entries.size()==n&&c.bytes==0&&c.sourceBytes<=128ull<<20);for(const auto& e:c.entries)assert(e.source&&!e.normal);}
    static void ready(NorthlightAutoNormalCache& c,IDirect3DTexture9* source,bool model,const std::vector<unsigned char>& data){
        using Disk=NorthlightMaterialMapPersistence;using Store=NorthlightMaterialDisk::Store;
        auto r=std::make_unique<Disk::Read>();r->source=source;source->AddRef();r->revision=NorthlightMaterialHeight::version(source);r->used=c.disk->frame;
        r->model=model;r->polarity=model?0:c.polarity;r->details=model?0:c.details;r->balance=model?50:c.balance;
        r->ticket=std::make_shared<Store::Ticket>();r->ticket->pixels=data;r->ticket->state.store(Store::State::Hit);c.disk->reads.push_back(std::move(r));
    }
    static void correctness(IDirect3DDevice9* d,IDirect3DTexture9* source,const Image& image,const std::filesystem::path& root){
        NorthlightAutoNormalCache c(d),fresh(d);c.setPersistentCache(root/"correctness","test");
        ok(d->BeginScene());auto map=fresh.acquire(source,true);assert(map);ok(d->EndScene());const auto expected=pixels(d,map.get());
        block(c);for(unsigned i=0;i<100;++i)assert(!c.resolve(source,true));pending(c,1);assert(!c.hits&&c.diskPending==100&&!c.generated);
        ready(c,source,true,expected);assert(!c.resolve(source,true));assert(c.disk->uploads==0); // Stable until the frame ends.
        assert(!c.resolve(source,false));pending(c,2); // Recipes stay independent.
        c.endFrame();empty(c);ok(d->BeginScene());auto restored=c.acquire(source,true);ok(d->EndScene());
        assert(restored&&c.disk->uploads==1&&c.builds==0&&pixels(d,restored.get())==expected);restored={};c.reset();
        block(c);assert(!c.resolve(source,true));c.entries[0].sourceBytes=c.sourceBytes=128ull<<20;
        auto* other=texture(d,image);ready(c,other,true,expected);auto admitted=c.acquire(other,true);
        assert(admitted&&c.entries.size()==1&&c.entries[0].source==other&&pixels(d,admitted.get())==expected);admitted={};c.reset();c.endFrame();drop(other);
        auto& texels=NorthlightMaterialTextures::cache();texels.upload(source,source,image.width,image.height,0,image.format,image.pixels.data(),image.pixels.size(),image.width*4,true,true,D3DPOOL_MANAGED);
        const auto first=NorthlightMaterialHeight::version(source);assert(first);block(c);assert(!c.resolve(source,true));pending(c,1);
        texels.invalidate(source);const auto second=NorthlightMaterialHeight::version(source);assert(first!=second);assert(!c.resolve(source,true));pending(c,1);assert(c.entries[0].version==second);
        NorthlightQuality::Settings settings;settings.autoHeightPolarity=2;c.configure(settings);empty(c);
        block(c);assert(!c.resolve(source,true));pending(c,1);c.reset();empty(c);
        c.sourceBytes=128ull<<20;block(c);assert(!c.resolve(source,true));assert(c.entries.empty());c.reset();
        auto* held=texture(d,image);block(c);assert(!c.resolve(held,true));held->Release();pending(c,1);
        D3DSURFACE_DESC desc{};ok(c.entries[0].source->GetLevelDesc(0,&desc));c.endFrame();empty(c);
        texels.erase(source);std::puts("MATERIAL_PENDING_FRAME_PASS attempts=100 readyNextFrame=exact recipes=separate revision=invalidated settingsReset=cleared sourceOwner=retained sourceBudget=bounded pendingEvictedBeforeReady=1 frameEnd=cleared");
    }
#ifdef NORTHLIGHT_PENDING_REFERENCE
    static void benchmark(IDirect3DDevice9* d,const std::vector<IDirect3DTexture9*>& sources,const std::filesystem::path& root){
      for(bool pending:{false,true}){
        NorthlightAutoNormalCache current(d);NorthlightAutoNormalCacheReference before(d);
        if(pending){current.setPersistentCache(root/"bench","test");before.setPersistentCache(root/"bench","test");}
        else for(auto* source:sources){ok(d->BeginScene());auto* a=current.resolve(source,true);auto* b=before.resolve(source,true);ok(d->EndScene());assert(a&&b&&pixels(d,a)==pixels(d,b));current.endFrame();before.endFrame();}
        for(unsigned count:{1u,8u,32u})for(unsigned blockIndex=0;blockIndex<16;++blockIndex){const bool now=blockIndex%4==1||blockIndex%4==2;
            const auto run=[&](auto& c){c.endFrame();const auto start=std::chrono::steady_clock::now();
                for(unsigned frame=0;frame<64;++frame){if(pending)block(c);for(unsigned i=0;i<512;++i)assert(bool(c.resolve(sources[i%count],true))!=pending);c.endFrame();if(pending)empty(c);}
                return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/64;};
            const double ms=now?run(current):run(before);
            std::printf("MATERIAL_PENDING_TIMING pending=%u textures=%u block=%u current=%u warm=%u attempts=512 cpuMs=%.9f\n",unsigned(pending),count,blockIndex,unsigned(now),unsigned(blockIndex<4),ms);
        }
      }
    }
#endif
};
int main(int argc,char** argv){
    assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));auto* api=create(D3D_SDK_VERSION);assert(api);
    auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    const std::filesystem::path root=std::filesystem::u8path(argv[2]);Image image{256,128,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(256*128*4,128)};
    for(size_t i=3;i<image.pixels.size();i+=4)image.pixels[i]=255;
    std::vector<IDirect3DTexture9*> sources;for(unsigned i=0;i<32;++i)sources.push_back(texture(d,image));
    MaterialDiskGpuTestAccess::correctness(d,sources[0],image,root);
#ifdef NORTHLIGHT_PENDING_REFERENCE
    MaterialDiskGpuTestAccess::benchmark(d,sources,root);
#endif
    for(auto*& source:sources)drop(source);d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
