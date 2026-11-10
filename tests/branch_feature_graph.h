#pragma once
// Additional production screen passes on the same world graph. Native draw
// capture, material generation, water capture, UI and Present remain outside it.
class BranchFeatureGraph {
    IDirect3DDevice9* d;unsigned flags,w,h;
    IDirect3DTexture9 *scene,*depth,*lensScene=nullptr;IDirect3DSurface9* lensSurface=nullptr;
    NorthlightHdr::Frame hdr;NorthlightHdr::Settings settings;
    NorthlightSurfaceRenderer surface;NorthlightSurfaceFrame weather{};
public:
    BranchFeatureGraph(IDirect3DDevice9* device,unsigned width,unsigned height,unsigned enabled,IDirect3DTexture9* color,IDirect3DTexture9* z,const float* c)
        :d(device),flags(enabled),w(width),h(height),scene(color),depth(z),surface(device){
        settings.requested=flags&1;settings.wideBloom=flags&16;
        weather.c[0][0]=1.f/w;weather.c[0][1]=1.f/h;weather.c[0][2]=c[2];weather.c[0][3]=c[3];
        weather.c[1][0]=c[4];weather.c[1][1]=c[5];weather.c[1][2]=-1;weather.c[2][1]=1;
        weather.c[3][0]=.8f;weather.c[3][1]=bool(flags&4);weather.c[4][1]=1;weather.c[5][2]=1;
        weather.c[6][0]=-1;weather.c[7][2]=3;weather.c[7][3]=1;weather.c[8][0]=16;weather.c[8][1]=2;weather.c[8][2]=9;weather.c[9][1]=.1f;
        if(flags&14){const auto format=flags&1?D3DFMT_A16B16G16R16F:D3DFMT_A8R8G8B8;
            benchOk(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&lensScene,nullptr));benchOk(lensScene->GetSurfaceLevel(0,&lensSurface));}
    }
    ~BranchFeatureGraph(){drop(lensSurface);drop(lensScene);}
    void prepare(float minZ,float maxZ,NorthlightStateBlockPool& pool,NorthlightGpuProfile* profile){
        if(flags&1)benchOk(hdr.prepareLegacy(d,scene,depth,nullptr,w,h,minZ,maxZ,settings,&pool,false));
        if(profile)profile->mark("HDRPrepare");
    }
    void finish(IDirect3DSurface9* target,unsigned frame,SavedState& outer,NorthlightStateBlockPool& pool,NorthlightGpuProfile* profile){
        const double time=double(frame)/60;weather.c[1][3]=float(time);NorthlightQuality::Settings quality{};
        if(flags&14){surface.setMotion(weather,flags&8?3:flags&4?2:1,quality,time);
            if(!(flags&8))surface.precipitation(target,depth,w,h,weather,NorthlightQuality::Preset::Quality,nullptr,&outer);
            if(profile)profile->mark("Precipitation");
            if(flags&8)surface.sandLens(target,w,h,time,1,nullptr,1,1,&outer);
            else surface.lens(target,lensScene,lensSurface,w,h,flags&4,time,.8f,nullptr,1,0,1,true,flags&4?1.f:0.f,&outer);
            if(profile)profile->mark("WeatherLens");
        }
        if(flags&1)benchOk(hdr.finish(d,target,target,settings,1.f/60,true,false,.18f,&pool));
        if(profile)profile->mark("HDRPost");
    }
};
