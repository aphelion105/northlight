// Grazing-angle material demonstration using the production cache, capture and final shader.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "world_compiled_shaders.h"
#include "patch_terrain_shadow_coverage.h"
#ifndef NORTHLIGHT_OBLIQUE_WIDTH
#define NORTHLIGHT_OBLIQUE_WIDTH 1280
#define NORTHLIGHT_OBLIQUE_HEIGHT 800
#endif
static constexpr UINT W=NORTHLIGHT_OBLIQUE_WIDTH,H=NORTHLIGHT_OBLIQUE_HEIGHT;
static IDirect3DTexture9* solid(IDirect3DDevice9* d,float x,float y,float z,float a){Image i{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float data[]={x,y,z,a};std::memcpy(i.pixels.data(),data,16);return texture(d,i);}
static void save(IDirect3DDevice9* d,IDirect3DSurface9* target,const std::string& path){
    D3DSURFACE_DESC desc{};target->GetDesc(&desc);IDirect3DSurface9* stage=nullptr;ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(target,stage));
    const UINT bytes=desc.Format==D3DFMT_A32B32G32R32F?16:desc.Format==D3DFMT_A16B16G16R16F?8:4;std::ofstream f(path,std::ios::binary);UINT header[]={0x31524746,desc.Width,desc.Height,UINT(desc.Format),bytes};f.write((char*)header,20);D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(UINT y=0;y<desc.Height;++y)f.write((char*)lock.pBits+y*lock.Pitch,desc.Width*bytes);stage->UnlockRect();drop(stage);
}

static std::vector<float> alphaPixels(IDirect3DDevice9*d,IDirect3DSurface9*source){IDirect3DSurface9*stage=nullptr;ok(d->CreateOffscreenPlainSurface(32,32,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(source,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float>a(1024);for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)std::memcpy(&a[y*32+x],(char*)lock.pBits+y*lock.Pitch+x*16+12,4);stage->UnlockRect();drop(stage);return a;}
static std::vector<unsigned char> materialPixels(IDirect3DDevice9* d,IDirect3DSurface9* source,unsigned stride){
    NorthlightDrawSnapshot::Ref<IDirect3DSurface9> stage;
    D3DSURFACE_DESC desc{};ok(source->GetDesc(&desc));
    ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,stage.out(),nullptr));
    ok(d->GetRenderTargetData(source,stage.p));
    D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    const size_t row=size_t(desc.Width)*stride;
    std::vector<unsigned char> result(row*desc.Height);
    for(unsigned y=0;y<desc.Height;++y)std::memcpy(result.data()+y*row,static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,row);
    ok(stage->UnlockRect());
    return result;
}
static void testM2SampleZero(IDirect3DDevice9* d){
    using NorthlightDrawSnapshot::Ref;
    SavedState saved(d);assert(saved.ok);
    const auto format=D3DFMT_A16B16G16R16F;
    Ref<IDirect3DSurface9> multi,firstLevel,outputLevel,workLevel;
    Ref<IDirect3DTexture9> first,output,work,legacy,depth;
    Ref<IDirect3DPixelShader9> fill,clear,merge;
    ok(d->CreatePixelShader(kBumpClearMSShader,clear.out()));
    ok(d->CreatePixelShader(kBumpMergeMSShader,merge.out()));
    {
        using namespace NorthlightWaterShaderPatch;
        const DWORD code[]={0xffff0300,0x02000001,dst(8,0),src(2,0),65535};
        ok(d->CreatePixelShader(code,fill.out()));
    }
    const float material[]={.625f,.3125f,-2.f,1.f+39.f/256};
    const float other[]={.125f,.875f,-8.f,1.f+207.f/256};
    const float nearZ=1,farZ=64;
    auto hardwareDepth=[&](float z){return farZ*(1-nearZ/z)/(farZ-nearZ);};
    legacy.p=solid(d,.25f,.75f,-3.f,1);
    auto sample=[&](IDirect3DTexture9* fallback,float z,unsigned samples){
        drop(depth.p);depth.p=solid(d,hardwareDepth(z),0,0,1);
        ok(d->BeginScene());
        assert(NorthlightM2Samples::merge(d,outputLevel.p,first.p,fallback,depth.p,merge.p,32,32,samples,nearZ,farZ,0,1));
        ok(d->StretchRect(firstLevel.p,nullptr,workLevel.p,nullptr,D3DTEXF_NONE));
        assert(NorthlightM2Samples::mergeAndResolve(d,multi.p,workLevel.p,work.p,fallback,depth.p,merge.p,
                                                   32,32,samples,nearZ,farZ,0,1));
        ok(d->EndScene());
        const auto expected=materialPixels(d,outputLevel.p,8);
        assert(materialPixels(d,workLevel.p,8)==expected);
        return expected;
    };
    auto level=[&](Ref<IDirect3DTexture9>& texture,Ref<IDirect3DSurface9>& surface){
        ok(d->CreateTexture(32,32,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,texture.out(),nullptr));
        ok(texture.p->GetSurfaceLevel(0,surface.out()));
    };
    level(first,firstLevel);level(output,outputLevel);level(work,workLevel);
    unsigned tested=0;
    for(unsigned samples:{2u,4u,8u}){
        const auto hr=d->CreateRenderTarget(32,32,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,multi.out(),nullptr);
        if(FAILED(hr)){assert(samples!=2);continue;}
        ok(d->BeginScene());
        ok(NorthlightHdr::quadState(d,multi.p,32,32));
        ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
        ok(d->SetPixelShader(fill.p));
        ok(d->SetPixelShaderConstantF(0,other,1));
        ok(NorthlightScreen::drawQuad(d,32,32));
        // At a silhouette, sample zero belongs to the material only on this
        // side of the edge. The others hold a distinct normal, depth and cavity.
        const RECT right{16,0,32,32};
        ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,1));
        ok(d->SetScissorRect(&right));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
        ok(d->SetPixelShaderConstantF(0,material,1));
        ok(NorthlightScreen::drawQuad(d,32,32));
        assert(NorthlightM2Samples::resolve(d,multi.p,firstLevel.p,clear.p,32,32));
        ok(d->EndScene());
        const auto at=[&](const auto& image,unsigned x){uint64_t value;std::memcpy(&value,image.data()+(16*32+x)*8,8);return value;};
        const auto relief=sample(legacy.p,2,samples);
        const uint64_t expected=uint64_t(0x3900)|(uint64_t(0x3500)<<16)|(uint64_t(0xc000)<<32)|(uint64_t(0x3c00+39*4)<<48);
        assert(at(relief,24)==expected);
        const auto background=sample(legacy.p,3,samples);
        assert(at(background,8)==at(background,24));
        const auto tie=Ref<IDirect3DTexture9>{solid(d,.25f,.75f,-2.f,1)};
        const auto existing=sample(tie.p,2,samples);
        assert(at(existing,8)==at(existing,24)&&at(existing,24)!=expected);
        ++tested;drop(multi.p);
    }
    assert(tested>0&&saved.restore());
    std::printf("NATIVE_M2_MSAA_PASS sampleCounts=%u normal=exact depth=exact cavity=exact silhouette=exact legacyTie=exact reusedTargets=bitwise\n",tested);
}
static void testM2Packed(IDirect3DDevice9* d,const std::string& directory){
    using NorthlightDrawSnapshot::Ref;
    SavedState saved(d);assert(saved.ok);
    constexpr unsigned side=128,count=side*side;
    constexpr unsigned depthBase=4609;
    constexpr float cavity=.35f,nearZ=.1f,farZ=611;
    const float controls[]={float(depthBase),cavity,0,0};
    using Pixel=std::array<float,4>;
    std::vector<Pixel> source(count),scene(count),replayed(count);
    unsigned minCavity=255,maxCavity=0;
    for(unsigned i=0;i<count;++i){
        const auto code=uint16_t(depthBase+i);
        const float z=i%2==0||i==count-1?half(code):(half(code)+half(uint16_t(code+1)))*.5f;
        // Sweep both sides of the fold, including values within an FP16 ULP.
        const float u=float((i*67)%2047+1)/2048;
        const float edge=std::abs(u-.5f);
        const float v=i%4==0?float((i*131)%2049)/2048:
            std::clamp((i&1?edge:1-edge)+(float(int(i%9)-4)/8192),0.f,1.f);
        const float shade=1-cavity*float((i*31)%256)/255;
        source[i]={u,v,-z,shade};
        minCavity=std::min(minCavity,unsigned(std::lround(shade*255)));
        maxCavity=std::max(maxCavity,unsigned(std::lround(shade*255)));
        const float viewZ=std::max(nearZ,i%7==0?z+.08f:i%7==1?z-.08f:z);
        scene[i]={farZ*(1-nearZ/viewZ)/(farZ-nearZ),0,0,1};
        replayed[i]={.25f,.75f,i%7==2?-z:-128.f,i%7==2?1.f:0.f};
    }
    assert(minCavity<maxCavity);
    auto image=[&](const std::vector<Pixel>& pixels){
        Image input{side,side,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(count*16)};
        std::memcpy(input.pixels.data(),pixels.data(),input.pixels.size());
        return texture(d,input);
    };
    Ref<IDirect3DTexture9> sourceTex{image(source)},depth{image(scene)},legacy{image(replayed)};
    Ref<IDirect3DTexture9> neighbor{solid(d,.125f,.875f,-4.f,.8f)};
    Ref<IDirect3DTexture9> first,reference,packed,out;
    Ref<IDirect3DSurface9> firstLevel,referenceLevel,packedLevel,outLevel;
    auto surface=[&](D3DFORMAT format,auto& tex,auto& level){
        ok(d->CreateTexture(side,side,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,tex.out(),nullptr));
        ok(tex->GetSurfaceLevel(0,level.out()));
    };
    surface(D3DFMT_A16B16G16R16F,first,firstLevel);surface(D3DFMT_A16B16G16R16F,reference,referenceLevel);
    surface(D3DFMT_R32F,packed,packedLevel);surface(D3DFMT_A16B16G16R16F,out,outLevel);
    Ref<IDirect3DPixelShader9> pack,fp16,clear,merge,unpack;
    auto probe=[&](const char* file,auto& ps){
        auto data=read(directory+"/"+file);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(data.data()),ps.out()));
    };
    probe("PackMaterialProbe.bin",pack);probe("Fp16MaterialProbe.bin",fp16);
    ok(d->CreatePixelShader(kBumpClearMSShader,clear.out()));ok(d->CreatePixelShader(kBumpMergeMSShader,merge.out()));
    ok(d->CreatePixelShader(kBumpMergePackedMSShader,unpack.out()));
    auto draw=[&](IDirect3DTexture9* values,IDirect3DPixelShader9* shader){
        ok(d->SetTexture(0,values));ok(d->SetPixelShader(shader));ok(d->SetPixelShaderConstantF(3,controls,1));
        ok(NorthlightScreen::drawQuad(d,side,side));
    };
    auto normal=[](float u,float v){
        float x=u*2-1,y=v*2-1,z=1-std::fabs(x)-std::fabs(y);
        if(z<0){x+=x>=0?z:-z;y+=y>=0?z:-z;}
        float scale=1/std::sqrt(x*x+y*y+z*z);
        return std::array<float,4>{x*scale,y*scale,z*scale,z};
    };
    unsigned tests=0,reliefPixels=0;
    float worstAngle=0;
    int worstCavity=0;
    for(unsigned samples:{2u,4u,8u}){
        Ref<IDirect3DSurface9> multi;
        const auto hr=d->CreateRenderTarget(side,side,D3DFMT_R32F,D3DMULTISAMPLE_TYPE(samples),0,FALSE,multi.out(),nullptr);
        if(FAILED(hr)){assert(samples!=2);continue;}
        for(bool edge:{false,true}){
            const RECT rect{edge?LONG(side/2):0,0,LONG(side),LONG(side)};
            auto region=[&]{ok(d->SetScissorRect(&rect));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));};
            ok(d->ColorFill(firstLevel.p,nullptr,0));ok(d->ColorFill(multi.p,nullptr,0));
            ok(d->BeginScene());
            ok(NorthlightHdr::quadState(d,firstLevel.p,side,side));region();draw(sourceTex.p,fp16.p);
            assert(NorthlightM2Samples::merge(d,referenceLevel.p,first.p,legacy.p,depth.p,merge.p,side,side,1,nearZ,farZ,0,1));
            ok(NorthlightHdr::quadState(d,multi.p,side,side));ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xfffffffeu));draw(neighbor.p,pack.p);
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,1));region();draw(sourceTex.p,pack.p);
            assert(NorthlightM2Samples::resolve(d,multi.p,packedLevel.p,clear.p,side,side));
            assert(NorthlightM2Samples::merge(d,outLevel.p,packed.p,legacy.p,depth.p,unpack.p,side,side,samples,nearZ,farZ,0,1,depthBase,cavity));
            ok(d->EndScene());
            const auto expected=materialPixels(d,referenceLevel.p,8),result=materialPixels(d,outLevel.p,8);
            for(unsigned i=0;i<count;++i){
                uint16_t a[4],b[4];std::memcpy(a,&expected[i*8],8);std::memcpy(b,&result[i*8],8);
                if((a[3]<=0x3c00&&std::memcmp(a,b,8))||(a[3]>0x3c00&&(a[2]!=b[2]||b[3]<=0x3c00))){
                    const auto bytes=materialPixels(d,packedLevel.p,4);
                    uint32_t raw;std::memcpy(&raw,&bytes[i*4],4);
                    std::fprintf(stderr,"Packed ownership/depth mismatch samples=%u edge=%u pixel=%u z=%.9g oct=%.9g,%.9g fp16=%04x/%04x packed=%04x/%04x raw=%08x\n",
                        samples,unsigned(edge),i,-source[i][2],source[i][0],source[i][1],a[2],a[3],b[2],b[3],raw);std::abort();
                }
                if(a[3]<=0x3c00)continue;
                ++reliefPixels;
                const auto t=normal(half(a[0]),half(a[1])),u=normal(half(b[0]),half(b[1]));
                if((t[3]<0)!=(u[3]<0)){
                    std::fprintf(stderr,"Packed fold mismatch pixel=%u oct=%g,%g reference=%g,%g packed=%g,%g\n",
                        i,source[i][0],source[i][1],half(a[0]),half(a[1]),half(b[0]),half(b[1]));std::abort();
                }
                const float dot=t[0]*u[0]+t[1]*u[1]+t[2]*u[2];
                worstAngle=std::max(worstAngle,std::acos(std::clamp(dot,-1.f,1.f))*57.2957795f);
                worstCavity=std::max(worstCavity,std::abs(int(a[3])-int(b[3]))/4);
            }
            ++tests;
        }
    }
    assert(tests>=2&&reliefPixels>10000&&worstAngle<3.5f&&worstCavity<=3&&saved.restore());
    std::printf("NATIVE_M2_PACKED_PASS cases=%u reliefPixels=%u depth=exact selection=exact hemisphere=exact normalMaxDeg=%.4f cavityMaxUNorm8=%d\n",
                tests,reliefPixels,worstAngle,worstCavity);
}
template<class V> static bool testFused(IDirect3DDevice9* d,const std::vector<unsigned char>& code,
        IDirect3DPixelShader9* native,IDirect3DPixelShader9* capture,unsigned captureControl,
        IDirect3DSurface9* target,const V* quad,const float* constants,float (&settings)[9][4],bool packedProbe){
    std::vector<uint32_t> patched;
    unsigned control;
    if(!NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size()/4,patched,control,false,false,false,true,3))return false;
    NorthlightEffectCoverage::NativePixel coverage;
    coverage.source=native;coverage.count=2;
    coverage.writes[0]=coverage.writes[1]=NorthlightEffectCoverage::Write::Clear;
    coverage.slots[0]=1;coverage.slots[1]=2;
    assert(coverage.compose(patched));
    using NorthlightDrawSnapshot::Ref;
    Ref<IDirect3DPixelShader9> fused;
    Ref<IDirect3DSurface9> relief,cavity,additive;
    ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),fused.out()));
    ok(d->CreateRenderTarget(32,32,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,relief.out(),nullptr));
    ok(d->CreateRenderTarget(32,32,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,cavity.out(),nullptr));
    ok(d->CreateRenderTarget(32,32,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,additive.out(),nullptr));
    const D3DVIEWPORT9 vp{0,0,32,32,0,1};
    auto draw=[&](IDirect3DPixelShader9* shader,IDirect3DSurface9* rt0,IDirect3DSurface9* rt1,IDirect3DSurface9* rt2,IDirect3DSurface9* rt3=nullptr){
        ok(d->SetRenderTarget(0,rt0));ok(d->SetRenderTarget(1,rt1));ok(d->SetRenderTarget(2,rt2));ok(d->SetRenderTarget(3,rt3));ok(d->SetViewport(&vp));
        ok(d->SetPixelShader(shader));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());
        ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));ok(d->EndScene());
        ok(d->SetRenderTarget(3,nullptr));ok(d->SetRenderTarget(2,nullptr));ok(d->SetRenderTarget(1,nullptr));ok(d->SetRenderTarget(0,target));
    };
    settings[3][2]=.7f;settings[3][3]=48;
    ok(d->SetPixelShaderConstantF(captureControl,settings[0],9));
    draw(capture,relief.p,cavity.p,nullptr);
    const auto expectedRelief=materialPixels(d,relief.p,8),expectedCavity=materialPixels(d,cavity.p,4);
    ok(d->SetPixelShaderConstantF(0,constants,224));
    draw(native,target,nullptr,nullptr);
    const auto expectedColor=materialPixels(d,target,16);
    ok(d->SetPixelShaderConstantF(control,settings[0],9));
    draw(fused.p,target,cavity.p,additive.p,relief.p);
    assert(materialPixels(d,target,16)==expectedColor);
    const auto packed=materialPixels(d,relief.p,8);
    for(size_t i=0;i<32*32;++i){
        assert(!std::memcmp(&packed[i*8],&expectedRelief[i*8],6));
        uint16_t packedAlpha,legacyAlpha;
        std::memcpy(&packedAlpha,&packed[i*8+6],2);
        std::memcpy(&legacyAlpha,&expectedRelief[i*8+6],2);
        if(!legacyAlpha){assert(!packedAlpha);continue;}
        assert(packedAlpha>0x3c00&&packedAlpha<=0x4000);
        const auto cavityCode=(packedAlpha-0x3c00)/4-1;
        assert(cavityCode==expectedCavity[i*4]);
    }
    if(packedProbe){
        Ref<IDirect3DPixelShader9> packedShader;
        Ref<IDirect3DSurface9> scalar;
        unsigned packedControl;
        assert(NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),
            code.size()/4,patched,packedControl,false,false,false,true,3,true));
        assert(coverage.compose(patched));
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),packedShader.out()));
        ok(d->CreateRenderTarget(32,32,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,scalar.out(),nullptr));
        settings[0][3]=4609;
        ok(d->SetPixelShaderConstantF(packedControl,settings[0],9));
        draw(packedShader.p,target,cavity.p,additive.p,scalar.p);
        settings[0][3]=0;
        assert(materialPixels(d,target,16)==expectedColor);
        const auto captured=materialPixels(d,scalar.p,4);
        assert(std::any_of(captured.begin(),captured.end(),[](unsigned char value){return value!=0;}));
    }
    settings[0][1]=1;
    ok(d->SetPixelShaderConstantF(control,settings[0],9));
    draw(fused.p,target,cavity.p,additive.p,relief.p);
    assert(materialPixels(d,target,16)==expectedColor);
    const auto distant=materialPixels(d,relief.p,8);
    assert(std::all_of(distant.begin(),distant.end(),[](unsigned char value){return value==0;}));
    return true;
}
static void sliceChecks(){
 using namespace NorthlightWaterShaderPatch;NorthlightTerrainCoverage::Program p;std::vector<Word> result;
 std::vector<Word> code={0xffff0300,0x0200001f,0x80000005,dst(1,0),0x02000001,dst(0,0),src(1,0),0x01000041,dst(0,0),0x02000001,dst(8,0),src(2,0),65535};
 assert(NorthlightTerrainCoverage::analyse(code.data(),code.size(),p));assert(NorthlightNativeMaterialCapture::alphaSlice(code.data(),p,false,result));
 NorthlightTerrainCoverage::Program sliced;assert(NorthlightTerrainCoverage::analyse(result.data(),result.size(),sliced)&&sliced.slots==2); // Keep the kill and all its initialized lanes.
 assert(result[5]==dst(0,0));const auto noNop=result;code.insert(code.end()-1,0);p={};assert(NorthlightTerrainCoverage::analyse(code.data(),code.size(),p));assert(NorthlightNativeMaterialCapture::alphaSlice(code.data(),p,false,result)&&result==noNop);
 code.insert(code.end()-1,{0x01000028,src(2,0)});p={};assert(NorthlightTerrainCoverage::analyse(code.data(),code.size(),p));assert(!NorthlightNativeMaterialCapture::alphaSlice(code.data(),p,true,result));
 std::puts("NATIVE_SLICE_PASS kill_dependencies unknown_flow_fallback");
}
int main(int argc,char**argv){std::setvbuf(stdout,nullptr,_IONBF,0);assert(argc>2);auto module=LoadLibraryA(argv[1]);assert(module);auto api=((IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9"))(D3D_SDK_VERSION);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9*d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DSurface9*target=nullptr;ok(d->CreateRenderTarget(32,32,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));d->SetRenderTarget(0,target);d->SetDepthStencilSurface(nullptr);D3DVIEWPORT9 vp{0,0,32,32,0,1};d->SetViewport(&vp);for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING})d->SetRenderState(state,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
 Image source{32,32,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(4096)};for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){auto*p=source.pixels.data()+(y*32+x)*4;p[0]=64;p[1]=128;p[2]=200;p[3]=x<8?0:x<16?64:x<24?128:255;}
 auto*diffuse=texture(d,source);auto*normal=solid(d,168.f/255,106.f/255,.5f,.3f);auto*white=solid(d,1,1,1,1);for(unsigned i=0;i<16;++i){d->SetTexture(i,i==14?normal:i<2?diffuse:white);for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(i,key,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
 struct V{float x,y,z,w;DWORD color;float uv[7][2],clip[4];};V q[4]={};for(unsigned i=0;i<4;++i){q[i].x=(i&1)?31.5f:-.5f;q[i].y=(i&2)?31.5f:-.5f;q[i].w=1;q[i].color=0x80ffffff;for(unsigned j=0;j<7;++j){q[i].uv[j][0]=(i&1)?1:0;q[i].uv[j][1]=(i&2)?1:0;}q[i].clip[0]=(i&1)?1:-1;q[i].clip[1]=(i&2)?-1:1;q[i].clip[3]=2;}
 sliceChecks();d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX8|D3DFVF_TEXCOORDSIZE4(7));float constants[224][4]={};d->SetPixelShaderConstantF(0,constants[0],224);std::ifstream manifest(std::string(argv[2])+"/files.txt");std::string file;unsigned tested=0,rejected=0,opaqueTested=0,nativeSlots=0,alphaSlots=0,opaqueSlots=0,sliceFallback=0;float worst=0;
 const bool fusedOnly=argc>3&&std::string_view(argv[3])=="fused",packedProbe=argc>5&&std::string_view(argv[5])=="packed";
 if(argc>4&&std::string_view(argv[4])=="msaa")testM2SampleZero(d);
 if(packedProbe)testM2Packed(d,argv[2]);
 unsigned fusedTested=0;
 for(bool normalOnly:{false,true})for(unsigned pattern=0;pattern<2;++pattern){if(fusedOnly&&(!normalOnly||pattern))continue;manifest.clear();manifest.seekg(0);
 for(unsigned i=0;i<224;++i)for(unsigned c=0;c<4;++c)constants[i][c]=pattern?.125f*(1+(i+3*c)%8):0;
 while(std::getline(manifest,file)){d->SetPixelShaderConstantF(0,constants[0],224);auto code=read(std::string(argv[2])+"/"+file);IDirect3DPixelShader9*native=nullptr,*capture=nullptr;ok(d->CreatePixelShader((DWORD*)code.data(),&native));std::vector<uint32_t> patched;unsigned control;
  if(!NorthlightNativeMaterialCapture::patch((uint32_t*)code.data(),code.size()/4,patched,control,true,false,false,normalOnly)){++rejected;drop(native);continue;}HRESULT created=d->CreatePixelShader((DWORD*)patched.data(),&capture);if(FAILED(created)){std::fprintf(stderr,"Rejected patched %s: %08lx\n",file.c_str(),(unsigned long)created);std::abort();}
  float settings[9][4]={{.06f,48,.001f,1},{1,1,-1,0},{0,16,16,0},{0,8,0,0},{0,0,1,1},{},{1,0,0,0},{0,1,0,0},{0,0,1,0}};d->SetPixelShaderConstantF(control,settings[0],9);
  auto draw=[&](IDirect3DPixelShader9*ps){d->SetPixelShader(ps);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V)));ok(d->EndScene());return alphaPixels(d,target);};auto a=draw(native),b=draw(capture);
  for(unsigned i=0;i<a.size();++i){assert(std::isfinite(a[i])&&std::isfinite(b[i]));float expected=std::clamp(a[i],0.f,1.f);if(expected<.001f)expected=0;worst=std::max(worst,std::fabs(expected-b[i]));if(std::fabs(expected-b[i])>.0001f){std::fprintf(stderr,"Alpha mismatch %s %u %g %g\n",file.c_str(),i,expected,b[i]);std::abort();}}
  NorthlightTerrainCoverage::Program original;assert(NorthlightTerrainCoverage::analyse(reinterpret_cast<const uint32_t*>(code.data()),code.size()/4,original));nativeSlots+=original.slots;
  for(bool alpha:{true,false}){std::vector<uint32_t> sliced;NorthlightTerrainCoverage::Program p;
   if(NorthlightNativeMaterialCapture::alphaSlice(reinterpret_cast<const uint32_t*>(code.data()),original,alpha,sliced)){assert(NorthlightTerrainCoverage::analyse(sliced.data(),sliced.size(),p));(alpha?alphaSlots:opaqueSlots)+=p.slots;}else{++sliceFallback;(alpha?alphaSlots:opaqueSlots)+=original.slots;}}
  // Force only the original output alpha to one; its native texkill still
  // decides coverage. This distinguishes a killed fragment from alpha zero.
  std::vector<float> coverage(a.size(),1.f);bool killed=false;for(const auto& op:original.ops)killed|=op.code==65;
  if(killed){using namespace NorthlightWaterShaderPatch;unsigned one=223;while(original.constants[one]){assert(one);--one;}
   std::vector<uint32_t> probe(reinterpret_cast<const uint32_t*>(code.data()),reinterpret_cast<const uint32_t*>(code.data())+code.size()/4-1);
   probe.insert(probe.begin()+1,{0x05000051,dst(2,one),0x3f800000,0x3f800000,0x3f800000,0x3f800000});probe.insert(probe.end(),{0x02000001,dst(8,0,8),src(2,one,0),65535});
   IDirect3DPixelShader9* mask=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(probe.data()),&mask));coverage=draw(mask);drop(mask);}
  drop(capture);assert(NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size()/4,patched,control,false,false,false,normalOnly));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&capture));settings[0][3]=0;d->SetPixelShaderConstantF(control,settings[0],9);
  auto opaque=draw(capture);for(unsigned i=0;i<opaque.size();++i)assert(std::isfinite(opaque[i])&&std::fabs(opaque[i]-coverage[i])<.0001f);++opaqueTested;
  if(normalOnly&&pattern==0&&fusedTested<16)
      fusedTested+=testFused(d,code,native,capture,control,target,q,constants[0],settings,packedProbe);
  ++tested;drop(native);drop(capture);
  if(fusedOnly&&fusedTested==16)break;
 }
 }
 assert(fusedTested==16);std::printf("NATIVE_M2_FUSED_PASS variants=%u color=exact relief=exact cavity=exact range=exact coveragePlanes=2\n",fusedTested);
 if(fusedOnly)return 0;
 assert(tested>=128);std::printf("NATIVE_MATERIAL_ALPHA_PASS normalOnlyAndGeneric=1 variants=%u preservedFallback=%u worstAlphaError=%g opaqueVariants=%u nativeSlots=%u alphaSlots=%u opaqueSlots=%u sliceFallback=%u\n",tested,rejected,worst,opaqueTested,nativeSlots,alphaSlots,opaqueSlots,sliceFallback);return 0;
}
