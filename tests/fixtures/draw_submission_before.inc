    // 0.3.187: the four draw entry points share drawHook(); each passes its capture and its real draw.
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t,UINT start,UINT count) override { Guard mirrorLock(mirrorState.gate);
        NorthlightAutoDisplacement::Args geometry{{t,0,0,0,start,count,false}};
        bool nativeSky=false,skyCaptured=false;
        auto draw=[&]{return hdrDraw([&]{auto raw=[&]{return effectCoverageDraw([&]{return nativeLocalSubmit([&]{return ext->DrawPrimitive(t,start,count);});},false,nativeSky,&skyCaptured);};return projectedDraw(count,applied?1u:0u,[&]{return autoBump&&enabled?autoBump->geometryDraw(raw,true,[&](auto submit,IDirect3DVertexShader9* source){return nativeLocalSubmit(submit,true,source);}):raw();});});};
        if(enabled&&(spellOn||particleOn))spellArgs=NorthlightSpellLayer::DrawArgs::primitive(t,start,count);
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){NorthlightAutoDisplacement::CaptureSource normalSource;if(autoBump&&enabled&&!applied&&!failed){autoBump->capture(vs,[&]{return ext->DrawPrimitive(t,start,count);},false,&geometry,nullptr,&normalSource);}captureModelNormals(vs,[&]{return ext->DrawPrimitive(t,start,count);},false,geometry,&normalSource);world->capture(t,0,0,0,start,count,false,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::NoUserPointer,draw);},draw,&nativeSky,&skyCaptured);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT base,UINT min,UINT vertices,UINT start,UINT count) override { Guard mirrorLock(mirrorState.gate);
        NorthlightAutoDisplacement::Args geometry{{t,base,min,vertices,start,count,true}};
        bool nativeSky=false,skyCaptured=false;
        auto draw=[&]{return hdrDraw([&]{auto raw=[&]{return effectCoverageDraw([&]{return nativeLocalSubmit([&]{return ext->DrawIndexedPrimitive(t,base,min,vertices,start,count);});},false,nativeSky,&skyCaptured);};return projectedDraw(count,applied?1u:0u,[&]{return autoBump&&enabled?autoBump->geometryDraw(raw,true,[&](auto submit,IDirect3DVertexShader9* source){return nativeLocalSubmit(submit,true,source);}):raw();});});};
        if(enabled&&(spellOn||particleOn))spellArgs=NorthlightSpellLayer::DrawArgs::indexed(t,base,min,vertices,start,count);
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){NorthlightAutoDisplacement::CaptureSource normalSource;
            NorthlightCaptureMetadata::PositionBounds positions;positions.owner=&geometry;positions.shader=vs;
            auto captureWorld=[&](NorthlightCaptureMetadata::PositionBounds* out=nullptr){world->capture(t,base,min,vertices,start,count,true,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame),out);};
            const bool sharedTerrain=autoBump&&autoBump->active()&&enabled&&!applied&&!failed&&world->terrainShader(vs);
            if(sharedTerrain)captureWorld(&positions);
            if(autoBump&&enabled&&!applied&&!failed)autoBump->capture(vs,[&]{return ext->DrawIndexedPrimitive(t,base,min,vertices,start,count);},false,&geometry,&positions,&normalSource);captureModelNormals(vs,[&]{return ext->DrawIndexedPrimitive(t,base,min,vertices,start,count);},false,geometry,&normalSource);
            if(!sharedTerrain)captureWorld();
            captureWater(vs,NorthlightWaterRenderer::NoUserPointer,draw);},draw,&nativeSky,&skyCaptured);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT count,const void* data,UINT stride) override { Guard mirrorLock(mirrorState.gate);
        NorthlightAutoDisplacement::Args geometry{{t,0,0,0,0,count,false},nullptr,D3DFMT_UNKNOWN,data,stride};
        bool nativeSky=false,skyCaptured=false;
        auto draw=[&]{return hdrDraw([&]{auto raw=[&]{return effectCoverageDraw([&]{return nativeLocalSubmit([&]{return ext->DrawPrimitiveUP(t,count,data,stride);});},false,nativeSky,&skyCaptured);};return projectedDraw(count,applied?1u:0u,[&]{return autoBump&&enabled?autoBump->geometryDraw(raw,true,[&](auto submit,IDirect3DVertexShader9* source){return nativeLocalSubmit(submit,true,source);}):raw();});});};
        if(enabled&&(spellOn||particleOn))spellArgs=NorthlightSpellLayer::DrawArgs::primitiveUP(t,count,data,stride);
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){NorthlightAutoDisplacement::CaptureSource normalSource;if(autoBump&&enabled&&!applied&&!failed){autoBump->capture(vs,[&]{return ext->DrawPrimitiveUP(t,count,data,stride);},true,&geometry,nullptr,&normalSource);captureModelNormals(vs,[&]{return ext->DrawPrimitiveUP(t,count,data,stride);},true,geometry,&normalSource);}world->captureUP(t,0,0,count,nullptr,D3DFMT_UNKNOWN,data,stride,false,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::UserVertices,draw);},draw,&nativeSky,&skyCaptured);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT min,UINT vertices,UINT count,const void* indices,D3DFORMAT fmt,const void* data,UINT stride) override { Guard mirrorLock(mirrorState.gate);
        NorthlightAutoDisplacement::Args geometry{{t,0,min,vertices,0,count,true},indices,fmt,data,stride};
        bool nativeSky=false,skyCaptured=false;
        auto draw=[&]{return hdrDraw([&]{auto raw=[&]{return effectCoverageDraw([&]{return nativeLocalSubmit([&]{return ext->DrawIndexedPrimitiveUP(t,min,vertices,count,indices,fmt,data,stride);});},false,nativeSky,&skyCaptured);};return projectedDraw(count,applied?1u:0u,[&]{return autoBump&&enabled?autoBump->geometryDraw(raw,true,[&](auto submit,IDirect3DVertexShader9* source){return nativeLocalSubmit(submit,true,source);}):raw();});});};
        if(enabled&&(spellOn||particleOn))spellArgs=NorthlightSpellLayer::DrawArgs::indexedUP(t,min,vertices,count,indices,fmt,data,stride);
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){NorthlightAutoDisplacement::CaptureSource normalSource;if(autoBump&&enabled&&!applied&&!failed){autoBump->capture(vs,[&]{return ext->DrawIndexedPrimitiveUP(t,min,vertices,count,indices,fmt,data,stride);},true,&geometry,nullptr,&normalSource);captureModelNormals(vs,[&]{return ext->DrawIndexedPrimitiveUP(t,min,vertices,count,indices,fmt,data,stride);},true,geometry,&normalSource);}world->captureUP(t,min,vertices,count,indices,fmt,data,stride,true,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::UserVerticesAndIndices,draw);},draw,&nativeSky,&skyCaptured);}
};

static bool hdrGamePresentation(IDirect3DDevice9* owner,D3DPRESENT_PARAMETERS* out){return static_cast<Device*>(owner)->gamePresentation(out);}
static bool hdrGameBackBuffer(IDirect3DDevice9* owner,unsigned index,IDirect3DSurface9** out){return static_cast<Device*>(owner)->gameBackBuffer(index,out);}
static void finishDeviceFrame(IDirect3DDevice9* owner,bool implicitChain) {static_cast<Device*>(owner)->finishFrame(implicitChain);}
static void presentedDeviceFrame(IDirect3DDevice9* owner,const PresentTicks& ticks) {static_cast<Device*>(owner)->presented(ticks);}
static void windowPresentedDeviceFrame(IDirect3DDevice9* owner,HWND window,HRESULT status,bool primary){static_cast<Device*>(owner)->windowPresented(window,status,primary);}
static bool hdrPresentDeviceFrame(IDirect3DDevice9* owner,const RECT* src,const RECT* dst,HWND window,const RGNDATA* dirty,DWORD flags,HRESULT* result){return static_cast<Device*>(owner)->presentHdr(src,dst,window,dirty,flags,result);}

static const char* tclCallerModule(void* address); /* 0.3.204 (task 21): defined below */
class Factory final : public ForwardIDirect3D9 {
    LONG refs=1;
public:
    explicit Factory(IDirect3D9* p):ForwardIDirect3D9(p){}
    ~Factory(){real->Release();}