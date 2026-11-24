    bool drawDirectLocalLights(UINT w,UINT h,const NorthlightLocalLightSelection::Selection& localLights,
                               const NorthlightLocalLightScissor::Rect* rects,float lampStrength,float lampSunlitCut,
                               const float (&shadowTuning)[4],NorthlightGpuProfile* profile,
                               NorthlightLightingCapture::Capture* lightingCapture,int reflection,float reflectionGain){
        return localLighting.drawDirect(d,screenLighting.raw().surface,w,h,localLights,rects,localScissorView,
                                        lampStrength,lampSunlitCut,shadowTuning,profile,lightingCapture,sourceTrace,
                                        [&](HRESULT hr,const char* message){return check(hr,message);},reflection,reflectionGain);
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
        int reflectedDirect=-1,reflectedPoint=-1;
        const float reflectionGain=debug==0&&materialDetail&&materialBumpGain>0&&paletteFrameValid&&
            NorthlightQuality::foreverCelestial(quality)?framePalette.localSurfaceSpecular:0.f;
        if(reflectionGain>0){
            // Compare existing sources once. Only one can add a reflection.
            float best=0;
            const auto score=[&](const NorthlightLocalLights::Light& light){
                if(light.flags&(NorthlightLocalLights::Baked|NorthlightLocalLights::Flash))return 0.f;
                const bool moonwell=(light.flags&NorthlightLocalLights::Moonwell)!=0;
                if(gpuBudgetLevel>=2&&!moonwell)return 0.f;
                const V delta=vec(light.position)-vec(context.camera);
                const float d2=NorthlightGI::dot(delta,delta);
                const float reach=moonwell?light.attenuationEnd+4.f:30.f;
                return d2<reach*reach?std::max({light.diffuse[0],light.diffuse[1],light.diffuse[2]})/(1+.05f*d2):0.f;
            };
            for(unsigned i=0;i<localLights.count;++i){
                const auto& light=localLights.sources[i];
                if(light.flags&NorthlightLocalLights::CastShadow)continue;
                const float value=score(light);
                if(value>best){best=value;reflectedDirect=int(i);}
            }
            const auto& slots=pointTargets.slots();
            for(unsigned i=0;pointReady&&i<slots.size();++i){
                const auto& slot=slots[i];
                if(!slot.ready||pointCubeIsWhite(slot))continue;
                const float value=score(slot.selected)*slot.selectionGain;
                if(value>best){best=value;reflectedPoint=int(i);reflectedDirect=-1;}
            }
        }
        const auto direct=[&]{
            return !localDirectCount||debug!=0||
                drawDirectLocalLights(w,h,localLights,rects,c[52][1],lampSunlitCut,shadowTuning,profile,lightingCapture,reflectedDirect,reflectionGain);
        };
        const auto points=[&]{
            const float sunEnergy=in.sunActive?std::max({sourceColors[0].x,sourceColors[0].y,sourceColors[0].z}):0.f;
            bool pointStatePrepared=false;
            for(pointIndex=0;pointReady&&pointIndex<pointTargets.slots().size();++pointIndex)if(pointSlot().ready)
                renderPointLighting(in.depth,in.waterMask,w,h,in.nearZ,in.farZ,in.minZ,in.maxZ,c[52][1],lampSunlitCut,c[20][2],sunEnergy,lightingCapture,lightingCapture?nullptr:&pointStatePrepared,pointInputs,int(pointIndex)==reflectedPoint?reflectionGain:0.f);
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
