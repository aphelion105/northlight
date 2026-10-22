#pragma once
#include "auto_bump_compiled_shaders.h"
#include "auto_bump_identities.h"
#include "water_shader_patch.h"
#include "quality_settings.h"
#include "auto_normal_cache.h"
#include "auto_parallax_renderer.h"
#include "auto_displacement_renderer.h"
#include "native_material_capture_patch.h"

// Prototype: one material-height replay per recognised draw; source textures remain intact.
// Included after drop and logf. Original resources stay intact; displacement draws use private subdivided buffers.
class NorthlightAutoBump {
    friend struct MaterialCaptureStateTestAccess;
    IDirect3DDevice9* d;
    NorthlightAutoNormalCache normals;
    NorthlightSharedPOM sharedPOM;
    NorthlightAutoNormalCache::Lease sharedMaps[2];
    NorthlightAutoParallax parallax;
    NorthlightAutoDisplacement displacement;
    float projection[4]={1,1,-1,0};
    // Only these states are changed by the replay. A full state block per material
    // copied every shader constant/texture and rebound all MRT/depth surfaces.
    struct ReplayState {
        IDirect3DDevice9* d;
        IDirect3DPixelShader9* pixel=nullptr;
        IDirect3DVertexShader9* vertex;
        IDirect3DSurface9* target=nullptr;
        IDirect3DSurface9* depth=nullptr;
        IDirect3DVertexBuffer9* stream=nullptr;
        IDirect3DIndexBuffer9* indices=nullptr;
        UINT offset=0,stride=0;
        D3DVIEWPORT9 viewport;
        float constant[24]={};unsigned constantBase=20,constantCount=5,normalStage=2,stateCount=14;
        IDirect3DBaseTexture9* texture2=nullptr,*texture3=nullptr;bool composite=false;
        const D3DSAMPLERSTATETYPE samplerKeys[8]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MIPMAPLODBIAS};
        DWORD samplerValues[8]={},samplerValues3[8]={};unsigned samplerChanged=0,samplerChanged3=0;
        struct State{D3DRENDERSTATETYPE key;DWORD value,replay;};
        State states[16]={
            {D3DRS_ZWRITEENABLE,0,FALSE},{D3DRS_ALPHATESTENABLE,0,FALSE},
            {D3DRS_FOGENABLE,0,FALSE},{D3DRS_SRGBWRITEENABLE,0,FALSE},
            {D3DRS_COLORWRITEENABLE,0,15},{D3DRS_ALPHABLENDENABLE,0,TRUE},
            {D3DRS_BLENDOP,0,D3DBLENDOP_ADD},{D3DRS_SRCBLEND,0,D3DBLEND_SRCALPHA},
            {D3DRS_DESTBLEND,0,D3DBLEND_INVSRCALPHA},{D3DRS_SEPARATEALPHABLENDENABLE,0,TRUE},
            {D3DRS_BLENDOPALPHA,0,D3DBLENDOP_ADD},{D3DRS_SRCBLENDALPHA,0,D3DBLEND_ONE},
            {D3DRS_DESTBLENDALPHA,0,D3DBLEND_INVSRCALPHA},{D3DRS_COLORWRITEENABLE1,0,15},
            {D3DRS_COLORWRITEENABLE2,0,15},{D3DRS_COLORWRITEENABLE3,0,15}};
        bool changed=false,userMemory=false,auxiliary=false,sharedTargets=false;
        ReplayState(IDirect3DDevice9* device,IDirect3DVertexShader9* vs,const D3DVIEWPORT9& vp):d(device),vertex(vs),viewport(vp){}
        bool save(bool up,bool twoLayers=false){
            composite=twoLayers;
            if(composite){if(FAILED(d->GetTexture(3,&texture3)))return false;for(unsigned i=0;i<8;++i)if(FAILED(d->GetSamplerState(3,samplerKeys[i],&samplerValues3[i])))return false;}
            if(!pixel||!target||
               FAILED(d->GetPixelShaderConstantF(constantBase,constant,constantCount))||FAILED(d->GetTexture(normalStage,&texture2)))return false;
            for(unsigned i=0;i<8;++i)if(FAILED(d->GetSamplerState(normalStage,samplerKeys[i],&samplerValues[i])))return false;
            if(up){if(FAILED(d->GetStreamSource(0,&stream,&offset,&stride))||FAILED(d->GetIndices(&indices)))return false;userMemory=true;}
            return true;
        }
        ~ReplayState(){
            if(changed){
                if(sharedTargets){d->SetRenderTarget(2,nullptr);d->SetRenderTarget(3,nullptr);}
                if(auxiliary)d->SetRenderTarget(1,nullptr);
                if(depth)d->SetDepthStencilSurface(nullptr);
                d->SetRenderTarget(0,target);if(depth)d->SetDepthStencilSurface(depth);d->SetViewport(&viewport);
                d->SetVertexShader(vertex);d->SetPixelShader(pixel);d->SetPixelShaderConstantF(constantBase,constant,constantCount);
                d->SetTexture(normalStage,texture2);for(unsigned i=0;i<8;++i)if(samplerChanged&(1u<<i))d->SetSamplerState(normalStage,samplerKeys[i],samplerValues[i]);
                if(composite){d->SetTexture(3,texture3);for(unsigned i=0;i<8;++i)if(samplerChanged3&(1u<<i))d->SetSamplerState(3,samplerKeys[i],samplerValues3[i]);}
                for(unsigned i=0;i<stateCount;++i){auto& s=states[i];if(s.value!=s.replay)d->SetRenderState(s.key,s.value);}
                // UP draws reset stream 0 and (for indexed UP) the index binding.
                if(userMemory){d->SetStreamSource(0,stream,offset,stride);d->SetIndices(indices);}
            }
            drop(pixel);drop(target);drop(depth);drop(stream);drop(indices);drop(texture2);drop(texture3);
        }
        void apply(){changed=true;for(unsigned i=0;i<stateCount;++i){auto& s=states[i];if(s.value!=s.replay)d->SetRenderState(s.key,s.replay);}}
    };
    struct Vertex {IDirect3DVertexShader9* patched=nullptr;unsigned kind=0;IDirect3DVertexShader9* shared=nullptr;bool sharedChecked=false;};
    bool sharedVertex(Vertex& vertex){if(!vertex.sharedChecked){vertex.sharedChecked=true;vertex.shared=NorthlightSharedPOM::vertexShader(d,vertex.patched);}return vertex.shared!=nullptr;}
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    std::unordered_map<IDirect3DPixelShader9*,unsigned> pixels;
    IDirect3DPixelShader9* shaders[6]={},*sharedShaders[6]={};
    struct NativeCapture {IDirect3DPixelShader9* source=nullptr,*shader=nullptr;unsigned constant=128;};
    std::unordered_map<IDirect3DPixelShader9*,NativeCapture> nativeCaptures[2][2];
    NativeCapture* modelCapture(IDirect3DPixelShader9* source,bool alpha,bool shared=false){
        auto& variants=nativeCaptures[shared][alpha];auto it=variants.find(source);if(it!=variants.end())return it->second.shader?&it->second:nullptr;
        if(variants.size()>=512)return nullptr;NativeCapture entry;entry.source=source;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes&&bytes<65536){std::vector<DWORD> code(bytes/4);std::vector<uint32_t> patched;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched,entry.constant,alpha,shared))d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&entry.shader);}
        auto added=variants.emplace(source,entry).first;if(!entry.shader)logf("MATERIAL_MODEL capture unavailable; native material retained");return entry.shader?&added->second:nullptr;
    }
    IDirect3DTexture9* height=nullptr;
    IDirect3DSurface9* target=nullptr;
    // The composite validates the captured linear depth against final scene depth.
    // A private 1x depth lets the replay shade MSAA scenes without 2/4/8 copies
    // of the full-screen normal/occlusion targets or a resolve after every draw.
    IDirect3DSurface9* captureDepth=nullptr;
    IDirect3DTexture9* occlusionMap=nullptr;IDirect3DSurface9* occlusionTarget=nullptr;
    bool occlusionUnavailable=false;unsigned occlusionRetry=0;
    float occlusionControl[4]={.7f,8,.35f,0},occlusionLight[4]={};
    UINT width=0,heightPixels=0;
    bool cleared=false,drawn=false,failed=false;
    bool enabled=false,terrain=true,walls=true,grass=true,memoryPressure=false;
    float strength=.75f,range=48;
    unsigned modes[4]={2,2,1,3};float pomControl[4]={.018f,16,16,0};
    unsigned draws[4]={},frames=0,rejected=0,invisibleRejected=0,noCaptureRequired=0;
    bool pomInRange=true;unsigned rangeTests=0,rangeRejected=0,pomRangeRejected=0;
    bool captureNeeded(unsigned domain)const{return strength>0||occlusionControl[2]>0||
        (modes[domain-1]==2&&pomControl[0]>0&&pomInRange&&occlusionControl[0]>0&&occlusionLight[3]>0);}
    template<size_t N> static unsigned kind(const AutoBumpIdentity (&table)[N],std::uint64_t hash){
        auto i=std::lower_bound(table,table+N,hash,[](const AutoBumpIdentity& a,std::uint64_t h){return a.hash<h;});
        return i!=table+N&&i->hash==hash?i->kind:0;
    }
    bool check(HRESULT hr,const char* stage){
        if(SUCCEEDED(hr))return true;
        if(hr==E_OUTOFMEMORY||hr==D3DERR_OUTOFVIDEOMEMORY){
            trimMemory();logf("AUTOBUMP allocation retry stage=%s hr=%08lx; modes retained",stage,(unsigned long)hr);return false;
        }
        if(!failed)logf("AUTOBUMP failed stage=%s hr=%08lx; native rendering retained",stage,(unsigned long)hr);
        failed=true;return false;
    }
public:
    explicit NorthlightAutoBump(IDirect3DDevice9* device):d(device),normals(device),sharedPOM(device),parallax(device,normals),displacement(device){}
    void setMemoryPressure(bool on){
        if(memoryPressure==on)return;memoryPressure=on;
        normals.setMemoryPressure(on);displacement.setMemoryPressure(on);sharedPOM.setMemoryPressure(on);
        logf("MATERIAL_MEMORY pressure=%u normalBudgetMiB=%u geometryBudgetMiB=%u relief=active",unsigned(on),on?32:96,on?32:128);
    }
    void trimMemory(){sharedMaps[0]={};sharedMaps[1]={};sharedPOM.reset();normals.trimMemory();displacement.trimMemory();}
    bool geometryPrepared()const{return displacement.prepared();}
    unsigned geometryPrimitives()const{return displacement.preparedPrimitives();}
    bool geometryHeightGPU()const{return displacement.preparedHeightGPU();}
    void beginDraw(){displacement.beginDraw();sharedMaps[0]={};sharedMaps[1]={};sharedPOM.beginDraw();pomInRange=true;}
    void setGeometryTracking(uint64_t(*identity)(void*,bool),uint64_t(*version)(void*,bool),NorthlightCaptureMetadata::Read metadata){displacement.tracking(identity,version,metadata);sharedPOM.tracking(identity,version,metadata);}
    void setConstantEpochSource(void* context,NorthlightConstantEpoch::Reader reader){displacement.setConstantEpochSource(context,reader);}
    template<class Draw> HRESULT geometryDraw(Draw draw,bool native=false){return parallax.shared()?sharedPOM.drawBound(true):displacement.draw(draw,native);}
    void setProjection(float x,float y,float z){projection[0]=x;projection[1]=y;projection[2]=z;}
    void setLight(const float* light){std::copy(light,light+4,occlusionLight);}
    void configure(const NorthlightQuality::Settings& settings){
        displacement.configure(settings);normals.configure(settings);
        const bool changed=enabled!=(settings.autoBump!=0)||strength!=settings.autoBumpStrength*.01f||range!=float(settings.autoBumpDistance)||modes[0]!=settings.autoBumpTerrain||modes[1]!=settings.autoBumpWalls||modes[2]!=settings.autoBumpGrass||modes[3]!=settings.autoBumpModels||pomControl[0]!=settings.autoParallaxDepth*.00018f||pomControl[1]!=float(settings.autoParallaxDistance)||pomControl[2]!=float(settings.autoParallaxSteps);
        enabled=settings.autoBump!=0;strength=settings.autoBumpStrength*.01f;range=float(settings.autoBumpDistance);
        terrain=settings.autoBumpTerrain!=0;walls=settings.autoBumpWalls!=0;grass=settings.autoBumpGrass!=0;
        modes[0]=settings.autoBumpTerrain;modes[1]=settings.autoBumpWalls;modes[2]=settings.autoBumpGrass;modes[3]=settings.autoBumpModels;
        pomControl[0]=settings.autoParallaxDepth*.00018f;pomControl[1]=float(settings.autoParallaxDistance);pomControl[2]=float(settings.autoParallaxSteps);
        const float shadow=settings.autoParallaxShadows?settings.autoParallaxShadowStrength*.01f:0,cavity=settings.autoCavity?settings.autoCavityStrength*.01f:0;
        if(shadow!=occlusionControl[0]||cavity!=occlusionControl[2]||float(settings.autoParallaxShadowSteps)!=occlusionControl[1])logf("MATERIAL_OCCLUSION shadow=%.2f steps=%u cavity=%.2f",shadow,settings.autoParallaxShadowSteps,cavity);
        occlusionControl[0]=shadow;occlusionControl[1]=float(settings.autoParallaxShadowSteps);occlusionControl[2]=cavity;
        if(shadow<=0&&cavity<=0){drop(occlusionTarget);drop(occlusionMap);occlusionUnavailable=false;}
        if(changed)logf("AUTOBUMP enabled=%d strength=%.2f distance=%.0f terrain=%d walls=%d grass=%d modes=%u/%u/%u/%u pomDepth=%.6f pomRange=%.0f pomSteps=%.0f",int(enabled),strength,range,int(terrain),int(walls),int(grass),modes[0],modes[1],modes[2],modes[3],pomControl[0],pomControl[1],pomControl[2]);
        if(!enabled)reset();
        bool captureRequired=false;for(unsigned i=0;i<4;++i)captureRequired|=modes[i]&&captureNeeded(i+1);
        if(!captureRequired){sharedMaps[0]={};sharedMaps[1]={};sharedPOM.reset();drop(captureDepth);drop(target);drop(height);drop(occlusionTarget);drop(occlusionMap);drawn=cleared=false;occlusionUnavailable=false;occlusionRetry=0;}
    }
    ~NorthlightAutoBump(){reset();for(auto& v:vertices){drop(v.second.patched);drop(v.second.shared);}for(auto*& p:shaders)drop(p);for(auto*& p:sharedShaders)drop(p);for(auto& groups:nativeCaptures)for(auto& variants:groups)for(auto& n:variants){drop(n.second.shader);drop(n.second.source);}}
    void reset(){sharedMaps[0]={};sharedMaps[1]={};sharedPOM.reset();displacement.reset();normals.reset();drop(captureDepth);drop(target);drop(height);drop(occlusionTarget);drop(occlusionMap);occlusionUnavailable=false;occlusionRetry=0;width=heightPixels=0;cleared=drawn=failed=false;}
    void endFrame(){
        if(occlusionRetry)--occlusionRetry;
        displacement.endFrame();
        sharedMaps[0]={};sharedMaps[1]={};sharedPOM.endFrame();if(frames%600==0)sharedPOM.report();
        normals.endFrame();
        parallax.endFrame();
        if(drawn&&frames%600==0){normals.report();auto& cache=NorthlightMaterialTextures::cache();const auto& c=cache.counts;logf("MHEIGHT cached=%u bytes=%zu uploads=%llu retained=%llu failed=%llu invalidated=%llu evicted=%llu",cache.size(),cache.bytes(),(unsigned long long)c.uploads,(unsigned long long)c.retained,(unsigned long long)c.failed,(unsigned long long)c.invalidations,(unsigned long long)c.evictions);}
        if((drawn||rangeTests)&&frames%600==0){logf("MATERIAL_RANGE proven=%u captureRejected=%u nativePomRejected=%u",rangeTests,rangeRejected,pomRangeRejected);displacement.reportHeightGPU();}
        if(drawn&&frames%600==0)logf("AUTOBUMP captured terrain=%u walls=%u grass=%u models=%u target=%ux%u bytes=%llu rejected=%u",draws[0],draws[1],draws[2],draws[3],width,heightPixels,(unsigned long long)width*heightPixels*8,rejected);
        if(frames%600==0&&(invisibleRejected||noCaptureRequired))logf("MATERIAL_ADMISSION invisible=%u noCaptureContribution=%u",invisibleRejected,noCaptureRequired);
        for(auto& n:draws)n=0;rejected=rangeTests=rangeRejected=pomRangeRejected=invisibleRejected=noCaptureRequired=0;cleared=drawn=false;pomInRange=true;++frames;
    }
    bool active()const{return enabled&&(modes[0]||modes[1]||modes[2]||modes[3])&&(strength>0||occlusionControl[2]>0||
        ((modes[0]==2||modes[1]==2||modes[2]==2||modes[3]==2)&&pomControl[0]>0)||
        ((modes[0]==3||modes[1]==3||modes[2]==3||modes[3]==3)&&displacement.hasDisplacement()))&&!failed;}
    IDirect3DTexture9* texture()const{return active()&&drawn?height:nullptr;}
    IDirect3DTexture9* occlusionTexture()const{return active()&&drawn?occlusionMap:nullptr;}
    float gain()const{return texture()?strength:0;}
    float distance()const{return range;}
    template<class Draw> HRESULT nativeDraw(Draw draw,IDirect3DVertexShader9* provenVS=nullptr,IDirect3DPixelShader9* provenPS=nullptr){
        if(!active()||!pomInRange||pomControl[0]<=0||(modes[0]!=2&&modes[1]!=2&&modes[2]!=2&&modes[3]!=2))return draw();
        struct BoundShaders {IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;~BoundShaders(){drop(vs);drop(ps);}} bound;
        if((!provenVS&&FAILED(d->GetVertexShader(&bound.vs)))||(!provenPS&&FAILED(d->GetPixelShader(&bound.ps))))return draw();
        auto v=vertices.find(provenVS?provenVS:bound.vs);auto p=pixels.find(provenPS?provenPS:bound.ps);
        if(v==vertices.end()||p==pixels.end()||v->second.kind>4||modes[v->second.kind-1]!=2)return draw();
        const unsigned domain=v->second.kind,material=p->second;
        if((domain==1&&material>2)||(domain==2&&material!=3&&material!=5&&material!=6)||(domain==3&&material!=4))return draw();
        return parallax.draw(v->first,v->second.patched,material,projection,pomControl,draw,domain==4,bound.vs,bound.ps,&sharedPOM,v->second.shared,provenVS!=nullptr);
    }
    void registerVertex(IDirect3DVertexShader9* original,std::uint64_t hash,const DWORD* code,size_t count,unsigned modelFamily=0){
        auto found=vertices.find(original);if(found!=vertices.end()){drop(found->second.patched);drop(found->second.shared);vertices.erase(found);}
        const unsigned k=modelFamily==3?4:kind(kAutoBumpVertex,hash);if(!k||vertices.size()>=4096)return;
        std::vector<std::uint32_t> patched;
        if(NorthlightWaterShaderPatch::patch(reinterpret_cast<const std::uint32_t*>(code),count,patched))return;
        IDirect3DVertexShader9* shader=nullptr;
        if(SUCCEEDED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader))){vertices[original]={shader,k};displacement.registerMaterial(original,code,count,k);displacement.registerVariant(shader,reinterpret_cast<const DWORD*>(patched.data()),patched.size(),displacement.skinned(original));}
    }
    void registerPixel(IDirect3DPixelShader9* original,std::uint64_t hash){
        pixels.erase(original);const unsigned k=kind(kAutoBumpPixel,hash);
        if(k&&pixels.size()<4096)pixels[original]=k;
    }
    template<class Draw> void capture(IDirect3DVertexShader9* original,Draw draw,bool userMemory=false,const NorthlightAutoDisplacement::Args* geometry=nullptr){
        pomInRange=true;
        if(!active())return;
        auto v=vertices.find(original);if(v==vertices.end())return;
        const unsigned domain=v->second.kind;
        if((domain==1&&!terrain)||(domain==2&&!walls)||(domain==3&&!grass)||(domain==4&&!modes[3]))return;
        DWORD colorWrite=0,depthWrite=0;
        if(FAILED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&colorWrite))||FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&depthWrite)))return;
        if(!(colorWrite&7)||(!depthWrite&&domain!=1)){++invisibleRejected;return;}
        if(geometry&&domain<=3&&modes[domain-1]!=3){float nearest=0;
            if(displacement.nearestMaterialDepth(original,*geometry,nearest)){++rangeTests;
                if(nearest>=pomControl[1]){pomInRange=false;++pomRangeRejected;}
                if(nearest>range){++rangeRejected;return;}}
        }
        const bool needsCapture=captureNeeded(domain);
        if(!needsCapture&&modes[domain-1]!=3){++noCaptureRequired;return;}
        IDirect3DPixelShader9* native=nullptr;if(FAILED(d->GetPixelShader(&native))||!native)return;
        auto p=pixels.find(native);if(p==pixels.end()){drop(native);return;}
        const unsigned material=p->second;
        if((domain==1&&material>2)||(domain==2&&material!=3&&material!=5&&material!=6)||(domain==3&&material!=4)){drop(native);return;}
        ReplayState saved(d,original,D3DVIEWPORT9{});saved.pixel=native;NorthlightSharedPOM::Binding sharedBinding(d);
        NativeCapture* model=nullptr;
        DWORD z=0,zfunc=0,ref=0,alphaFunc=0,stencil=0;
        auto rs=[&](D3DRENDERSTATETYPE key,DWORD& value){return SUCCEEDED(d->GetRenderState(key,&value));};
        if(!rs(D3DRS_ZENABLE,z)||!rs(D3DRS_ZFUNC,zfunc)||!rs(D3DRS_ALPHAREF,ref)||
           !rs(D3DRS_ALPHAFUNC,alphaFunc)||!rs(D3DRS_STENCILENABLE,stencil)||!z||zfunc==D3DCMP_ALWAYS||zfunc==D3DCMP_NEVER||stencil)return;
        D3DVIEWPORT9 viewport{};if(FAILED(d->GetViewport(&viewport))||viewport.X||viewport.Y||viewport.Width<8||viewport.Height<8)return;
        for(unsigned i=1;i<4;++i){IDirect3DSurface9* extra=nullptr;d->GetRenderTarget(i,&extra);if(extra){drop(extra);return;}}
        if(FAILED(d->GetDepthStencilSurface(&saved.depth))||!saved.depth)return;
        saved.states[0].value=depthWrite;saved.states[4].value=colorWrite;
        for(unsigned i=0;i<saved.stateCount;++i){if(i==0||i==4)continue;auto& state=saved.states[i];if(FAILED(d->GetRenderState(state.key,&state.value)))return;}
        const auto& s=saved.states;
        const bool alpha=s[1].value!=0;
        // These capture programs produce alpha exactly one. Their normal,
        // occlusion and UV outputs overwrite the destination without blending.
        if((domain==1&&material==1)||(domain==4&&!alpha))saved.states[5].replay=FALSE;
        if(!(s[4].value&7)||(!s[0].value&&domain!=1)||
           (s[5].value&&(s[7].value!=D3DBLEND_SRCALPHA||s[8].value!=D3DBLEND_INVSRCALPHA))||
           (alpha&&alphaFunc!=D3DCMP_GREATER&&alphaFunc!=D3DCMP_GREATEREQUAL))return;
        if(FAILED(d->GetRenderTarget(0,&saved.target))||!saved.target)return;
        D3DSURFACE_DESC desc{};if(FAILED(saved.target->GetDesc(&desc)))return;
        if(desc.Width!=viewport.Width||desc.Height!=viewport.Height){++rejected;return;}
        if(width!=desc.Width||heightPixels!=desc.Height){reset();width=desc.Width;heightPixels=desc.Height;}
        // A distant proof needs no capture constants, samplers or private depth.
        // Resolve the target size first: reset() would discard prepared geometry.
        if(domain<=4&&modes[domain-1]==3&&geometry){displacement.prepare(original,domain,material,*geometry);
            if(displacement.nearestPreparedDepth()>range){++rangeTests;++rangeRejected;return;}}
        if(!needsCapture){++noCaptureRequired;return;}
        const DWORD* codes[]={kBumpOpaqueShader,kBumpLayerShader,kBumpWallShader,kBumpGrassShader,kBumpWallOpaqueShader,kBumpCompositeShader};
        const DWORD* sharedCodes[]={kBumpSharedOpaqueShader,kBumpSharedLayerShader,kBumpSharedWallShader,kBumpSharedGrassShader,kBumpSharedWallOpaqueShader,kBumpSharedCompositeShader};
        bool sharing=geometry&&modes[domain-1]==2&&pomInRange&&pomControl[0]>0&&sharedPOM.canTry()&&!(domain==4&&material==6)&&parallax.supportsShared(native,material)&&sharedVertex(v->second)&&
            sharedPOM.prepare(geometry->draw,geometry->indices,geometry->format,geometry->vertices,geometry->stride,width,heightPixels,material==6);
        if(sharing&&domain!=4&&!sharedShaders[material-1]&&FAILED(d->CreatePixelShader(sharedCodes[material-1],&sharedShaders[material-1])))sharing=false;
        if(sharing&&!sharedBinding.bind(sharedPOM,false))sharing=false;
        if(domain==4){model=modelCapture(native,alpha,sharing);if(!model&&sharing){sharedBinding.restore();sharing=false;model=modelCapture(native,alpha);}if(!model)return;saved.constantBase=model->constant;saved.normalStage=14;}
        if(sharing){saved.constantCount=6;saved.stateCount=16;for(unsigned i=14;i<16;++i)if(FAILED(d->GetRenderState(saved.states[i].key,&saved.states[i].value)))return;}
        saved.viewport=viewport;if(!saved.save(userMemory&&!sharing,material==6&&domain!=4))return;
        const bool multisampled=desc.MultiSampleType!=D3DMULTISAMPLE_NONE;
        if(multisampled){
            if(!captureDepth&&!check(d->CreateDepthStencilSurface(width,heightPixels,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&captureDepth,nullptr),"material depth"))return;
            saved.states[0].replay=saved.states[0].value;
        }else drop(saved.depth);
        if(!height){
            const std::uint64_t bytes=std::uint64_t(width)*heightPixels*8;
            MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
            if(bytes>(64ull<<20)||!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<bytes+(128ull<<20)){++rejected;return;}
            if(!check(d->CreateTexture(width,heightPixels,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&height,nullptr),"height texture"))return;
        }
        if(!target&&!check(height->GetSurfaceLevel(0,&target),"height surface"))return;
        if((occlusionControl[0]>0||occlusionControl[2]>0)&&!occlusionMap&&!occlusionUnavailable&&!occlusionRetry){
            D3DCAPS9 caps{};MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;const auto bytes=std::uint64_t(width)*heightPixels*4;
            const bool supported=SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.NumSimultaneousRTs>=2&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS);
            bool available=supported&&GlobalMemoryStatusEx(&memory)&&memory.ullAvailVirtual>bytes+(128ull<<20);
            if(available)available=SUCCEEDED(d->CreateTexture(width,heightPixels,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&occlusionMap,nullptr))&&SUCCEEDED(occlusionMap->GetSurfaceLevel(0,&occlusionTarget));
            if(!available){drop(occlusionTarget);drop(occlusionMap);occlusionUnavailable=!supported;occlusionRetry=supported?60:0;logf("MATERIAL_OCCLUSION allocation retry=%u unsupported=%u; settings retained",occlusionRetry,unsigned(!supported));}
        }
        const unsigned captureIndex=model?0:material-1;auto& captureShader=sharing?sharedShaders[captureIndex]:shaders[captureIndex];
        if(!model&&!captureShader&&!check(d->CreatePixelShader(sharing?sharedCodes[material-1]:codes[material-1],&captureShader),"capture shader"))return;
        IDirect3DBaseTexture9* diffuse=nullptr;if(FAILED(d->GetTexture(0,&diffuse)))return;
        auto normal=normals.acquire(diffuse,domain==4);drop(diffuse);if(!normal)return;
        NorthlightAutoNormalCache::Lease normal1;
        if(material==6&&domain!=4){IDirect3DBaseTexture9* second=nullptr;if(FAILED(d->GetTexture(1,&second)))return;normal1=normals.acquire(second);drop(second);if(!normal1)return;}
        DWORD addressU=D3DTADDRESS_WRAP,addressV=D3DTADDRESS_WRAP;d->GetSamplerState(0,D3DSAMP_ADDRESSU,&addressU);d->GetSamplerState(0,D3DSAMP_ADDRESSV,&addressV);
        saved.apply();d->SetTexture(saved.normalStage,normal.get());
        const DWORD normalSampler[]={D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_LINEAR,addressU,addressV,FALSE,0,0};
        for(unsigned i=0;i<8;++i)if(saved.samplerValues[i]!=normalSampler[i]){saved.samplerChanged|=1u<<i;d->SetSamplerState(saved.normalStage,saved.samplerKeys[i],normalSampler[i]);}
        if(normal1){d->SetTexture(3,normal1.get());for(unsigned i=0;i<8;++i)if(saved.samplerValues3[i]!=normalSampler[i]){saved.samplerChanged3|=1u<<i;d->SetSamplerState(3,saved.samplerKeys[i],normalSampler[i]);}}
        if(multisampled)d->SetDepthStencilSurface(nullptr);
        if(!check(d->SetRenderTarget(0,target),"capture target"))return;
        if(multisampled&&!check(d->SetDepthStencilSurface(captureDepth),"material depth bind"))return;
        if(occlusionTarget){saved.auxiliary=true;if(FAILED(d->SetRenderTarget(1,occlusionTarget))){drop(occlusionTarget);drop(occlusionMap);occlusionUnavailable=true;d->SetRenderTarget(1,nullptr);}}
        if(!cleared){if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|(multisampled?D3DCLEAR_ZBUFFER:0),0,1,0),"height clear"))return;cleared=true;}
        bool cacheBound=false;if(sharing){saved.sharedTargets=true;cacheBound=SUCCEEDED(d->SetRenderTarget(2,sharedPOM.target(0)))&&SUCCEEDED(d->SetRenderTarget(3,material==6?sharedPOM.target(1):nullptr));if(!cacheBound){d->SetRenderTarget(2,nullptr);d->SetRenderTarget(3,nullptr);}}
        const float constants[]={domain==1?.18f:domain==2?.14f:.06f,range,alpha?(float(ref)+(alphaFunc==D3DCMP_GREATER?.5f:0.f))/255.f:.001f,domain==4&&alpha?1.f:0.f};
        float pom[4];std::copy(pomControl,pomControl+4,pom);if(modes[domain-1]!=2||!parallax.supports(saved.pixel,material))pom[0]=0;
        float occult[4];std::copy(occlusionControl,occlusionControl+4,occult);occult[3]=occlusionTarget?1.f:0.f;if(pom[0]<=0||occlusionLight[3]<=0)occult[0]=0;
        float bank[24];for(unsigned i=0;i<4;++i){bank[i]=constants[i];bank[4+i]=projection[i];bank[8+i]=pom[i];bank[12+i]=occult[i];bank[16+i]=occlusionLight[i];bank[20+i]=sharedPOM.control[i];}
        if(!check(d->SetVertexShader(sharing?v->second.shared:v->second.patched),"capture VS")||!check(d->SetPixelShader(model?model->shader:captureShader),"capture PS")||
           !check(d->SetPixelShaderConstantF(saved.constantBase,bank,saved.constantCount),"capture constants"))return;
        if(check(sharing?sharedPOM.drawBound(false):displacement.draw(draw),"material replay")){drawn=true;++draws[domain-1];if(cacheBound){sharedMaps[0]=std::move(normal);sharedMaps[1]=std::move(normal1);for(unsigned i=0;i<2;++i){sharedPOM.maps[i]=sharedMaps[i].get();sharedPOM.addressU[i]=addressU;sharedPOM.addressV[i]=addressV;}sharedPOM.producedDraw();}}
    }
};
