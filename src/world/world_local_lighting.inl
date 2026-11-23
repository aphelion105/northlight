    bool drawDirectLocalLights(UINT w,UINT h,const NorthlightLocalLightSelection::Selection& localLights,
                               const NorthlightLocalLightScissor::Rect* rects,float lampStrength,float lampSunlitCut,
                               const float (&shadowTuning)[4],NorthlightGpuProfile* profile,
                               NorthlightLightingCapture::Capture* lightingCapture){
        return localLighting.drawDirect(d,screenLighting.raw().surface,w,h,localLights,rects,localScissorView,
                                        lampStrength,lampSunlitCut,shadowTuning,profile,lightingCapture,sourceTrace,
                                        [&](HRESULT hr,const char* message){return check(hr,message);});
    }

    struct LocalLightingInput {
        UINT w,h;
        float nearZ,farZ,minZ,maxZ;
        int debug;
        bool accumulate,fullMaterial,diagnostic,sunActive;
        float lampSunlitCut;
        IDirect3DTexture9 *depth,*waterMask;
        const float (&constants)[68][4];
        const float (&shadowTuning)[4];
        IDirect3DTexture9* const* frameTextures;
    };

    // Temporal resolve has finished consuming the directional target. The
    // local pass can reuse it and restore the world state needed by fog.
    bool renderLocalLighting(const LocalLightingInput& in,const NorthlightLocalLightSelection::Selection& localLights,
                             const NorthlightLocalLightScissor::Rect* rects,NorthlightGpuProfile* profile,
                             NorthlightLightingCapture::Capture* lightingCapture,NorthlightLightingCapture::Capture* pointInputs=nullptr){
        const UINT w=in.w,h=in.h;
        const int debug=in.debug;
        const float lampSunlitCut=in.lampSunlitCut;
        const auto& c=in.constants;
        const auto& shadowTuning=in.shadowTuning;
        const bool positionalLighting=debug==0&&(pointReady||flashShadows.ready());
        const bool localMaterialInputs=in.accumulate||positionalLighting||in.fullMaterial||lightingCapture||in.diagnostic||debug!=0;
        const NorthlightWorldLocalLighting::SurfaceInput pass{screenLighting.raw().surface,materialDetail,worldNormals.texture(),
            in.frameTextures,c,in.accumulate,localMaterialInputs,positionalLighting,effects.fog};
        const auto direct=[&]{
            return !localDirectCount||debug!=0||
                drawDirectLocalLights(w,h,localLights,rects,c[52][1],lampSunlitCut,shadowTuning,profile,lightingCapture);
        };
        const auto points=[&]{
            const float sunEnergy=in.sunActive?std::max({sourceColors[0].x,sourceColors[0].y,sourceColors[0].z}):0.f;
            bool pointStatePrepared=false;
            for(pointIndex=0;pointReady&&pointIndex<pointTargets.slots().size();++pointIndex)if(pointSlot().ready)
                renderPointLighting(in.depth,in.waterMask,w,h,in.nearZ,in.farZ,in.minZ,in.maxZ,c[52][1],lampSunlitCut,c[20][2],sunEnergy,lightingCapture,lightingCapture?nullptr:&pointStatePrepared,pointInputs);
            pointIndex=0;
        };
        return localLighting.drawSurface(d,pass,localLights,profile,effectsBuckets,lightingCapture,pointInputs,
            [&](HRESULT hr,const char* message){return check(hr,message);},
            direct,[&]{renderFlashLighting();},points);
    }

    bool renderLocalFog(UINT w,UINT h,IDirect3DSurface9* currentFogSurface,
                        const NorthlightLocalLightSelection::Selection& localLights,
                        const NorthlightLocalLightScissor::Rect* rects,const float (&worldFogInfo)[4],
                        NorthlightLightingCapture::Capture* lightingCapture){
        // The next history slot has no readers after this frame's resolve.
        const auto& scratch=history.pending().light;
        return localLighting.drawFog(d,w,h,currentFogSurface,scratch.texture,scratch.surface,
                                     localLights,rects,localScissorView,worldFogInfo,sourceTrace,lightingCapture,
                                     [&](HRESULT hr,const char* message){return check(hr,message);});
    }
