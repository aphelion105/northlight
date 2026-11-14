// Optional screen-space filtering of the existing directional maps.
    IDirect3DTexture9* softVisibility=nullptr;
    IDirect3DSurface9* softVisibilitySurface=nullptr;
    IDirect3DPixelShader9 *directionalSoftPS=nullptr,*lightingSoftPS=nullptr;
    IDirect3DPixelShader9 *sharedDirectionalPS=nullptr,*lightingSharedPS=nullptr;
    bool sharedShadowTried=false;
    DWORD softShadowRetryAt=0;unsigned softShadowFailures=0;
    void releaseSoftShadows(){
        resetShadowTransitions();
        drop(softVisibilitySurface);drop(softVisibility);drop(directionalSoftPS);drop(lightingSoftPS);softShadowRetryAt=0;
        drop(sharedDirectionalPS);drop(lightingSharedPS);sharedShadowTried=false;
    }
    bool sharedShadowResources(){
        if(!sharedShadowTried){
            sharedShadowTried=true;D3DCAPS9 caps={};
            if(SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPixelShader30InstructionSlots>=kUnifiedDirectionalShadowShaderSlots){
                if(FAILED(d->CreatePixelShader(kUnifiedDirectionalShadowShader,&sharedDirectionalPS))||
                   FAILED(d->CreatePixelShader(kWorldLightingSharedShader,&lightingSharedPS))){drop(sharedDirectionalPS);drop(lightingSharedPS);}
            }
        }
        return sharedDirectionalPS&&lightingSharedPS;
    }
    bool softShadowResources(UINT w,UINT h){
        if(softVisibilitySurface&&directionalSoftPS&&lightingSoftPS)return true;
        if(softShadowRetryAt&&LONG(GetTickCount()-softShadowRetryAt)<0)return false;
        const auto budget=NorthlightGeometryMemory::defaultTargetBudget();
        if(!NorthlightGeometryMemory::admits(geometryAdmission(budget),budget)){softShadowRetryAt=GetTickCount()+1000;return false;}
        HRESULT hr=d->CreatePixelShader(kDirectionalShadowShader,&directionalSoftPS);
        if(SUCCEEDED(hr))hr=d->CreatePixelShader(kWorldLightingSoftShader,&lightingSoftPS);
        if(SUCCEEDED(hr))hr=d->CreateTexture(w/2,h/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&softVisibility,nullptr);
        if(SUCCEEDED(hr)&&softVisibility)hr=softVisibility->GetSurfaceLevel(0,&softVisibilitySurface);
        if(SUCCEEDED(hr)&&softVisibilitySurface)return true;
        releaseSoftShadows();softShadowRetryAt=GetTickCount()+1000;
        if(softShadowFailures++<4)logf("WORLD PCSS unavailable hr=%08lx; ordinary shadows retained",(unsigned long)hr);
        return false;
    }
