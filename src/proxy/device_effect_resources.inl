// Device effects resources. The COM adapter calls this module at its Reset boundary.
    void releaseResources(bool preserveCaptureSources=false) {
        projectedAudit.reset();
        effectCoverage.reset();
        if(surface)surface->reset();if(autoBump)autoBump->reset(nullptr,preserveCaptureSources);if(modelNormals)modelNormals->reset(preserveCaptureSources);
        if(aoActorMask)aoActorMask->reset();aoActorFrame=~0u;aoActorOn=false;
        drop(aoActorPreparePS);drop(aoActorPS);drop(aoActorAlphaPS);aoActorShaderFailed=false;
        rainMrtUnbind();stateBlocks.clear();clearFrame();maskFormatFor=nullptr;maskFormatValue=0;maskStencil=false;maskDsId=nullptr; /* 0.3.212 (task 32): the depth may change with the device */
        drop(sceneSurface); drop(aoSurface); drop(aoRawSurface); drop(scene); drop(depthTex); drop(ao); drop(aoRaw);drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);drop(rainDepth);rainMaskFailed=false; /* 0.3.202 (rain) */
        drop(aoPS); drop(aoBlurPS); drop(aoBlurNoWaterPS);aoBlurNoWaterRefused=false;drop(aoContactBloomPS); drop(aoOnlyPS); drop(contactBloomPS); drop(compositePS);drop(rainMrtPS);drop(rainScrubPS); width = height = 0;
    }
    bool error(HRESULT hr, const char* stage) {
        if (SUCCEEDED(hr)) return false;
        if (!failed) logf("DISABLED: %s failed HRESULT=0x%08lx", stage, (unsigned long)hr);
        failed = true; return true;
    }
    void selectAO(unsigned method,unsigned quality){
        if(aoMethod!=method||(method&&aoQuality!=quality))drop(aoOnlyPS);
        if(aoMethod!=method){drop(aoContactBloomPS);drop(aoActorPS);drop(aoActorAlphaPS);aoActorShaderFailed=false;}
        else if(method&&aoQuality!=quality)drop(aoContactBloomPS);
        aoMethod=method;aoQuality=quality;
    }
    DWORD depthSnapshotUsage()const{
        return depthSnapshotFormat==D3DFMT_R32F?D3DUSAGE_RENDERTARGET:D3DUSAGE_DEPTHSTENCIL;
    }
    bool resources(UINT w, UINT h, D3DFORMAT format, bool colorOnly=false) {
        // DXVK 3's 1x depth+stencil RESZ copy can leave INTZ unchanged (proved
        // with a nonzero sentinel). DF24 copies depth alone, which is all our
        // snapshots consume; NameMask still uses the original world stencil.
        // DXVK 2.7.1 has the same ResolveZ and the same copy-path choice (a depth+stencil copy goes through the
        // shader copy on AMD drivers, depth alone through vkCmdCopyImage), so both packaged DXVKs take DF24.
        // A frame whose world depth was not bound (bindWorldDepth failed after projectionValid) keeps the current
        // format: deciding INTZ there recreated every effect resource, and DF24 again on the next frame. A DF24 the
        // device refused stays INTZ for the session (df24Refused).
        const D3DFORMAT intz=(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),df24=(D3DFORMAT)MAKEFOURCC('D','F','2','4');
        const D3DFORMAT depthFormat=hdr.nativeOutput?D3DFMT_R32F:!worldDepthDescKnown&&depthSnapshotFormat!=D3DFMT_UNKNOWN?depthSnapshotFormat
            :NorthlightBackend::isPackagedDxvk(selectedBackend)&&!df24Refused&&worldDepthDescKnown&&worldDepthDesc.MultiSampleType==D3DMULTISAMPLE_NONE?df24:intz;
        if(!colorOnly){
            // Method switches replace shaders while keeping the existing scene/depth targets.
            const DWORD* aoContactShaders[3][5]={
                {kAoContactBloomShader,kHdaoContactBloomShader,kHbaoPlusContactBloomShader,kGtaoContactBloomShader,kAssaoContactBloomShader},
                {kAoContactBloomShader,kHdaoContactBloomMediumShader,kHbaoPlusContactBloomMediumShader,kGtaoContactBloomMediumShader,kAssaoContactBloomMediumShader},
                {kAoContactBloomShader,kHdaoContactBloomLowShader,kHbaoPlusContactBloomLowShader,kGtaoContactBloomLowShader,kAssaoContactBloomLowShader}};
            if (!aoPS && error(ext->CreatePixelShader(kAoShader, &aoPS), "AO shader")) return false;
            if (!aoContactBloomPS && error(ext->CreatePixelShader(aoContactShaders[aoQuality][aoMethod], &aoContactBloomPS), "AO contact bloom shader")) return false;
            if((hdr.active||aoMethod)&&!aoOnlyPS){
                const DWORD* shaders[3][5]={
                    {kAoOnlyShader,kHdaoOnlyShader,kHbaoPlusOnlyShader,kGtaoOnlyShader,kAssaoOnlyShader},
                    {kAoOnlyShader,kHdaoOnlyMediumShader,kHbaoPlusOnlyMediumShader,kGtaoOnlyMediumShader,kAssaoOnlyMediumShader},
                    {kAoOnlyShader,kHdaoOnlyLowShader,kHbaoPlusOnlyLowShader,kGtaoOnlyLowShader,kAssaoOnlyLowShader}};
                if(error(ext->CreatePixelShader(shaders[aoQuality][aoMethod],&aoOnlyPS),"AO alpha shader"))return false;
            }
            if (!aoBlurPS && error(ext->CreatePixelShader(kAoBlurShader, &aoBlurPS), "AO blur shader")) return false;
            if(!aoBlurNoWaterPS&&!aoBlurNoWaterRefused&&FAILED(ext->CreatePixelShader(kAoBlurNoWaterShader,&aoBlurNoWaterPS))){drop(aoBlurNoWaterPS);aoBlurNoWaterRefused=true;}
            if (!contactBloomPS && error(ext->CreatePixelShader(kContactBloomShader, &contactBloomPS), "contact bloom shader")) return false;
            if (!compositePS && error(ext->CreatePixelShader(kCompositeShader, &compositePS), "composite shader")) return false;
        }
        if (sceneSurface && w == width && h == height && format == sceneFormat && (colorOnly || (aoSurface && aoRawSurface && depthTex && depthFormat == depthSnapshotFormat))) return true;
        // Resource replacement must not invalidate this frame's retained depth surface.
        if (w!=width || h!=height || format!=sceneFormat) {drop(sceneSurface); drop(scene);}
        if (w!=width || h!=height || (!colorOnly && depthFormat!=depthSnapshotFormat)) {
            if(autoBump&&depthTex)autoBump->discardSceneDepthScratch(depthTex);
            rainMrtUnbind();rainDepthOk=rainMaskCleared=rainMaskDrawn=false;rainMaskFailed=false;
            drop(aoSurface); drop(ao); drop(aoRawSurface); drop(aoRaw); drop(depthTex); captured=false;drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);drop(rainDepth); /* 0.3.202 (rain): the mask follows the effect size */
        }
        width = w; height = h; sceneFormat = format;
        if (!scene && error(ext->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &scene, nullptr), "scene texture")) return false;
        if (!sceneSurface && error(scene->GetSurfaceLevel(0, &sceneSurface), "scene surface")) return false;
        if(colorOnly)return true; // HDR conversion needs no depth snapshot, AO targets or effect shaders.
        depthSnapshotFormat=depthFormat;
        if (!depthTex) {
            HRESULT hr=ext->CreateTexture(w, h, 1, depthSnapshotUsage(), depthSnapshotFormat, D3DPOOL_DEFAULT, &depthTex, nullptr);
            // A DXVK without the DF formats (d3d9.supportDFFormats off) refuses DF24: INTZ, not the effects disabled.
            if (FAILED(hr) && depthSnapshotFormat==df24) {
                logf("Depth snapshot DF24 refused HRESULT=0x%08lx: INTZ for this session", (unsigned long)hr);
                df24Refused=true;depthSnapshotFormat=intz;
                hr=ext->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL, depthSnapshotFormat, D3DPOOL_DEFAULT, &depthTex, nullptr);
            }
            if (error(hr, "depth snapshot texture")) return false;
        }
        if (!ao && error(ext->CreateTexture(w/2, h/2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &ao, nullptr), "half resolution AO texture")) return false;
        if (!aoRaw && error(ext->CreateTexture(w/2, h/2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &aoRaw, nullptr), "half resolution raw AO texture")) return false;
        if (!aoSurface && error(ao->GetSurfaceLevel(0, &aoSurface), "AO surface")) return false;
        if (!aoRawSurface && error(aoRaw->GetSurfaceLevel(0, &aoRawSurface), "raw AO surface")) return false;
        logf("Resources %ux%u, AO %ux%u, format=%u depthSnapshot=%s sourceMSAA=%u", w,h,w/2,h/2,unsigned(format),depthSnapshotFormat==D3DFMT_R32F?"R32F":depthSnapshotFormat==df24?"DF24":"INTZ",unsigned(worldDepthDesc.MultiSampleType));
        return true;
    }
