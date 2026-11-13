#pragma once
#include "utf8_path.h"
#include <d3d9.h>
#include "local_light_selection.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Explicit, bounded normal-view diagnostics. Nothing is retained beyond this
// render call; a missing export is never replaced with an invented texture.
namespace NorthlightLightingCapture {
// Diagnostic admission precedes world projection discovery. Terrain may use a
// program outside the registered families and draw before the first model.
inline bool traceTargetMatches(IDirect3DDevice9* device,unsigned width,unsigned height){
    if(!device||!width||!height)return false;
    IDirect3DSurface9 *target=nullptr,*depth=nullptr;D3DSURFACE_DESC rt{},ds{};D3DVIEWPORT9 vp{};DWORD z=FALSE;
    const bool ok=SUCCEEDED(device->GetRenderTarget(0,&target))&&target&&SUCCEEDED(target->GetDesc(&rt))&&
        SUCCEEDED(device->GetDepthStencilSurface(&depth))&&depth&&SUCCEEDED(depth->GetDesc(&ds))&&
        SUCCEEDED(device->GetViewport(&vp))&&SUCCEEDED(device->GetRenderState(D3DRS_ZENABLE,&z))&&z!=FALSE&&
        rt.Width==width&&rt.Height==height&&ds.Width==width&&ds.Height==height&&
        !vp.X&&!vp.Y&&vp.Width==width&&vp.Height==height;
    if(depth)depth->Release();if(target)target->Release();return ok;
}
class Capture {
    template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}};
    IDirect3DDevice9* device_=nullptr;
    const std::string* directory_=nullptr;
    const char* journalName_="lighting-inputs";
    FILE* journal_=nullptr;
    unsigned capture_=0,frame_=0,rendererFrame_=0,passes_=0;
    bool complete_=true,finished_=false;
    struct Input {IDirect3DBaseTexture9* identity=nullptr;unsigned snapshot=0;bool known=false;};
    std::array<std::array<Input,16>,5> groups_{};
    std::array<IDirect3DPixelShader9*,8> shaders_{};unsigned shaderCount_=0;
    Ref<IDirect3DSurface9> nativeResolve_;D3DSURFACE_DESC nativeResolveDesc_{};
    Ref<IDirect3DSurface9> traceTarget_;D3DSURFACE_DESC traceDesc_{};
    unsigned traceDraws_=0,traceSkipped_=0;
    static unsigned pixelBytes(D3DFORMAT format){
        return format==D3DFMT_A16B16G16R16F||format==D3DFMT_G32R32F?8:
            format==D3DFMT_R32F||format==D3DFMT_A8R8G8B8||format==D3DFMT_X8R8G8B8?4:0;
    }

    bool path(const char* name,const char* extension,char (&output)[2048])noexcept{
        const int n=std::snprintf(output,sizeof output,"%s/capture-%u-%s.%s",directory_->c_str(),capture_,name,extension);
        if(n<0||size_t(n)>=sizeof output){complete_=false;return false;}return true;
    }
    void unavailable(const char* item,const char* reason)noexcept{
        complete_=false;if(journal_)std::fprintf(journal_,"{\"event\":\"unavailable\",\"pass\":%u,\"item\":\"%s\",\"reason\":\"%s\"}\n",passes_,item,reason);
    }
    bool call(const char* operation,HRESULT hr,bool notFoundAllowed=false)noexcept{
        if(journal_)std::fprintf(journal_,"{\"event\":\"api\",\"pass\":%u,\"operation\":\"%s\",\"HRESULT\":\"%08lx\"}\n",passes_,operation,(unsigned long)hr);
        if(FAILED(hr)&&!(notFoundAllowed&&hr==D3DERR_NOTFOUND))complete_=false;return SUCCEEDED(hr);
    }
    bool bytes(const char* name,const char* extension,const void* data,size_t size)noexcept{
        char output[2048];bool ok=false;
        if(path(name,extension,output))if(FILE* f=NorthlightFiles::open(output,"wb")){
            ok=std::fwrite(data,1,size,f)==size;if(std::fclose(f))ok=false;
        }
        if(journal_)std::fprintf(journal_,"{\"event\":\"file\",\"name\":\"%s.%s\",\"bytes\":%zu,\"saved\":%s}\n",name,extension,size,ok?"true":"false");
        if(!ok)complete_=false;return ok;
    }
    bool surface(const char* name,IDirect3DSurface9* source,bool resolveAllowed=true)noexcept{
        if(!source){unavailable(name,"missing-surface");return false;}
        D3DSURFACE_DESC desc{};if(!call("GetDesc",source->GetDesc(&desc)))return false;
        const unsigned stride=pixelBytes(desc.Format);
        std::fprintf(journal_,"{\"event\":\"image\",\"name\":\"%s\",\"width\":%u,\"height\":%u,\"format\":%u,\"usage\":%lu,\"pool\":%u,\"samples\":%u,\"sample_quality\":%u}\n",name,desc.Width,desc.Height,unsigned(desc.Format),(unsigned long)desc.Usage,unsigned(desc.Pool),unsigned(desc.MultiSampleType),unsigned(desc.MultiSampleQuality));
        if(!stride||!desc.Width||!desc.Height||uint64_t(desc.Width)*desc.Height*stride>64u*1024u*1024u){unavailable(name,"unsupported-format-or-size");return false;}
        if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
            // Keep the application's scene boundary. Backends that reject this
            // resolve at the capture point produce an incomplete journal.
            if(!resolveAllowed){unavailable(name,"multisample-resolve-failed");return false;}
            Ref<IDirect3DSurface9> resolved;
            if(!call("CreateRenderTarget-color-resolve",device_->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved.p,nullptr))||!resolved.p){unavailable(name,"resolve-target-unavailable");return false;}
            if(!call("StretchRect-color-resolve",device_->StretchRect(source,nullptr,resolved.p,nullptr,D3DTEXF_NONE)))return false;
            std::fprintf(journal_,"{\"event\":\"color-resolve\",\"name\":\"%s\",\"source_samples\":%u,\"method\":\"StretchRect-NONE\",\"per_sample_image\":false}\n",name,unsigned(desc.MultiSampleType));
            return surface(name,resolved.p,false);
        }
        Ref<IDirect3DSurface9> staging;
        if(desc.Pool==D3DPOOL_MANAGED||desc.Pool==D3DPOOL_SYSTEMMEM){staging.p=source;source->AddRef();}
        else{
            if(!(desc.Usage&D3DUSAGE_RENDERTARGET)){unavailable(name,"non-lockable-input-without-render-target-usage");return false;}
            if(!call("CreateOffscreenPlainSurface",device_->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging.p,nullptr))||!staging.p){unavailable(name,"staging-unavailable");return false;}
            if(!call("GetRenderTargetData",device_->GetRenderTargetData(source,staging.p)))return false;
        }
        D3DLOCKED_RECT lock{};if(!call("LockRect-readonly",staging.p->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return false;
        bool ok=false;char output[2048];
        if(lock.pBits&&lock.Pitch>=INT(desc.Width*stride)&&path(name,"fgr",output)){
            if(FILE* f=NorthlightFiles::open(output,"wb")){
                const uint32_t header[]={0x31524746,desc.Width,desc.Height,unsigned(desc.Format),stride};
                ok=std::fwrite(header,sizeof header,1,f)==1;
                for(unsigned y=0;y<desc.Height&&ok;++y)ok=std::fwrite(static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch,stride,desc.Width,f)==desc.Width;
                if(std::fclose(f))ok=false;
            }
        }
        const bool unlocked=call("UnlockRect-readonly",staging.p->UnlockRect());
        std::fprintf(journal_,"{\"event\":\"file\",\"name\":\"%s.fgr\",\"bytes\":%llu,\"saved\":%s}\n",name,(unsigned long long)(20+uint64_t(desc.Width)*desc.Height*stride),ok?"true":"false");
        if(!ok)complete_=false;return ok&&unlocked;
    }
    bool depth(const char* name,IDirect3DTexture9* source,const D3DSURFACE_DESC& desc,IDirect3DSurface9* nameStencil=nullptr)noexcept{
        if(!desc.Width||!desc.Height||uint64_t(desc.Width)*desc.Height*4>64u*1024u*1024u){unavailable(name,"depth-size-limit");return false;}
        // ps_3_0; dcl_texcoord v0.xy; dcl_2d s0; texld r0,v0,s0;
        // mov oC0,r0. No depth inversion, filtering or half-resolution input.
        static const DWORD code[]={0xffff0300,0x0200001f,0x80000005,0x90030000,
            0x0200001f,0x90000000,0xa00f0800,0x03000042,0x800f0000,0x90e40000,0xa0e40800,
            0x02000001,0x800f0800,0x80e40000,0x0000ffff};
        static const DWORD white[]={0xffff0200,0x05000051,0xa00f0000,0x3f800000,0x3f800000,0x3f800000,0x3f800000,
            0x02000001,0x800f0800,0xa0e40000,0xffff};
        Ref<IDirect3DSurface9> target;Ref<IDirect3DPixelShader9> shader;
        if(!call("CreateRenderTarget-depth-copy",device_->CreateRenderTarget(desc.Width,desc.Height,nameStencil?D3DFMT_A8R8G8B8:D3DFMT_R32F,nameStencil?desc.MultiSampleType:D3DMULTISAMPLE_NONE,nameStencil?desc.MultiSampleQuality:0,FALSE,&target.p,nullptr))||!target.p){unavailable(name,"depth-target-unavailable");return false;}
        if(!call("CreatePixelShader-depth-copy",device_->CreatePixelShader(nameStencil?white:code,&shader.p))||!shader.p){unavailable(name,"depth-shader-unavailable");return false;}
        Ref<IDirect3DStateBlock9> state;Ref<IDirect3DSurface9> targets[4],oldDepth;
        D3DVIEWPORT9 viewport{};float vertex[256*4]{},pixel[224*4]{};D3DCAPS9 caps{};
        bool saved=call("CreateStateBlock-depth-copy",device_->CreateStateBlock(D3DSBT_ALL,&state.p))&&state.p;
        for(unsigned i=0;i<4;++i){const HRESULT hr=device_->GetRenderTarget(i,&targets[i].p);call("GetRenderTarget-depth-copy",hr,i!=0);saved=(SUCCEEDED(hr)||(i&&hr==D3DERR_NOTFOUND))&&saved;}
        {const HRESULT hr=device_->GetDepthStencilSurface(&oldDepth.p);call("GetDepthStencilSurface-depth-copy",hr,true);saved=(SUCCEEDED(hr)||hr==D3DERR_NOTFOUND)&&saved;}
        saved=call("GetViewport-depth-copy",device_->GetViewport(&viewport))&&saved;
        saved=call("GetDeviceCaps-depth-copy",device_->GetDeviceCaps(&caps))&&saved;
        const UINT vertexCount=std::min<UINT>(caps.MaxVertexShaderConst,256);
        saved=vertexCount&&call("GetVertexShaderConstantF-depth-copy",device_->GetVertexShaderConstantF(0,vertex,vertexCount))&&saved;
        saved=call("GetPixelShaderConstantF-depth-copy",device_->GetPixelShaderConstantF(0,pixel,224))&&saved;
        if(!saved||!targets[0].p){unavailable(name,"state-save-failed");return false;}
        bool changed=false,ok=true;
        auto set=[&](const char* operation,HRESULT hr){changed=true;const bool success=call(operation,hr);ok=success&&ok;};
        set("SetDepthStencilSurface-depth-copy",device_->SetDepthStencilSurface(nullptr));
        for(unsigned i=1;i<4;++i)set("SetRenderTarget-depth-copy",device_->SetRenderTarget(i,nullptr));
        set("SetRenderTarget-depth-copy",device_->SetRenderTarget(0,target.p));
        set("SetVertexShader-depth-copy",device_->SetVertexShader(nullptr));set("SetFVF-depth-copy",device_->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        set("SetStreamSourceFreq-depth-copy",device_->SetStreamSourceFreq(0,1));
        set("SetTextureStageState-depth-copy",device_->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));
        set("SetTextureStageState-depth-copy",device_->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
        set("SetPixelShader-depth-copy",device_->SetPixelShader(shader.p));set("SetTexture-depth-copy",device_->SetTexture(0,source));
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})set("SetSamplerState-depth-copy",device_->SetSamplerState(0,key,D3DTEXF_POINT));
        for(auto key:{D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MIPMAPLODBIAS})set("SetSamplerState-depth-copy",device_->SetSamplerState(0,key,0));
        for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})set("SetSamplerState-depth-copy",device_->SetSamplerState(0,key,D3DTADDRESS_CLAMP));
        for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_DITHERENABLE,D3DRS_WRAP0})set("SetRenderState-depth-copy",device_->SetRenderState(key,FALSE));
        set("SetRenderState-depth-copy",device_->SetRenderState(D3DRS_COLORWRITEENABLE,15));set("SetRenderState-depth-copy",device_->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        set("SetRenderState-depth-copy",device_->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));set("SetRenderState-depth-copy",device_->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
        D3DVIEWPORT9 copyViewport={0,0,desc.Width,desc.Height,0,1};set("SetViewport-depth-copy",device_->SetViewport(&copyViewport));
        if(nameStencil){
            set("Clear-name-mask-copy",device_->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            set("SetDepthStencilSurface-name-mask-copy",device_->SetDepthStencilSurface(nameStencil));
            const D3DRENDERSTATETYPE keys[]={D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,
                D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,D3DRS_TWOSIDEDSTENCILMODE,D3DRS_MULTISAMPLEANTIALIAS};
            const DWORD values[]={TRUE,D3DCMP_EQUAL,0x80,0x80,0,D3DSTENCILOP_KEEP,D3DSTENCILOP_KEEP,D3DSTENCILOP_KEEP,FALSE,TRUE};
            for(unsigned i=0;i<std::size(keys);++i)set("SetRenderState-name-mask-copy",device_->SetRenderState(keys[i],values[i]));
        }
        struct Vertex{float x,y,z,w,u,v;};const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(desc.Width)-.5f,-.5f,0,1,1,0},{-.5f,float(desc.Height)-.5f,0,1,0,1},{float(desc.Width)-.5f,float(desc.Height)-.5f,0,1,1,1}};
        if(ok)ok=call("DrawPrimitiveUP-depth-copy",device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
        if(ok)ok=surface(name,target.p);
        if(changed){
            auto restore=[&](const char* operation,HRESULT hr){const bool success=call(operation,hr);ok=success&&ok;};
            restore("restore-SetDepthStencilSurface",device_->SetDepthStencilSurface(nullptr));
            for(unsigned i=1;i<4;++i)restore("restore-SetRenderTarget",device_->SetRenderTarget(i,nullptr));
            restore("restore-SetRenderTarget",device_->SetRenderTarget(0,targets[0].p));
            for(unsigned i=1;i<4;++i)restore("restore-SetRenderTarget",device_->SetRenderTarget(i,targets[i].p));
            restore("restore-SetDepthStencilSurface",device_->SetDepthStencilSurface(oldDepth.p));restore("restore-StateBlockApply",state.p->Apply());
            // Native fixed-function vertex submission may clear registers not
            // reliably restored by ALL on every supported D3D9 implementation.
            restore("restore-VertexConstants",device_->SetVertexShaderConstantF(0,vertex,vertexCount));restore("restore-PixelConstants",device_->SetPixelShaderConstantF(0,pixel,224));
            restore("restore-Viewport",device_->SetViewport(&viewport));
        }
        return ok;
    }
    void shader(IDirect3DPixelShader9* source)noexcept{
        for(unsigned i=0;i<shaderCount_;++i)if(shaders_[i]==source){std::fprintf(journal_,"{\"event\":\"shader-binding\",\"pass\":%u,\"shader\":%u}\n",passes_,i);return;}
        if(!source||shaderCount_==shaders_.size()){unavailable("pixel-shader","missing-or-shader-limit");return;}
        UINT size=0;if(!call("GetFunction-size",source->GetFunction(nullptr,&size)))return;
        if(!size||size>64u*1024u||size%4){unavailable("pixel-shader","unsupported-bytecode-size");return;}
        try{std::vector<unsigned char> code(size);if(!call("GetFunction-bytes",source->GetFunction(code.data(),&size)))return;
            if(size!=code.size()){unavailable("pixel-shader","bytecode-size-changed");return;}
            char name[80];std::snprintf(name,sizeof name,"lighting-shader-%u",shaderCount_);
            if(bytes(name,"bin",code.data(),code.size())){std::fprintf(journal_,"{\"event\":\"shader-binding\",\"pass\":%u,\"shader\":%u}\n",passes_,shaderCount_);shaders_[shaderCount_++]=source;}
        }catch(...){unavailable("pixel-shader","allocation-failed");}
    }
public:
    Capture(IDirect3DDevice9* device,const std::string& directory,unsigned capture,unsigned frame,unsigned rendererFrame,unsigned width,unsigned height,const char* journalName="lighting-inputs")noexcept
        :device_(device),directory_(&directory),journalName_(journalName),capture_(capture),frame_(frame),rendererFrame_(rendererFrame){
        char output[2048];if(device_&&!directory.empty()&&path(journalName_,"jsonl.tmp",output))journal_=NorthlightFiles::open(output,"wb");
        logf("WORLD LIGHT_CAPTURE begin capture=%u worldFrame=%u rendererFrame=%u rendererFrameKnown=%u normalView=1 diagnostic=1 fpsEligible=0 saved=%u",capture_,frame_,rendererFrame_,unsigned(rendererFrame_!=0),unsigned(journal_!=nullptr));
        if(journal_)std::fprintf(journal_,"{\"event\":\"begin\",\"schema\":1,\"capture\":%u,\"world_frame\":%u,\"renderer_frame\":%u,\"renderer_frame_known\":%s,\"width\":%u,\"height\":%u,\"normal_view\":true,\"scope\":\"%s\",\"constant_register_count\":%u,\"per_source_bank_missing\":false,\"depth_reconstructed\":false,\"source_physical_ownership_proven\":false,\"material_replay_complete\":false,\"whole_scene_replay\":false,\"replay_execution_verified\":false,\"per_sample_color_exported\":false,\"stencil_image_exported\":false,\"scene_boundary_changed\":false,\"fps_eligible\":false}\n",capture_,frame_,rendererFrame_,rendererFrame_?"true":"false",width,height,std::strcmp(journalName_,"weather-inputs")==0?"weather-inputs":std::strcmp(journalName_,"native-inputs")==0?"native-receiver-inputs":"local-light-contribution-inputs",std::strcmp(journalName_,"weather-inputs")==0?10u:116u);
    }
    ~Capture(){if(journal_)finish(false);}
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    bool active()const noexcept{return journal_!=nullptr&&!finished_;}
    unsigned nativeDraws_=0,nativeImages_=0,nativeSkipped_=0;
    template<class Scene> void nativeScene(const Scene& scene)noexcept{
        if(!active())return;
        std::fprintf(journal_,"{\"event\":\"native-scene\",\"valid\":%u,\"count\":%u,\"scene\":%u,\"time\":%u,\"point_stride\":%zu,\"snapshot_bytes\":%zu}\n",scene.valid,scene.count,scene.scene,scene.time,sizeof scene.lights[0],sizeof scene);
        bytes("native-scene","bin",&scene,sizeof scene);
        for(unsigned i=0;i<scene.count&&i<scene.lights.size();++i){const auto& p=scene.lights[i];
            std::fprintf(journal_,"{\"event\":\"native-emitter\",\"id\":%llu,\"fixture\":%u,\"flags\":%u,\"raw\":[%.9g,%.9g,%.9g],\"resolved\":[%.9g,%.9g,%.9g]}\n",
                (unsigned long long)p.id,p.reserved,p.flags,p.position[0],p.position[1],p.position[2],p.position[0]+p.emitterOffset[0],p.position[1]+p.emitterOffset[1],p.position[2]+p.emitterOffset[2]);}
    }
    void nativeImage(const char* name)noexcept{
        Ref<IDirect3DSurface9> source,copy;D3DSURFACE_DESC desc{};
        if(!call("GetRenderTarget-native",device_->GetRenderTarget(0,&source.p))||!source.p||!call("GetDesc-native",source.p->GetDesc(&desc)))return;
        // MSAA resolves require the complete surface. Reuse one bounded GPU
        // target, then crop before staging; never stage a full 4K native image.
        IDirect3DSurface9* input=source.p;
        if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
            const unsigned stride=pixelBytes(desc.Format);
            if(!stride||!desc.Width||!desc.Height||uint64_t(desc.Width)*desc.Height*stride>64u*1024u*1024u){unavailable(name,"native-resolve-size-or-format");return;}
            if(nativeResolve_.p&&(nativeResolveDesc_.Width!=desc.Width||nativeResolveDesc_.Height!=desc.Height||nativeResolveDesc_.Format!=desc.Format)){nativeResolve_.p->Release();nativeResolve_.p=nullptr;}
            if(!nativeResolve_.p){
                if(!call("CreateRenderTarget-native-resolve",device_->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&nativeResolve_.p,nullptr))||!nativeResolve_.p)return;
                nativeResolveDesc_=desc;
            }
            if(!call("StretchRect-native-resolve",device_->StretchRect(source.p,nullptr,nativeResolve_.p,nullptr,D3DTEXF_NONE)))return;
            input=nativeResolve_.p;
        }
        const unsigned width=std::min(768u,desc.Width),height=std::min(768u,desc.Height);
        const LONG x=LONG((desc.Width-width)/2),y=LONG(std::min(desc.Height-height,desc.Height*7/10-height/2));
        const RECT rect{x,y,x+LONG(width),y+LONG(height)};
        std::fprintf(journal_,"{\"event\":\"native-image-roi\",\"name\":\"%s\",\"rect\":[%ld,%ld,%ld,%ld],\"source_width\":%u,\"source_height\":%u,\"source_samples\":%u,\"msaa_resolved\":%s,\"filter\":\"none\",\"per_sample_color_exported\":false}\n",name,rect.left,rect.top,rect.right,rect.bottom,desc.Width,desc.Height,unsigned(desc.MultiSampleType),desc.MultiSampleType!=D3DMULTISAMPLE_NONE?"true":"false");
        if(!call("CreateRenderTarget-native-roi",device_->CreateRenderTarget(width,height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&copy.p,nullptr))||!copy.p)return;
        if(call("StretchRect-native-roi",device_->StretchRect(input,&rect,copy.p,nullptr,D3DTEXF_NONE)))surface(name,copy.p,false);
    }
    // This census is deliberately independent of receiver classification. A
    // rejected/unknown shader is often exactly the draw being investigated.
    // Thumbnails locate a contribution; they are not a geometry replay.
    void traceImage(unsigned draw,const char* stage)noexcept{
        Ref<IDirect3DSurface9> source;D3DSURFACE_DESC desc{};
        if(!call("GetRenderTarget-trace",device_->GetRenderTarget(0,&source.p))||!source.p||!call("GetDesc-trace",source.p->GetDesc(&desc)))return;
        if(!pixelBytes(desc.Format)||!desc.Width||!desc.Height){unavailable("native-trace","target-format");return;}
        IDirect3DSurface9* input=source.p;
        if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
            if(nativeResolve_.p&&(nativeResolveDesc_.Width!=desc.Width||nativeResolveDesc_.Height!=desc.Height||nativeResolveDesc_.Format!=desc.Format)){nativeResolve_.p->Release();nativeResolve_.p=nullptr;}
            if(!nativeResolve_.p){
                if(!call("CreateRenderTarget-trace-resolve",device_->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&nativeResolve_.p,nullptr))||!nativeResolve_.p)return;
                nativeResolveDesc_=desc;
            }
            if(!call("StretchRect-trace-resolve",device_->StretchRect(source.p,nullptr,nativeResolve_.p,nullptr,D3DTEXF_NONE)))return;
            input=nativeResolve_.p;
        }
        const unsigned w=std::min(384u,desc.Width),h=std::max(1u,unsigned(uint64_t(desc.Height)*w/desc.Width));
        if(h>384){unavailable("native-trace","portrait-target");return;}
        if(traceTarget_.p&&(traceDesc_.Width!=w||traceDesc_.Height!=h||traceDesc_.Format!=desc.Format)){traceTarget_.p->Release();traceTarget_.p=nullptr;}
        if(!traceTarget_.p){
            if(!call("CreateRenderTarget-trace",device_->CreateRenderTarget(w,h,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&traceTarget_.p,nullptr))||!traceTarget_.p)return;
            traceDesc_=desc;traceDesc_.Width=w;traceDesc_.Height=h;
        }
        if(!call("StretchRect-trace-thumbnail",device_->StretchRect(input,nullptr,traceTarget_.p,nullptr,D3DTEXF_POINT)))return;
        char name[80];std::snprintf(name,sizeof name,"native-trace-%u-%s",draw,stage);surface(name,traceTarget_.p,false);
    }
    unsigned traceBefore(unsigned rendererFrame,unsigned primitives)noexcept{
        if(!active())return 0;
        if(traceDraws_>=2048){if(!traceSkipped_++)unavailable("native-trace","draw-limit");return 0;}
        const unsigned draw=++traceDraws_;char name[96];
        std::fprintf(journal_,"{\"event\":\"native-trace\",\"draw\":%u,\"renderer_frame\":%u,\"primitives\":%u,\"receiver_classification_required\":false}\n",draw,rendererFrame,primitives);
        try{
            Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps;
            call("GetVertexShader-trace",device_->GetVertexShader(&vs.p));call("GetPixelShader-trace",device_->GetPixelShader(&ps.p));
            auto shader=[&](auto* p,const char* stage){
                std::fprintf(journal_,"{\"event\":\"native-trace-program\",\"draw\":%u,\"stage\":\"%s\",\"present\":%s}\n",draw,stage,p?"true":"false");
                if(!p)return;UINT size=0;
                if(!call("GetFunction-trace-size",p->GetFunction(nullptr,&size))||size<8||size>65536){unavailable("native-trace","shader-size");return;}
                std::vector<unsigned char> code(size);if(call("GetFunction-trace",p->GetFunction(code.data(),&size))){std::snprintf(name,sizeof name,"native-trace-%u-%s",draw,stage);bytes(name,"bin",code.data(),size);}
            };
            shader(vs.p,"vs");shader(ps.p,"ps");
            DWORD fvf=0;if(call("GetFVF-trace",device_->GetFVF(&fvf)))std::fprintf(journal_,"{\"event\":\"native-trace-fvf\",\"draw\":%u,\"value\":%lu}\n",draw,(unsigned long)fvf);
            Ref<IDirect3DVertexDeclaration9> declaration;
            if(call("GetVertexDeclaration-trace",device_->GetVertexDeclaration(&declaration.p))&&declaration.p){D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1];UINT count=MAXD3DDECLLENGTH+1;
                if(call("GetDeclaration-trace",declaration.p->GetDeclaration(elements,&count))){std::snprintf(name,sizeof name,"native-trace-%u-declaration",draw);bytes(name,"bin",elements,count*sizeof *elements);}}
            if(!vs.p){
                D3DMATRIX transforms[3];const D3DTRANSFORMSTATETYPE states[]={D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION};
                bool valid=true;for(unsigned i=0;i<3;++i)valid=call("GetTransform-trace",device_->GetTransform(states[i],&transforms[i]))&&valid;
                if(valid){std::snprintf(name,sizeof name,"native-trace-%u-transforms",draw);bytes(name,"f32",transforms,sizeof transforms);}
                D3DMATERIAL9 material{};if(call("GetMaterial-trace",device_->GetMaterial(&material))){std::snprintf(name,sizeof name,"native-trace-%u-material",draw);bytes(name,"bin",&material,sizeof material);}
                for(unsigned i=0;i<8;++i){BOOL enabled=FALSE;D3DLIGHT9 light{};const HRESULT enableHr=device_->GetLightEnable(i,&enabled),lightHr=device_->GetLight(i,&light);
                    std::fprintf(journal_,"{\"event\":\"native-trace-fixed-light\",\"draw\":%u,\"index\":%u,\"enabled\":%s,\"enable_HRESULT\":\"%08lx\",\"light_HRESULT\":\"%08lx\"}\n",draw,i,enabled?"true":"false",(unsigned long)enableHr,(unsigned long)lightHr);
                    if(SUCCEEDED(lightHr)){std::snprintf(name,sizeof name,"native-trace-%u-light-%u",draw,i);bytes(name,"bin",&light,sizeof light);}}
            }
            float vertex[256][4],pixel[224][4];D3DCAPS9 caps{};
            if(call("GetDeviceCaps-trace",device_->GetDeviceCaps(&caps))){const unsigned count=std::min(256u,unsigned(caps.MaxVertexShaderConst));
                if(count&&call("GetVertexShaderConstantF-trace",device_->GetVertexShaderConstantF(0,vertex[0],count))){std::snprintf(name,sizeof name,"native-trace-%u-vs-constants",draw);bytes(name,"f32",vertex,count*sizeof vertex[0]);}}
            if(call("GetPixelShaderConstantF-trace",device_->GetPixelShaderConstantF(0,pixel[0],224))){std::snprintf(name,sizeof name,"native-trace-%u-ps-constants",draw);bytes(name,"f32",pixel,sizeof pixel);}
            for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_COLORWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,D3DRS_CULLMODE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_LIGHTING,D3DRS_COLORVERTEX,D3DRS_AMBIENT,D3DRS_DIFFUSEMATERIALSOURCE,D3DRS_EMISSIVEMATERIALSOURCE,D3DRS_AMBIENTMATERIALSOURCE}){
                DWORD value=0;if(call("GetRenderState-trace",device_->GetRenderState(key,&value)))std::fprintf(journal_,"{\"event\":\"native-trace-state\",\"draw\":%u,\"state\":%u,\"value\":%lu}\n",draw,unsigned(key),(unsigned long)value);}
            traceImage(draw,"before");
        }catch(...){unavailable("native-trace","allocation-failed");}
        return draw;
    }
    void traceAfter(unsigned token,HRESULT result)noexcept{
        if(!active()||!token)return;
        std::fprintf(journal_,"{\"event\":\"native-trace-result\",\"draw\":%u,\"HRESULT\":\"%08lx\"}\n",token,(unsigned long)result);
        if(FAILED(result))complete_=false;traceImage(token,"after");
    }
    unsigned nativeBefore(unsigned rendererFrame,uint64_t source,unsigned slot,bool prepared,bool focus,IDirect3DVertexShader9* originalVS,IDirect3DPixelShader9* originalPS,const uint64_t (&sources)[4],bool allowImages=true)noexcept{
        if(!active())return 0;
        if(nativeDraws_>=128||(!focus&&nativeDraws_>=32)){++nativeSkipped_;return 0;}
        const unsigned draw=++nativeDraws_;char name[96];
        std::fprintf(journal_,"{\"event\":\"native-draw\",\"draw\":%u,\"renderer_frame\":%u,\"source_id\":%llu,\"native_slot\":%u,\"prepared_geometry\":%s,\"focus_source\":%s}\n",draw,rendererFrame,(unsigned long long)source,slot,prepared?"true":"false",focus?"true":"false");
        std::fprintf(journal_,"{\"event\":\"native-sources\",\"draw\":%u,\"source_ids\":[%llu,%llu,%llu,%llu]}\n",draw,(unsigned long long)sources[0],(unsigned long long)sources[1],(unsigned long long)sources[2],(unsigned long long)sources[3]);
        try{
            Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps;
            call("GetVertexShader-native",device_->GetVertexShader(&vs.p));call("GetPixelShader-native",device_->GetPixelShader(&ps.p));
            auto shader=[&](auto* p,const char* stage){if(!p){unavailable(stage,"missing-shader");return;}UINT size=0;if(!call("GetFunction-native-size",p->GetFunction(nullptr,&size))||size<8||size>65536){unavailable(stage,"shader-size");return;}std::vector<unsigned char> code(size);if(call("GetFunction-native",p->GetFunction(code.data(),&size))){std::snprintf(name,sizeof name,"native-draw-%u-%s",draw,stage);bytes(name,"bin",code.data(),size);}};
            shader(vs.p,"vs");shader(ps.p,"ps");shader(originalVS,"original-vs");shader(originalPS,"original-ps");
            float vertex[256][4],pixel[224][4];
            if(call("GetVertexShaderConstantF-native",device_->GetVertexShaderConstantF(0,vertex[0],256))){std::snprintf(name,sizeof name,"native-draw-%u-vs-constants",draw);bytes(name,"f32",vertex,sizeof vertex);}
            if(call("GetPixelShaderConstantF-native",device_->GetPixelShaderConstantF(0,pixel[0],224))){std::snprintf(name,sizeof name,"native-draw-%u-ps-constants",draw);bytes(name,"f32",pixel,sizeof pixel);}
            Ref<IDirect3DVertexDeclaration9> declaration;if(call("GetVertexDeclaration-native",device_->GetVertexDeclaration(&declaration.p))&&declaration.p){D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1];UINT count=MAXD3DDECLLENGTH+1;if(call("GetDeclaration-native",declaration.p->GetDeclaration(elements,&count))){std::snprintf(name,sizeof name,"native-draw-%u-declaration",draw);bytes(name,"bin",elements,count*sizeof *elements);}}
            std::fprintf(journal_,"{\"event\":\"native-programs\",\"draw\":%u,\"private_vs\":%s,\"private_ps\":%s}\n",draw,vs.p!=originalVS?"true":"false",ps.p!=originalPS?"true":"false");
            if(focus&&allowImages&&nativeImages_<8){++nativeImages_;std::snprintf(name,sizeof name,"native-draw-%u-before",draw);nativeImage(name);return draw|0x80000000u;}
        }catch(...){unavailable("native-draw","allocation-failed");}
        return draw;
    }
    void nativeSlot(unsigned token,unsigned count,unsigned slot,bool decoded,const float* position,const float* color,const float* attenuation,const char* reason,uint64_t source=0,int physicalSlot=-1)noexcept{
        if(!active()||!token||count>4||slot>=count)return;
        std::fprintf(journal_,"{\"event\":\"native-slot\",\"draw\":%u,\"native_slot_count\":%u,\"native_slot\":%u,\"decoded\":%s,\"status\":\"%s\",\"candidate_source_id\":\"%llu\",\"point_slot\":%d,\"position\":[%.9g,%.9g,%.9g],\"rgb\":[%.9g,%.9g,%.9g],\"attenuation\":[%.9g,%.9g,%.9g]}\n",token&0x7fffffffu,count,slot,decoded?"true":"false",reason,(unsigned long long)source,physicalSlot,position[0],position[1],position[2],color[0],color[1],color[2],attenuation[0],attenuation[1],attenuation[2]);
    }
    void directional(unsigned slot,bool sampled,char action,const float* matrix,const void* texture,unsigned productSource,bool reusable,unsigned committedPass,uint64_t cacheGeneration,uint64_t cacheSignature)noexcept{
        if(!active()||slot>=4)return;
        std::fprintf(journal_,"{\"event\":\"directional-map\",\"slot\":%u,\"source\":%u,\"cascade\":%u,\"sampled\":%s,\"action\":\"%c\",\"texture_identity\":\"%p\",\"product\":\"%s\",\"reuse_valid\":%s,\"reuse_committed_pass\":%u,\"static_cache_generation\":\"%llu\",\"static_cache_signature\":\"%llu\",\"working_content_proven\":false,\"receiver_matrix\":[",slot,slot/2,slot%2,sampled?"true":"false",action,texture,slot/2==productSource?(slot%2?"shadow-far":"shadow-near"):"",reusable?"true":"false",committedPass,(unsigned long long)cacheGeneration,(unsigned long long)cacheSignature);
        for(unsigned i=0;i<16;++i)std::fprintf(journal_,"%s%.9g",i?",":"",matrix[i]);std::fprintf(journal_,"]}\n");
    }
    void nativeAfter(unsigned token,unsigned corrected,HRESULT result,const char* reason=nullptr,unsigned coldDelta=0,unsigned deferredDelta=0,uint64_t fallbackDelta=0,bool rendererObserved=false)noexcept{
        if(!active()||!token)return;const unsigned draw=token&0x7fffffffu;
        std::fprintf(journal_,"{\"event\":\"native-result\",\"draw\":%u,\"corrected_sources\":%u,\"reason\":\"%s\",\"HRESULT\":\"%08lx\",\"renderer_counters_observed\":%s,\"cold_pairs_delta\":%u,\"deferred_pairs_delta\":%u,\"fallbacks_delta\":%llu,\"fallback_cause_proven\":false}\n",draw,corrected,reason?reason:corrected?"corrected":"endpoint-unsupported-or-cold",(unsigned long)result,rendererObserved?"true":"false",coldDelta,deferredDelta,(unsigned long long)fallbackDelta);
        if(token&0x80000000u){char name[96];std::snprintf(name,sizeof name,"native-draw-%u-after",draw);nativeImage(name);}
    }
    void image(const char* name,IDirect3DSurface9* value)noexcept{if(active())surface(name,value);}
    bool imageSaved(const char* name,IDirect3DSurface9* value)noexcept{return active()&&surface(name,value);}
    template<class Slots> void pointSources(const Slots& slots,uint32_t now,bool enabled,bool nativeScene=false)noexcept{
        if(!active())return;
        if(slots.size()>128){unavailable("point-sources","source-limit");return;}
        std::fprintf(journal_,"{\"event\":\"point-source-stage\",\"stage\":\"%s\"}\n",nativeScene?"native-scene-before-refresh":"point-correction-after-refresh");
        for(unsigned i=0;i<slots.size();++i){const auto& p=slots[i];if(!p.cube&&!p.selected.sourceId)continue;
            const auto& l=p.selected;
            std::fprintf(journal_,"{\"event\":\"point-source\",\"slot\":%u,\"source_id\":\"%llu\",\"enabled\":%s,\"ready\":%s,\"usable\":%s,\"age_ms\":%u,\"gain\":%.9g,\"position\":[%.9g,%.9g,%.9g],\"range\":%.9g,\"static_valid\":%s,\"actor_admission_proven\":false}\n",i,(unsigned long long)l.sourceId,enabled?"true":"false",p.ready?"true":"false",p.schedule.usable?"true":"false",uint32_t(now-p.schedule.updatedAt),p.selectionGain,l.position[0],l.position[1],l.position[2],l.attenuationEnd,p.cacheValid?"true":"false");
            std::fprintf(journal_,"{\"event\":\"point-publication\",\"slot\":%u,\"cube_identity\":\"%p\",\"schedule_source_id\":\"%llu\",\"schedule_generation\":\"%llu\",\"schedule_complete\":%s,\"cache_source_id\":\"%llu\",\"static_only_faces\":%u,\"map\":\"",i,static_cast<void*>(p.cube),(unsigned long long)p.schedule.light.sourceId,(unsigned long long)p.schedule.generation,p.schedule.complete?"true":"false",(unsigned long long)p.cacheSource,p.staticOnlyFaces);
            for(unsigned char c:p.map){if(c=='"'||c=='\\')std::fputc('\\',journal_);if(c<32)std::fprintf(journal_,"\\u%04x",unsigned(c));else std::fputc(c,journal_);}std::fprintf(journal_,"\",\"schedule_position\":[%.9g,%.9g,%.9g],\"schedule_range\":[%.9g,%.9g],\"selected_rgb\":[%.9g,%.9g,%.9g],\"faces\":[",p.schedule.light.position[0],p.schedule.light.position[1],p.schedule.light.position[2],p.schedule.light.attenuationStart,p.schedule.light.attenuationEnd,l.diffuse[0],l.diffuse[1],l.diffuse[2]);
            for(unsigned face=0;face<6;++face)std::fprintf(journal_,"%s{\"face\":%u,\"static_known\":%s,\"static_serial\":\"%llu\",\"static_content\":[\"%llu\",\"%llu\"],\"static_triangles\":\"%llu\"}",face?",":"",face,p.faceKnown[face]?"true":"false",(unsigned long long)p.faceSerial[face],(unsigned long long)p.faceContent[face].words[0],(unsigned long long)p.faceContent[face].words[1],(unsigned long long)p.faceContent[face].triangles);
            std::fprintf(journal_,"]}\n");
            if(!enabled||!p.ready)continue;
            if(!p.cube){unavailable("point-cube","ready-source-without-cube");continue;}
            for(unsigned face=0;face<6;++face){char name[96];Ref<IDirect3DSurface9> value;
                std::snprintf(name,sizeof name,"%spoint-%u-face-%u",nativeScene?"native-":"",i,face);
                if(call("GetCubeMapSurface-point",p.cube->GetCubeMapSurface(D3DCUBEMAP_FACES(face),0,&value.p)))surface(name,value.p);
                if(!nativeScene&&p.cacheValid&&p.cacheSurface[face]){std::snprintf(name,sizeof name,"point-%u-static-%u",i,face);surface(name,p.cacheSurface[face]);}
            }
        }
    }
    void pointPass(unsigned slot,bool soft,IDirect3DSurface9* resolved,HRESULT result)noexcept{
        if(!active())return;
        if(slot>=128){unavailable("point-pass","source-limit");return;}
        constexpr unsigned inputsMask=(1u<<0)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5);
        inputs(4,inputsMask,inputsMask);
        const bool cubeInput=!resolved;const unsigned mask=inputsMask|(cubeInput?(1u<<1):0);
        if(cubeInput){
            Ref<IDirect3DBaseTexture9> bound;
            if(call("GetTexture-point-cube",device_->GetTexture(1,&bound.p))&&bound.p&&bound.p->GetType()==D3DRTYPE_CUBETEXTURE){
                auto& input=groups_[4][1];input.identity=bound.p;input.known=true;input.snapshot=4;
                // pointSources already exported the six exact faces of this slot.
                std::fprintf(journal_,"{\"event\":\"input-binding\",\"group\":4,\"slot\":1,\"null\":false,\"snapshot_group\":4,\"texture_type\":\"cube\",\"file_base\":\"point-%u\",\"faces\":6}\n",slot);
            }else unavailable("point-cube","missing-cube-binding");
        }
        if(soft&&resolved){char name[64];std::snprintf(name,sizeof name,"point-%u-resolved",slot);surface(name,resolved);}
        pass(soft?(cubeInput?"LocalLightingPcss":"LocalLightingSoft"):"LocalLighting",4,mask,slot,1,SUCCEEDED(result),result);
    }
    bool sampledDepthSaved(const char* name,IDirect3DTexture9* value)noexcept{
        D3DSURFACE_DESC desc{};return active()&&value&&call("GetLevelDesc-sampled-depth",value->GetLevelDesc(0,&desc))&&depth(name,value,desc);
    }
    bool nameStencilSaved(const char* name,IDirect3DSurface9* value)noexcept{
        D3DSURFACE_DESC desc{};return active()&&value&&call("GetDesc-name-stencil",value->GetDesc(&desc))&&depth(name,nullptr,desc,value);
    }
    void sources(const NorthlightLocalLightSelection::Selection& selected,bool fogEnabled)noexcept{
        if(!active())return;
        if(selected.count>selected.sources.size()){unavailable("selection","source-limit");return;}
        std::fprintf(journal_,"{\"event\":\"selection\",\"count\":%u,\"fog_enabled\":%s,\"fog_distance\":%.9g}\n",selected.count,fogEnabled?"true":"false",selected.fogDistance);
        for(unsigned i=0;i<selected.count;++i){const auto& l=selected.sources[i];
            std::fprintf(journal_,"{\"event\":\"source\",\"slot\":%u,\"source_id\":\"%llu\",\"kind\":%u,\"flags\":%u,\"position\":[%.9g,%.9g,%.9g],\"rgb\":[%.9g,%.9g,%.9g],\"range\":[%.9g,%.9g],\"sigma\":%.9g,\"physical_ownership_proven\":false}\n",i,(unsigned long long)l.sourceId,l.kind,l.flags,l.position[0],l.position[1],l.position[2],l.diffuse[0],l.diffuse[1],l.diffuse[2],l.attenuationStart,l.attenuationEnd,selected.fog[i][0]);}
    }
    void inputs(unsigned group,unsigned mask,unsigned immutableMask=0)noexcept{
        if(!active())return;if(group>=groups_.size()){unavailable("inputs","group-limit");return;}
        for(unsigned slot=0;slot<16;++slot)if(mask&(1u<<slot)){
            Ref<IDirect3DBaseTexture9> source;if(!call("GetTexture-input",device_->GetTexture(slot,&source.p)))continue;
            auto& input=groups_[group][slot];const bool existing=input.known&&input.identity==source.p&&(immutableMask&(1u<<slot));
            if(!existing){input.identity=source.p;input.known=true;input.snapshot=group;}
            if(immutableMask&(1u<<slot))for(unsigned previous=0;previous<group;++previous){const auto& old=groups_[previous][slot];if(old.known&&old.identity==source.p){input.snapshot=old.snapshot;break;}}
            char name[80];if(input.snapshot==0&&slot==1)std::snprintf(name,sizeof name,"depth");else std::snprintf(name,sizeof name,"lighting-input-g%u-s%u",input.snapshot,slot);
            std::fprintf(journal_,"{\"event\":\"input-binding\",\"group\":%u,\"slot\":%u,\"null\":%s,\"snapshot_group\":%u,\"file_base\":\"%s\"}\n",group,slot,source.p?"false":"true",input.snapshot,source.p?name:"");
            if(!source.p||input.snapshot!=group||existing)continue;
            if(source.p->GetType()!=D3DRTYPE_TEXTURE){unavailable("input","unsupported-texture-type");continue;}
            auto* texture=static_cast<IDirect3DTexture9*>(source.p);D3DSURFACE_DESC desc{};
            if(!call("GetLevelDesc-input",texture->GetLevelDesc(0,&desc)))continue;
            if(texture->GetLevelCount()!=1){unavailable("input","mip-chain-not-exported");continue;}
            const bool sampledDepth=desc.Format==D3DFORMAT(MAKEFOURCC('I','N','T','Z'))||desc.Format==D3DFORMAT(MAKEFOURCC('D','F','2','4'));
            if(sampledDepth){std::fprintf(journal_,"{\"event\":\"sampled-depth\",\"group\":%u,\"slot\":%u,\"source_format\":%u,\"export_format\":%u,\"depth_reconstructed\":false}\n",group,slot,unsigned(desc.Format),unsigned(D3DFMT_R32F));depth(name,texture,desc);}
            else{Ref<IDirect3DSurface9> level;if(call("GetSurfaceLevel-input",texture->GetSurfaceLevel(0,&level.p)))surface(name,level.p);}
        }
    }
    void skipped(const char* entry,unsigned first,unsigned count,const char* reason)noexcept{
        if(active())std::fprintf(journal_,"{\"event\":\"skipped\",\"entry\":\"%s\",\"first\":%u,\"count\":%u,\"reason\":\"%s\",\"bank_present\":false}\n",entry,first,count,reason);
    }
    void pass(const char* entry,unsigned group,unsigned mask,unsigned first,unsigned count,bool submitted,HRESULT result)noexcept{
        if(!active())return;if(++passes_>256){unavailable("pass","pass-limit");return;}
        std::fprintf(journal_,"{\"event\":\"pass\",\"index\":%u,\"world_frame\":%u,\"renderer_frame\":%u,\"entry\":\"%s\",\"input_group\":%u,\"first\":%u,\"count\":%u,\"submitted\":%s,\"draw_HRESULT\":\"%08lx\",\"vertex_contract\":\"world-quad-XYZRHW-TEX1\"}\n",passes_,frame_,rendererFrame_,entry,group,first,count,submitted?"true":"false",(unsigned long)result);
        if(FAILED(result))complete_=false;
        float bank[116][4];if(call("GetPixelShaderConstantF-pass",device_->GetPixelShaderConstantF(0,bank[0],116))){char name[80];std::snprintf(name,sizeof name,"lighting-pass-%u-constants",passes_);bytes(name,"f32",bank,sizeof bank);}
        Ref<IDirect3DPixelShader9> ps;if(call("GetPixelShader-pass",device_->GetPixelShader(&ps.p)))shader(ps.p);
        Ref<IDirect3DVertexShader9> vs;if(call("GetVertexShader-pass",device_->GetVertexShader(&vs.p))&&vs.p)unavailable("vertex-contract","unexpected-programmable-vertex-shader");
        DWORD fvf=0;if(call("GetFVF-pass",device_->GetFVF(&fvf))&&fvf!=(D3DFVF_XYZRHW|D3DFVF_TEX1))unavailable("vertex-contract","unexpected-FVF");
        UINT frequency=0;if(call("GetStreamSourceFreq-pass",device_->GetStreamSourceFreq(0,&frequency))&&frequency!=1)unavailable("vertex-contract","unexpected-stream-frequency");
        D3DVIEWPORT9 viewport{};RECT rect{};if(call("GetViewport-pass",device_->GetViewport(&viewport)))std::fprintf(journal_,"{\"event\":\"viewport\",\"pass\":%u,\"value\":[%lu,%lu,%lu,%lu,%.9g,%.9g]}\n",passes_,(unsigned long)viewport.X,(unsigned long)viewport.Y,(unsigned long)viewport.Width,(unsigned long)viewport.Height,viewport.MinZ,viewport.MaxZ);
        if(call("GetScissorRect-pass",device_->GetScissorRect(&rect)))std::fprintf(journal_,"{\"event\":\"scissor\",\"pass\":%u,\"value\":[%ld,%ld,%ld,%ld]}\n",passes_,rect.left,rect.top,rect.right,rect.bottom);
        for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,D3DRS_BLENDFACTOR,D3DRS_COLORWRITEENABLE,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,D3DRS_CULLMODE,D3DRS_FILLMODE,D3DRS_CLIPPLANEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_DITHERENABLE,D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,D3DRS_WRAP0}){DWORD value=0;if(call("GetRenderState-pass",device_->GetRenderState(state,&value)))std::fprintf(journal_,"{\"event\":\"render-state\",\"pass\":%u,\"state\":%u,\"value\":%lu}\n",passes_,unsigned(state),(unsigned long)value);}
        for(auto state:{D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS}){DWORD value=0;if(call("GetTextureStageState-pass",device_->GetTextureStageState(0,state,&value)))std::fprintf(journal_,"{\"event\":\"texture-stage-state\",\"pass\":%u,\"slot\":0,\"state\":%u,\"value\":%lu}\n",passes_,unsigned(state),(unsigned long)value);}
        for(unsigned slot=0;slot<16;++slot)if(mask&(1u<<slot)){
            Ref<IDirect3DBaseTexture9> texture;if(call("GetTexture-pass",device_->GetTexture(slot,&texture.p))){if(group>=groups_.size()||!groups_[group][slot].known||groups_[group][slot].identity!=texture.p)unavailable("input","binding-changed-since-snapshot");}
            for(unsigned state=1;state<=13;++state){DWORD value=0;if(call("GetSamplerState-pass",device_->GetSamplerState(slot,D3DSAMPLERSTATETYPE(state),&value)))std::fprintf(journal_,"{\"event\":\"sampler-state\",\"pass\":%u,\"slot\":%u,\"state\":%u,\"value\":%lu}\n",passes_,slot,state,(unsigned long)value);}
        }
        for(unsigned slot=0;slot<4;++slot){Ref<IDirect3DSurface9> target;const HRESULT hr=device_->GetRenderTarget(slot,&target.p);call("GetRenderTarget-pass",hr,slot!=0);
            std::fprintf(journal_,"{\"event\":\"target-binding\",\"pass\":%u,\"slot\":%u,\"present\":%s}\n",passes_,slot,target.p?"true":"false");if(!slot&&!target.p)unavailable("target","missing-target-zero");
            if(SUCCEEDED(hr)&&target.p){D3DSURFACE_DESC desc{};if(call("GetDesc-target-pass",target.p->GetDesc(&desc)))std::fprintf(journal_,"{\"event\":\"target\",\"pass\":%u,\"slot\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"samples\":%u,\"sample_quality\":%u}\n",passes_,slot,desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.MultiSampleType),unsigned(desc.MultiSampleQuality));}}
        Ref<IDirect3DSurface9> ds;const HRESULT dsHr=device_->GetDepthStencilSurface(&ds.p);call("GetDepthStencilSurface-pass",dsHr,true);
        std::fprintf(journal_,"{\"event\":\"depth-stencil-binding\",\"pass\":%u,\"present\":%s,\"stencil_image_exported\":false}\n",passes_,ds.p?"true":"false");
        if(ds.p){D3DSURFACE_DESC desc{};if(call("GetDesc-depth-stencil-pass",ds.p->GetDesc(&desc)))std::fprintf(journal_,"{\"event\":\"depth-stencil\",\"pass\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"samples\":%u,\"sample_quality\":%u}\n",passes_,desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.MultiSampleType),unsigned(desc.MultiSampleQuality));}
    }
    void finish(bool renderCompleted)noexcept{
        if(!active())return;finished_=true;complete_=complete_&&renderCompleted&&std::ferror(journal_)==0;
        if(traceDraws_||traceSkipped_)std::fprintf(journal_,"{\"event\":\"native-trace-summary\",\"draws\":%u,\"skipped\":%u,\"limit\":2048,\"whole_scene_replay\":false,\"thumbnail_filter\":\"point\"}\n",traceDraws_,traceSkipped_);
        if(std::strcmp(journalName_,"native-inputs")==0)std::fprintf(journal_,"{\"event\":\"native-summary\",\"recorded_draws\":%u,\"image_pairs\":%u,\"skipped_draws\":%u,\"record_limit\":128,\"nonfocus_limit\":32,\"image_pair_limit\":8,\"whole_scene_replay\":false}\n",nativeDraws_,nativeImages_,nativeSkipped_);
        std::fprintf(journal_,"{\"event\":\"end\",\"render_completed\":%s,\"export_complete\":%s,\"passes\":%u,\"authentic_source_replay\":false,\"whole_scene_replay\":false,\"fps_eligible\":false}\n",renderCompleted?"true":"false",complete_?"true":"false",passes_);
        if(std::fclose(journal_))complete_=false;journal_=nullptr;
        char from[2048],to[2048],incomplete[128];std::snprintf(incomplete,sizeof incomplete,"%s-incomplete",journalName_);
        if(path(journalName_,"jsonl.tmp",from)&&path(complete_?journalName_:incomplete,"jsonl",to)&&std::rename(from,to))complete_=false;
        logf("WORLD LIGHT_CAPTURE end capture=%u worldFrame=%u rendererFrame=%u rendererFrameKnown=%u diagnostic=1 fpsEligible=0 renderCompleted=%u exportComplete=%u passes=%u authenticSourceReplay=0 wholeSceneReplay=0",capture_,frame_,rendererFrame_,unsigned(rendererFrame_!=0),unsigned(renderCompleted),unsigned(complete_),passes_);
    }
};
}
