#define NORTHLIGHT_MATERIAL_DISK_SUPPORT_ONLY
#include "material_disk_gpu.cpp"
#define NorthlightMaterialMapPersistence ReferenceMaterialMapPersistence
#include "reference_persistence.h"
#undef NorthlightMaterialMapPersistence

template<class Disk> static void compare(DiskMeter& meter,const std::filesystem::path& root,const char* name,bool current,bool crossAspect){
    Disk disk(&meter,root/name,"staging-test");
    typename Disk::Probe probe;probe.status=Disk::Status::Hit;
    probe.ticket=std::make_shared<NorthlightMaterialDisk::Store::Ticket>();
    const unsigned sizes[][2]={{512,512},{64,64},{512,64},{64,512},{31,17},{128,256},{511,127},{1,1}};
    const auto starts=meter.stagingCreates;unsigned maps=0;double uploadMs=0;
    for(unsigned round=0;round<12;++round)for(unsigned slot=0;slot<8;++slot){
        const unsigned width=crossAspect&&slot==0?64:sizes[slot][0],height=sizes[slot][1];
        IDirect3DTexture9* target=nullptr;
        ok(meter.real->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&target,nullptr));
        probe.ticket->pixels.resize(size_t(width)*height*4);
        // Nonuniform rows reveal pitch/rectangle mistakes and stale larger-map bytes.
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<4;++c)
            probe.ticket->pixels[(size_t(y)*width+x)*4+c]=std::uint8_t(x*17+y*31+c*43+round*19+slot*7);
        const auto start=std::chrono::steady_clock::now();
        assert(disk.upload(probe,target,width,height));
        uploadMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        assert(pixels(meter.real,target)==probe.ticket->pixels);++maps;drop(target);
    }
    const unsigned allocations=meter.stagingCreates-starts;
    assert(allocations==(current?(crossAspect?2u:1u):96u));
    // Invalid dimensions cannot discard a useful staging surface.
    auto before=meter.stagingCreates;assert(!disk.upload(probe,nullptr,0,1));assert(meter.stagingCreates==before);
    IDirect3DTexture9* target=nullptr;ok(meter.real->CreateTexture(1,1,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&target,nullptr));
    disk.trimUploads();assert(disk.upload(probe,target,1,1)&&meter.stagingCreates==before+1);
    disk.reset();assert(disk.upload(probe,target,1,1)&&meter.stagingCreates==before+2);
    assert(pixels(meter.real,target)==probe.ticket->pixels);drop(target);
    std::printf("{\"name\":\"%s\",\"current\":%s,\"cross_aspect\":%s,\"maps\":%u,\"allocations\":%u,\"upload_ms\":%.6f,\"exact\":true}\n",name,current?"true":"false",crossAspect?"true":"false",maps,allocations,uploadMs);
}

int main(int argc,char** argv){
    assert(argc==3);auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    auto window=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    {DiskMeter meter(raw);const std::filesystem::path root=argv[2];
        // Existing failure and busy-device contracts run against the changed code.
        MaterialDiskGpuTestAccess::readMatching(meter,root);
        MaterialDiskGpuTestAccess::uploads(meter,root);
        for(unsigned run=0;run<8;++run){const bool current=run%4==1||run%4==2;const auto name=std::string(current?"after-":"before-")+std::to_string(run);
            if(current)compare<NorthlightMaterialMapPersistence>(meter,root,name.c_str(),true,false);
            else compare<ReferenceMaterialMapPersistence>(meter,root,name.c_str(),false,false);
        }
        compare<NorthlightMaterialMapPersistence>(meter,root,"cross-aspect-after",true,true);
        compare<ReferenceMaterialMapPersistence>(meter,root,"cross-aspect-before",false,true);
    }
    drop(raw);drop(api);FreeLibrary(module);DestroyWindow(window);std::puts("MATERIAL_STAGING_SIZES_PASS");
}
