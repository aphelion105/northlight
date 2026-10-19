#pragma once

// Explicit F12 diagnostics only. Compare the retained snapshot with fresh copies;
// neither failures nor the probe's temporary bindings alter rendering decisions.
namespace NorthlightDepthProbe {
// Local F12 investigation only; INTZ keeps stencil, DF24 requests depth alone.
// Test the original attachment without RESZ or a depth sampler. No colour,
// depth or stencil writes; queries near the far plane distinguish scene geometry from an untouched clear.
inline void sourceTest(IDirect3DDevice9* d, IDirect3DSurface9* world,
                       unsigned frame,const char* point) {
    SavedState saved(d);
    if(!saved.ok||!world)return;
    D3DSURFACE_DESC ds={},rt={};
    if(FAILED(world->GetDesc(&ds))||FAILED(saved.targets[0]->GetDesc(&rt))||
       ds.Width!=rt.Width||ds.Height!=rt.Height||ds.MultiSampleType!=rt.MultiSampleType)return;
    IDirect3DQuery9* query=nullptr;
    HRESULT hr=d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query);
    if(FAILED(hr)||!query){logf("DEPTH source-test frame=%u unavailable HRESULT=%08lx",frame,(unsigned long)hr);return;}
    hr=d->SetDepthStencilSurface(world);
    for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);
    d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);d->SetStreamSourceFreq(0,1);
    const struct {D3DRENDERSTATETYPE state;DWORD value;} states[]={
        {D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},
        {D3DRS_STENCILENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},
        {D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_LIGHTING,FALSE},
        {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_COLORWRITEENABLE,0},
        {D3DRS_CLIPPLANEENABLE,0},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},
        {D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_MULTISAMPLEANTIALIAS,TRUE}};
    for(auto state:states)d->SetRenderState(state.state,state.value);
    d->SetTexture(0,nullptr);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    const D3DVIEWPORT9 viewport={0,0,ds.Width,ds.Height,0,1};
    if(SUCCEEDED(hr))hr=d->SetViewport(&viewport);
    const DWORD deadline=GetTickCount();
    for(float z:{0.f,.5f,.9f,.99f,.999f,.9999f,.99999f,1.f}){
        struct Vertex{float x,y,z,rhw;DWORD color;};
        const Vertex v[]={{-.5f,-.5f,z,1,~0u},{float(ds.Width)-.5f,-.5f,z,1,~0u},
            {-.5f,float(ds.Height)-.5f,z,1,~0u},{float(ds.Width)-.5f,float(ds.Height)-.5f,z,1,~0u}};
        HRESULT result=hr;
        if(SUCCEEDED(result))result=query->Issue(D3DISSUE_BEGIN);
        if(SUCCEEDED(result)){
            result=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex));
            const HRESULT end=query->Issue(D3DISSUE_END);if(SUCCEEDED(result))result=end;
        }
        DWORD count=0;
        if(SUCCEEDED(result)){
            while((result=query->GetData(&count,sizeof count,D3DGETDATA_FLUSH))==S_FALSE&&GetTickCount()-deadline<1000)Sleep(1);
        }
        logf("DEPTH source-test frame=%u point=%s z=%.8f passedSamples=%lu pixels=%llu msaa=%u HRESULT=%08lx",
             frame,point,z,(unsigned long)count,(unsigned long long)ds.Width*ds.Height,unsigned(ds.MultiSampleType),(unsigned long)result);
        if(result!=S_OK)break;
    }
    query->Release();
}
inline void run(IDirect3DDevice9* d, IDirect3DTexture9* retained,
                IDirect3DSurface9* world, unsigned frame,const char* point) {
    SavedState saved(d);
    if (!saved.ok || !world || !retained) return;
    D3DSURFACE_DESC desc={};
    if (FAILED(world->GetDesc(&desc))) return;
    logf("DEPTH probe frame=%u point=%s source=%p current=%p sourceFormat=%u sourceMSAA=%u size=%ux%u",
         frame,point,static_cast<void*>(world),static_cast<void*>(saved.depth),unsigned(desc.Format),
         unsigned(desc.MultiSampleType),desc.Width,desc.Height);
    struct Resources {
        IDirect3DSurface9 *output=nullptr,*staging=nullptr;
        IDirect3DTexture9* scratch=nullptr;
        ~Resources(){if(output)output->Release();if(staging)staging->Release();if(scratch)scratch->Release();}
    } r;
    constexpr UINT w=32,h=18;
    HRESULT hr=d->CreateRenderTarget(w,h,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&r.output,nullptr);
    if(SUCCEEDED(hr))hr=d->CreateOffscreenPlainSurface(w,h,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&r.staging,nullptr);
    if(FAILED(hr)){logf("DEPTH probe frame=%u setup failed HRESULT=%08lx",frame,(unsigned long)hr);return;}
    auto sample=[&](const char* label,IDirect3DTexture9* texture){
        d->SetDepthStencilSurface(nullptr);
        for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        HRESULT result=d->SetRenderTarget(0,r.output);
        d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);
        d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        const struct {D3DRENDERSTATETYPE state;DWORD value;} states[]={
            {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},
            {D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},
            {D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},
            {D3DRS_FOGENABLE,FALSE},{D3DRS_LIGHTING,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},
            {D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_COLORWRITEENABLE,15},
            {D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_WRAP0,0},
            {D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_MULTISAMPLEANTIALIAS,TRUE}};
        for(auto state:states)d->SetRenderState(state.state,state.value);
        d->SetTexture(0,texture);d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
        d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        struct Vertex{float x,y,z,rhw,u,v;};
        const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},
            {-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};
        const D3DVIEWPORT9 viewport={0,0,w,h,0,1};
        if(SUCCEEDED(result))result=d->SetViewport(&viewport);
        if(SUCCEEDED(result))result=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
        if(SUCCEEDED(result))result=d->GetRenderTargetData(r.output,r.staging);
        D3DLOCKED_RECT lock={};bool locked=false;
        if(SUCCEEDED(result)){result=r.staging->LockRect(&lock,nullptr,D3DLOCK_READONLY);locked=SUCCEEDED(result);}
        float lo=1.f,hi=0.f;unsigned zero=0,finite=0;std::uint64_t hash=14695981039346656037ull;
        if(locked&&lock.pBits&&lock.Pitch>=INT(w*sizeof(float)))for(UINT y=0;y<h;++y){
            const float* row=reinterpret_cast<const float*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch);
            for(UINT x=0;x<w;++x)if(std::isfinite(row[x])){++finite;lo=std::min(lo,row[x]);hi=std::max(hi,row[x]);if(row[x]==0)++zero;std::uint32_t bits=0;std::memcpy(&bits,&row[x],sizeof bits);hash^=bits;hash*=1099511628211ull;}}
        if(locked)r.staging->UnlockRect();
        logf("DEPTH probe frame=%u sample=%s min=%.9g max=%.9g zero=%u finite=%u total=%u fingerprint=%016llx HRESULT=%08lx",
             frame,label,lo,hi,zero,finite,w*h,(unsigned long long)hash,(unsigned long)result);
    };
    sample("retained",retained);
    for(bool depthOnly:{false,true}){
        const D3DFORMAT format=depthOnly?(D3DFORMAT)MAKEFOURCC('D','F','2','4'):(D3DFORMAT)MAKEFOURCC('I','N','T','Z');
        hr=d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_DEPTHSTENCIL,format,D3DPOOL_DEFAULT,&r.scratch,nullptr);
        if(FAILED(hr)){logf("DEPTH probe frame=%u format=%u scratch failed HRESULT=%08lx",frame,unsigned(format),(unsigned long)hr);continue;}
        // A 1x destination starts at a known nonzero value: an ignored RESZ
        // command then differs from a successful copy of an all-zero source.
        if(desc.MultiSampleType==D3DMULTISAMPLE_NONE){
            IDirect3DSurface9* scratchSurface=nullptr;
            HRESULT init=r.scratch->GetSurfaceLevel(0,&scratchSurface);
            const D3DVIEWPORT9 full={0,0,desc.Width,desc.Height,0,1};
            if(SUCCEEDED(init))init=d->SetTexture(0,nullptr);
            if(SUCCEEDED(init))init=d->SetRenderTarget(0,saved.targets[0]);
            if(SUCCEEDED(init))init=d->SetDepthStencilSurface(scratchSurface);
            if(SUCCEEDED(init))init=d->SetViewport(&full);
            if(SUCCEEDED(init))init=d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.314159f,0);
            if(scratchSurface)scratchSurface->Release();
            logf("DEPTH probe frame=%u sentinel format=%u HRESULT=%08lx",frame,unsigned(format),(unsigned long)init);
            if(SUCCEEDED(init))sample(depthOnly?"sentinel-df24":"sentinel-intz",r.scratch);
        }
        auto resolve=[&](const char* label,IDirect3DSurface9* source){
            HRESULT result=d->SetDepthStencilSurface(source);
            if(SUCCEEDED(result))result=d->SetTexture(0,r.scratch);
            if(SUCCEEDED(result))result=d->SetRenderState(D3DRS_POINTSIZE,0x3f800000);
            if(SUCCEEDED(result))result=d->SetRenderState(D3DRS_POINTSIZE,0x7fa05000);
            logf("DEPTH probe frame=%u copy=%s HRESULT=%08lx",frame,label,(unsigned long)result);
            if(SUCCEEDED(result))sample(label,r.scratch);
        };
        resolve(depthOnly?"depth-only-df24":"fresh-intz",world);
        if(saved.depth&&saved.depth!=world)resolve(depthOnly?"current-df24":"current-intz",saved.depth);
        d->SetTexture(0,nullptr);r.scratch->Release();r.scratch=nullptr;
    }
    // Use the original full-size render target for the attachment-only queries.
    // Query draws write neither color nor depth/stencil; SavedState restores all state.
    d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,saved.targets[0]);d->SetDepthStencilSurface(world);
    sourceTest(d,world,frame,point);
}
}
