// Included inside WorldRenderer. Resource groups follow the frame's consumers;
// completed groups remain warm across switches until reset, resize or pressure.
unsigned screenGroups=0;

void releaseScreenFallbacks(){
    screenFallbacks.releaseScreen();
    rainMask=nullptr;
}

void releaseScreenColorTargets(){
    screenLighting.releaseRawTarget();
    fogScreen.reset();
}

bool createResources(UINT w,UINT h,D3DFORMAT fmt,const NorthlightFrameRequirements::Plan& needs){
    if(width!=w||height!=h){releaseGPU(true);width=w;height=h;}
    const auto required=needs.screenGroups();
    if((screenGroups&required)==required&&shadowPrograms.liveLayout())return true;
    auto pixel=[&](const DWORD* code,IDirect3DPixelShader9*& shader,const char* name){
        return shader||check(NorthlightScreen::createPixelShader(d,code,shader),name);
    };
    const auto checkTarget=[&](HRESULT hr,const char* step){return check(hr,step);};
    auto image=[&](D3DFORMAT format,IDirect3DTexture9*& texture,IDirect3DSurface9*& surface){
        return NorthlightWorldRenderTarget::ensure(d,w/2,h/2,format,texture,surface,checkTarget,
            "world render texture","world render surface");
    };
    if(!composite.createStandard(pixel))return false;
    if(needs.lighting){
        if(!screenLighting.createDirectional(pixel)||
           !history.createLightingShaders(pixel,needs.shadows)||!worldNormals.createShader(pixel))return false;
        if(!screenLighting.createTargets(image)||
           !worldNormals.createTarget(image))return false;
        if(needs.shadows&&!fogScreen.createScratch(image))return false;
        if(!history.createLightTargets(image))return false;
    }
    if(needs.distance&&!history.createDistance(pixel,image))return false;
    if(needs.probes&&!screenLighting.createGi(pixel))return false;
    if(needs.local&&!localLighting.createDirect(pixel))return false;
    if(needs.fog){
        if(!foreverFog.createBase(d))logf("FOREVER atmosphere shader unavailable; regional fallback retained");
        if(!regionalFogGpu.createBase(pixel)||!fogScreen.create(pixel,image))return false;
        if(needs.local&&!localLighting.createFog(pixel))return false;
        if(!(screenGroups&4u)){
            regionalFogGpu.createOptional(d);
            fogScreen.createOptionalHistory(d);
        }
    }
    if(needs.materials&&!composite.createMaterial(pixel))return false;
    if(needs.shadows){
        if(!shadowPrograms.createDirectional(d,[&](HRESULT hr,const char* name){return check(hr,name);}))return false;
        if(!(screenGroups&16u))shadowPrograms.createOptionalCache(d);
        D3DCAPS9 caps={};d->GetDeviceCaps(&caps);prepareActorShaders(caps);
    }
    if(!screenFallbacks.create(d,[&](HRESULT hr,const char* action){return check(hr,action);}))return false;
    static_assert(sizeof(StaticShadow::Vertex)==20,"live terrain shadow vertex stride");
    if(!check(shadowPrograms.createWorldLayout(d),"shadow declaration")||
       !check(shadowPrograms.createLiveLayout(d),"live terrain declaration"))return false;
    screenGroups|=required;return true;
}
bool resources(UINT w,UINT h,D3DFORMAT fmt,const NorthlightFrameRequirements::Plan& needs){
    if(width==w&&height==h&&shadowPrograms.liveLayout()&&(screenGroups&needs.screenGroups())==needs.screenGroups())return true;
    const DWORD now=GetTickCount();if(screenResourceRetry&&DWORD(now-screenResourceRetryAt)<1000)return false;
    screenResourceOom=false;
    struct Scope {bool& flag;explicit Scope(bool& value):flag(value){flag=true;}~Scope(){flag=false;}};
    bool ok;{Scope scope(buildingScreenResources);ok=createResources(w,h,fmt,needs);}
    if(!ok&&screenResourceOom){releaseGPU(true);screenResourceRetry=true;screenResourceRetryAt=now;if(!screenResourceReported||DWORD(now-screenResourceReportAt)>=60000){screenResourceReported=true;screenResourceReportAt=now;logf("WORLD screen resources deferred: out of memory retryMs=1000");}}
    else if(ok)screenResourceRetry=false;
    return ok;
}
bool resources(UINT w,UINT h,D3DFORMAT fmt){return resources(w,h,fmt,frameRequirements(quality.autoBump));}
