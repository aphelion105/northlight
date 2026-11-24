    struct FusedM2Draw {
        NorthlightAutoNormalCache::Lease normal;
        NorthlightDrawSnapshot::Ref<IDirect3DSurface9> color,depth;
        IDirect3DVertexShader9* originalVertex=nullptr;
        IDirect3DVertexShader9* vertex=nullptr;
        const NativeCapture* replay=nullptr;
        D3DVIEWPORT9 viewport{};
        RECT scissor{};
        NorthlightDrawSnapshot::Draw draw;
        float bank[36];
        DWORD addressU=0,addressV=0,zfunc=0;
        bool auxiliary=false,scissorEnabled=false,multisampled=false;
    };
    FusedM2Draw fusedM2;
    std::unordered_map<IDirect3DPixelShader9*,NativeCapture> fusedM2Pixels[2][3];
    bool allowFusedM2=false,checkedFusedM2Caps=false,fusedM2Caps=false;
    unsigned fusedM2Draws=0,fusedM2Fallbacks=0,fusedM2Candidates=0;
    unsigned fusedM2Rejections[5]={};
    IDirect3DSurface9 *fusedM2Samples=nullptr,*fusedM2ResolvedSurface=nullptr,*fusedM2MergedSurface=nullptr;
    IDirect3DTexture9 *fusedM2Resolved=nullptr,*fusedM2Merged=nullptr;
    IDirect3DPixelShader9 *fusedM2ClearPS=nullptr,*fusedM2MergePS=nullptr,*fusedM2PackedMergePS=nullptr;
    NorthlightDrawSnapshot::Ref<IDirect3DSurface9> fusedM2SceneDepth;
    IDirect3DSurface9* fusedM2WorldDepth=nullptr;
    D3DMULTISAMPLE_TYPE fusedM2SampleType=D3DMULTISAMPLE_NONE;
    DWORD fusedM2SampleQuality=0;
    unsigned fusedM2SampleCount=0;
    unsigned fusedM2DepthBase=0;
    float fusedM2Near=0,fusedM2PackedRange=0,fusedM2PackedCavity=0;
    bool fusedM2MsaaDrawn=false,fusedM2MsaaOcclusion=false,fusedM2MsaaComplete=false,fusedM2MsaaUnavailable=false;
    bool fusedM2PackedUnavailable=false;
public:
    void endNativeMaterialDraw(){
        if(fusedM2.normal){fusedM2.normal={};drop(fusedM2.color.p);drop(fusedM2.depth.p);}
    }
    bool finishFusedM2(IDirect3DSurface9* worldDepth,IDirect3DTexture9* depth,float nearZ,float farZ,float minZ,float maxZ){
        if(!fusedM2MsaaDrawn||fusedM2MsaaComplete)return true;
        if(worldDepth!=fusedM2SceneDepth.p||!depth||!(nearZ>0&&farZ>nearZ&&maxZ>minZ))return true;
        SavedState saved(d,&captureStates);
        if(!saved.ok)return true;
        const bool ready=(cleared||SUCCEEDED(d->ColorFill(target,nullptr,0)))&&
            NorthlightM2Samples::resolve(d,fusedM2Samples,fusedM2ResolvedSurface,fusedM2ClearPS,width,heightPixels)&&
            (fusedM2DepthBase?
                NorthlightM2Samples::merge(d,fusedM2MergedSurface,fusedM2Resolved,height,depth,fusedM2PackedMergePS,
                    width,heightPixels,fusedM2SampleCount,nearZ,farZ,minZ,maxZ,float(fusedM2DepthBase),fusedM2PackedCavity):
                NorthlightM2Samples::mergeAndResolve(d,fusedM2Samples,fusedM2ResolvedSurface,fusedM2Resolved,
                    height,depth,fusedM2MergePS,width,heightPixels,fusedM2SampleCount,nearZ,farZ,minZ,maxZ));
        const bool restored=saved.restore();
        if(ready&&restored){
            fusedM2MsaaComplete=drawn=cleared=true;
            occlusionDrawn|=fusedM2MsaaOcclusion;
        }else{
            if(!fusedM2MsaaUnavailable)logf("MATERIAL_M2_MSAA resolve failed; replays restored for subsequent frames");
            fusedM2MsaaUnavailable=true;
        }
        return restored;
    }
private:

    void endFusedM2MsaaFrame(){
        drop(fusedM2SceneDepth.p);
        fusedM2MsaaDrawn=fusedM2MsaaOcclusion=fusedM2MsaaComplete=false;
    }
    void resetFusedM2Msaa(){
        endFusedM2MsaaFrame();
        drop(fusedM2Samples);drop(fusedM2ResolvedSurface);drop(fusedM2Resolved);
        drop(fusedM2MergedSurface);drop(fusedM2Merged);
        fusedM2DepthBase=0;
        fusedM2MsaaUnavailable=false;
    }
    static unsigned packedM2DepthBase(float distance,float nearZ,float cavity){
#ifdef NORTHLIGHT_PROFILE_BUILD
        // The experiment depends on the adapter's FP16 attachment rounding.
        static const bool enabled=[]{const auto* flag=std::getenv("NORTHLIGHT_GAME_M2_PACKED_MSAA");return flag&&std::strcmp(flag,"1")==0;}();
        if(!enabled)return 0;
        if(!(distance>=1&&distance<=96&&nearZ>0)||cavity>.5f)return 0;
        int exponent;
        const float mantissa=std::frexp(distance,&exponent);
        const auto top=unsigned((exponent+14)*1024+int((2*mantissa-1)*1024+.5f));
        const unsigned base=top>17407?top-16383:1024;
        const float minimum=std::ldexp(1.f+(base%1024)/1024.f,int(base/1024)-15);
        // Any depth outside the packed interval would be rejected by the old
        // merge against the closest possible scene depth as well.
        return minimum+std::max(.025f,nearZ*.0015f)+.001f<nearZ?base:0;
#else
        (void)distance;(void)nearZ;(void)cavity;
        return 0;
#endif
    }
    bool createFusedM2Msaa(D3DMULTISAMPLE_TYPE type,DWORD quality,bool packed){
        const auto format=packed?D3DFMT_R32F:D3DFMT_A16B16G16R16F;
        auto texture=[&](D3DFORMAT kind,IDirect3DTexture9*& image,IDirect3DSurface9*& surface){
            return SUCCEEDED(d->CreateTexture(width,heightPixels,1,D3DUSAGE_RENDERTARGET,kind,D3DPOOL_DEFAULT,&image,nullptr))&&
                SUCCEEDED(image->GetSurfaceLevel(0,&surface));
        };
        auto*& merge=packed?fusedM2PackedMergePS:fusedM2MergePS;
        return SUCCEEDED(d->CreateRenderTarget(width,heightPixels,format,type,quality,FALSE,&fusedM2Samples,nullptr))&&
            texture(format,fusedM2Resolved,fusedM2ResolvedSurface)&&
            (!packed||texture(D3DFMT_A16B16G16R16F,fusedM2Merged,fusedM2MergedSurface))&&
            (fusedM2ClearPS||SUCCEEDED(d->CreatePixelShader(kBumpClearMSShader,&fusedM2ClearPS)))&&
            (merge||SUCCEEDED(d->CreatePixelShader(packed?kBumpMergePackedMSShader:kBumpMergeMSShader,&merge)))&&
            SUCCEEDED(d->StretchRect(fusedM2Samples,nullptr,fusedM2ResolvedSurface,nullptr,D3DTEXF_NONE));
    }
    bool prepareFusedM2Msaa(IDirect3DSurface9* depth,float distance,float cavity){
        if(depth!=fusedM2WorldDepth||memoryPressure||fusedM2MsaaUnavailable)return false;
        const auto type=sceneColor.desc.MultiSampleType;
        const auto quality=sceneColor.desc.MultiSampleQuality;
        if(fusedM2SceneDepth.p)return fusedM2SceneDepth.p==depth&&type==fusedM2SampleType&&quality==fusedM2SampleQuality&&
            (!fusedM2DepthBase||(distance==fusedM2PackedRange&&cavity==fusedM2PackedCavity));
        const unsigned samples=type==D3DMULTISAMPLE_NONMASKABLE&&quality<=3?1u<<quality:unsigned(type);
        if(samples!=2&&samples!=4&&samples!=8)return false;
        unsigned base=fusedM2PackedUnavailable?0:packedM2DepthBase(distance,fusedM2Near,cavity);
        if(type!=fusedM2SampleType||quality!=fusedM2SampleQuality||bool(base)!=bool(fusedM2DepthBase))resetFusedM2Msaa();
        if(!fusedM2Samples){
            if(base&&!createFusedM2Msaa(type,quality,true)){
                resetFusedM2Msaa();fusedM2PackedUnavailable=true;base=0;
                logf("MATERIAL_M2_MSAA packed unavailable; using FP16");
            }
            if(!base&&!createFusedM2Msaa(type,quality,false)){
                resetFusedM2Msaa();fusedM2MsaaUnavailable=true;
                logf("MATERIAL_M2_MSAA unavailable samples=%u quality=%lu; material replays retained",samples,quality);
                return false;
            }
            fusedM2SampleType=type;fusedM2SampleQuality=quality;fusedM2SampleCount=samples;
            logf("MATERIAL_M2_MSAA ready samples=%u quality=%lu bytes=%llu packed=%u near=%.5f range=%.1f cavity=%.3f",samples,quality,
                (unsigned long long)width*heightPixels*(base?4*(samples+1)+8:8*(samples+1)),unsigned(base!=0),fusedM2Near,distance,cavity);
        }
        if(FAILED(d->ColorFill(fusedM2Samples,nullptr,0))){
            resetFusedM2Msaa();fusedM2MsaaUnavailable=true;
            logf("MATERIAL_M2_MSAA clear failed; material replays retained");
            return false;
        }
        fusedM2DepthBase=base;fusedM2PackedRange=distance;fusedM2PackedCavity=cavity;
        fusedM2SceneDepth.p=depth;depth->AddRef();
        return true;
    }

    bool planFusedM2(const NorthlightDrawSnapshot::Draw& draw, const ReplayState& saved, const Vertex& shader,
                     const NativeCapture& model, NorthlightAutoNormalCache::Lease& normal, const float* bank,
                     DWORD addressU,DWORD addressV,DWORD zfunc,bool auxiliary){
        ++fusedM2Candidates;
        if(!saved.depth||!saved.target||!draw.indexed||!shader.patched)return false;
        if(!checkedFusedM2Caps){
            D3DCAPS9 caps{};
            fusedM2Caps=SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.NumSimultaneousRTs>=4&&
                (caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)&&
                (caps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
            checkedFusedM2Caps=true;
        }
        const D3DRENDERSTATETYPE keys[]={D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_DITHERENABLE,D3DRS_SCISSORTESTENABLE};
        DWORD values[4];
        if(!fusedM2Caps||!readStates(keys,values)||values[0]||values[1]||values[2])return false;
        if(values[3]&&FAILED(d->GetScissorRect(&fusedM2.scissor)))return false;
        const bool ms=sceneColor.desc.MultiSampleType!=D3DMULTISAMPLE_NONE;
        if(ms?!prepareFusedM2Msaa(saved.depth,bank[1],bank[14]):!finishCaptures())return false;
        if(!ms&&!cleared){
            if(FAILED(d->ColorFill(target,nullptr,0)))return false;
            cleared=true;
        }
        if(auxiliary&&!occlusionCleared){
            if(FAILED(d->ColorFill(occlusionTarget,nullptr,0)))return false;
            occlusionCleared=true;
        }
        fusedM2.color.p=saved.target;saved.target->AddRef();
        fusedM2.depth.p=saved.depth;saved.depth->AddRef();
        fusedM2.originalVertex=saved.vertex;
        fusedM2.vertex=shader.patched;
        fusedM2.replay=&model;
        fusedM2.viewport=saved.viewport;
        fusedM2.draw=draw;
        fusedM2.addressU=addressU;
        fusedM2.addressV=addressV;
        fusedM2.zfunc=zfunc;
        fusedM2.auxiliary=auxiliary;
        fusedM2.scissorEnabled=values[3]!=0;
        fusedM2.multisampled=ms;
        std::copy_n(bank,36,fusedM2.bank);
        fusedM2.normal=std::move(normal);
        return true;
    }

    const NativeCapture* fusedM2Pixel(IDirect3DPixelShader9* bound,unsigned slot,
                                     const NorthlightEffectCoverage::NativePixel* coverage)try{
        const bool packed=fusedM2.multisampled&&fusedM2DepthBase;
        auto& shaders=fusedM2Pixels[packed][slot-1];
        const auto found=shaders.find(bound);
        if(found!=shaders.end())return found->second.shader?&found->second:nullptr;
        if(shaders.size()>=512)return nullptr;
        auto& result=shaders.try_emplace(bound).first->second;
        result.source=bound;bound->AddRef();
        auto* original=fusedM2.replay->source;
        UINT bytes=0;
        if(FAILED(original->GetFunction(nullptr,&bytes))||bytes<8||bytes>=65536||bytes%4)return nullptr;
        std::vector<DWORD> code(bytes/4);
        std::vector<uint32_t> patched;
        if(FAILED(original->GetFunction(code.data(),&bytes))||
           !NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched,
               result.constant,false,false,false,true,slot,packed))return nullptr;
        if(bound!=original&&!coverage->compose(patched))return nullptr;
        if(FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&result.shader)))drop(result.shader);
        return result.shader?&result:nullptr;
    }catch(const std::bad_alloc&){return nullptr;}

    static constexpr DWORD m2Sampler(unsigned index,DWORD u,DWORD v){
        switch(index){
            case 0:case 1:case 2:return D3DTEXF_LINEAR;
            case 3:return u;
            case 4:return v;
            default:return 0;
        }
    }
    void countFusedM2(bool fused){
        if(fused&&fusedM2.multisampled){
            fusedM2MsaaDrawn=true;fusedM2MsaaOcclusion|=fusedM2.auxiliary;
            ++draws[3];++fusedM2Draws;return;
        }
        drawn=true;++draws[3];occlusionDrawn|=fusedM2.auxiliary;
        if(fused)++fusedM2Draws;else ++fusedM2Fallbacks;
    }
    // Called before the native submit if state or shader ownership changes
    // after admission. Bind the original capture targets and replay once.
    bool replayFusedM2(){
        if(fusedM2.multisampled&&!finishCaptures())return true;
        SavedState restore(d,&captureStates);
        if(!restore.ok)return false;
        const auto& m=fusedM2;
        const auto& draw=m.draw;
        const auto base=m.replay->constant;
        float previous[36];
        if(FAILED(d->GetPixelShaderConstantF(base,previous,9)))return false;
        bool ok=true;
        auto set=[&](HRESULT hr){ok=SUCCEEDED(hr)&&ok;};
        set(d->SetDepthStencilSurface(nullptr));
        for(unsigned i=1;i<4;++i)set(d->SetRenderTarget(i,nullptr));
        set(d->SetRenderTarget(0,target));
        if(m.auxiliary)set(d->SetRenderTarget(1,occlusionTarget));
        set(d->SetDepthStencilSurface(m.multisampled?captureDepth:m.depth.p));
        if(m.multisampled&&ok){
            const DWORD flags=(!cleared?D3DCLEAR_TARGET:0)|(!privateDepthCleared?D3DCLEAR_ZBUFFER:0);
            if(flags){
                set(d->Clear(0,nullptr,flags,0,1,0));
                if(ok)cleared=privateDepthCleared=true;
            }
        }
        set(d->SetViewport(&m.viewport));
        set(d->SetRenderState(D3DRS_SCISSORTESTENABLE,m.scissorEnabled));
        if(m.scissorEnabled)set(d->SetScissorRect(&m.scissor));
        set(d->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE));
        set(d->SetRenderState(D3DRS_ZWRITEENABLE,m.multisampled));
        set(d->SetRenderState(D3DRS_ZFUNC,m.multisampled&&m.zfunc==D3DCMP_EQUAL?D3DCMP_LESSEQUAL:m.zfunc));
        set(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));
        set(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
        set(d->SetRenderState(D3DRS_FOGENABLE,FALSE));
        set(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
        set(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        set(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        if(m.auxiliary)set(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
        set(d->SetVertexShader(m.vertex));
        set(d->SetPixelShader(m.replay->shader));
        set(d->SetTexture(14,m.normal.get()));
        for(unsigned i=0;i<8;++i)set(d->SetSamplerState(14,ReplayState::samplerKeys[i],m2Sampler(i,m.addressU,m.addressV)));
        set(d->SetPixelShaderConstantF(base,m.bank,9));
        if(ok)set(d->DrawIndexedPrimitive(draw.topology,draw.base,draw.minimum,draw.vertices,draw.start,draw.primitives));
        const bool state=restore.restore();
        const bool constants=SUCCEEDED(d->SetPixelShaderConstantF(base,previous,9));
        if(ok&&state&&constants)countFusedM2(false);
        return state&&constants;
    }

    template<class Draw> HRESULT submitFusedM2(Draw draw,const NorthlightEffectCoverage::NativePixel* coverage){
        if(!fusedM2.normal)return draw();
        auto& m=fusedM2;
        auto fallback=[&](unsigned reason){
            ++fusedM2Rejections[reason];
            const bool restored=replayFusedM2();
            endNativeMaterialDraw();
            return restored?draw():E_FAIL;
        };
        using namespace NorthlightDrawSnapshot;
        Ref<IDirect3DVertexShader9> vertex;
        Ref<IDirect3DPixelShader9> pixel;
        Ref<IDirect3DSurface9> color,depth;
        D3DVIEWPORT9 viewport{};
        if(FAILED(d->GetVertexShader(vertex.out()))||vertex.p!=m.originalVertex||
           FAILED(d->GetPixelShader(pixel.out()))||!pixel.p||
           FAILED(d->GetRenderTarget(0,color.out()))||color.p!=m.color.p||
           FAILED(d->GetDepthStencilSurface(depth.out()))||depth.p!=m.depth.p||
           FAILED(d->GetViewport(&viewport))||std::memcmp(&viewport,&m.viewport,sizeof viewport))return fallback(0);
        if(pixel.p!=m.replay->source&&(!coverage||!coverage->matches(pixel.p)||coverage->source!=m.replay->source))return fallback(0);
        unsigned slot=0;
        for(unsigned i=3;i>0;--i){
            Ref<IDirect3DSurface9> surface;
            const auto hr=d->GetRenderTarget(i,surface.out());
            if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return fallback(1);
            if(!surface.p){slot=i;break;}
        }
        if(!slot)return fallback(1);
        const auto* shader=fusedM2Pixel(pixel.p,slot,coverage);
        if(!shader)return fallback(2);
        DWORD samplers[8];
        const auto maskKey=slot==1?D3DRS_COLORWRITEENABLE1:slot==2?D3DRS_COLORWRITEENABLE2:D3DRS_COLORWRITEENABLE3;
        const D3DRENDERSTATETYPE keys[]={maskKey,
            D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,
            D3DRS_SRGBWRITEENABLE,D3DRS_ZFUNC,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_SCISSORTESTENABLE};
        DWORD raster[10]={};
        Ref<IDirect3DBaseTexture9> texture;
        const auto base=shader->constant;
        float constants[36];
        // The native actor and receiver stamps use ALWAYS with stencil
        // writes. The fused draw keeps those writes and their coverage.
        if(!readStates(keys,raster)||raster[1]||raster[2]||!northlightMaterialStencilAllows(d,raster[3])||
           raster[4]||raster[5]||raster[6]!=m.zfunc||raster[7]!=D3DZB_TRUE||!raster[8]||
           (raster[9]!=0)!=m.scissorEnabled||
           FAILED(d->GetTexture(14,texture.out()))||FAILED(d->GetPixelShaderConstantF(base,constants,9))){
#ifdef NORTHLIGHT_PROFILE_BUILD
            static bool reported=false;
            if(!reported){reported=true;logf("MATERIAL_M2_FUSION_STATE blend=%lu alpha=%lu stencil=%lu fog=%lu srgb=%lu zfunc=%lu expected=%lu z=%lu write=%lu",
                raster[1],raster[2],raster[3],raster[4],raster[5],raster[6],m.zfunc,raster[7],raster[8]);}
#endif
            return fallback(3);
        }
        if(m.scissorEnabled){
            RECT scissor{};
            if(FAILED(d->GetScissorRect(&scissor))||std::memcmp(&scissor,&m.scissor,sizeof scissor))return fallback(3);
        }
        const auto mask=raster[0];
        for(unsigned i=0;i<8;++i)
            if(FAILED(d->GetSamplerState(14,ReplayState::samplerKeys[i],samplers+i)))return fallback(3);
        bool ok=true;
        auto set=[&](HRESULT hr){ok=SUCCEEDED(hr)&&ok;};
        set(d->SetRenderTarget(slot,m.multisampled?fusedM2Samples:target));
        set(d->SetViewport(&viewport));
        if(mask!=15)set(d->SetRenderState(maskKey,15));
        set(d->SetVertexShader(m.vertex));
        set(d->SetPixelShader(shader->shader));
        set(d->SetTexture(14,m.normal.get()));
        unsigned changed=0;
        for(unsigned i=0;i<8;++i){
            const auto value=m2Sampler(i,m.addressU,m.addressV);
            if(samplers[i]!=value){changed|=1u<<i;set(d->SetSamplerState(14,ReplayState::samplerKeys[i],value));}
        }
        const float captureAlpha=m.bank[3];
        if(m.multisampled&&fusedM2DepthBase)m.bank[3]=float(fusedM2DepthBase);
        set(d->SetPixelShaderConstantF(base,m.bank,9));
        m.bank[3]=captureAlpha;
        const bool bound=ok;
        const HRESULT hr=bound?draw():E_FAIL;
        ok=true;
        set(d->SetVertexShader(vertex.p));
        set(d->SetPixelShader(pixel.p));
        set(d->SetTexture(14,texture.p));
        for(unsigned i=0;i<8;++i)if(changed&(1u<<i))set(d->SetSamplerState(14,ReplayState::samplerKeys[i],samplers[i]));
        set(d->SetPixelShaderConstantF(base,constants,9));
        set(d->SetRenderTarget(slot,nullptr));
        if(mask!=15)set(d->SetRenderState(maskKey,mask));
        set(d->SetViewport(&viewport));
        if(ok&&!bound)return fallback(4);
        if(SUCCEEDED(hr)&&ok)countFusedM2(true);
        endNativeMaterialDraw();
        return FAILED(hr)?hr:ok?D3D_OK:E_FAIL;
    }
