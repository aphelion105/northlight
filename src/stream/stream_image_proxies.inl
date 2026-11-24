// Surface, volume, and texture adapters for the image lock implementation.
#define NL_TEX_LOCALS(S) \
    void observe(CmdTag<Cmd::S##_GenerateMipSubLevels>){for(std::size_t i=0;i<subs.size();++i)if(i%info.levels>0)markGpuWritten(*core,subs[i]);} \
    DWORD local(CmdTag<Cmd::S##_SetLOD>,DWORD v){const DWORD o=lod;if(info.pool==D3::kPoolManaged)lod=v<info.levels?v:info.levels-1;return o;} \
    DWORD local(CmdTag<Cmd::S##_GetLOD>){return lod;} \
    DWORD local(CmdTag<Cmd::S##_GetLevelCount>){return (info.usage&D3::kUsageAutoGen)?1:info.levels;} \
    D3DTEXTUREFILTERTYPE local(CmdTag<Cmd::S##_GetAutoGenFilterType>){return (D3DTEXTUREFILTERTYPE)autoGenFilter;} \
    void observe(CmdTag<Cmd::S##_SetAutoGenFilterType>,D3DTEXTUREFILTERTYPE f){autoGenFilter=unsigned(f);}

struct StreamSurface final:IDirect3DSurface9,ProxyBase {
    SubRes own;SubRes* subp=&own;UINT sub=0;   // child of a texture level/face: subp points into the container's table
    StreamSurface(StreamCore* c,const Info& i,ProxyBase* container=nullptr,SubRes* shared=nullptr):ProxyBase(c,Kind::Surface){
        info=i;info.type=D3::kTypeSurface;parent=container;if(shared)subp=shared;
        own.written=(i.usage&(D3::kUsageRT|D3::kUsageDS))!=0;
        ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DSurface9*>(this));
    }
    ~StreamSurface(){dropSubShadow(*core,own);dropSubStage(*core,own);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_SURFACE_METHODS
    NL_PROXY_COM(IDirect3DSurface9,IDirect3DResource9)
    NL_RES_LOCALS(Surface)
    HRESULT local(CmdTag<Cmd::Surface_GetDesc>,D3DSURFACE_DESC* d){
        if(!d)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_SURFACE;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->MultiSampleType=(D3DMULTISAMPLE_TYPE)info.ms;
        d->MultiSampleQuality=info.msq;d->Width=info.w;d->Height=info.h;return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID id,void** pp) override{
        if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;
        if(parent)return parent->unk->QueryInterface(id,pp);
        return core->game->QueryInterface(id,pp);
    }
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* lr,const RECT* rect,DWORD flags) override{
        return lockImage(*this,parent?*parent:static_cast<ProxyBase&>(*this),*subp,RouteSurface,0,0,info.w,info.h,1,lr,nullptr,rect,nullptr,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockRect() override{return unlockImage(*this,*subp,RouteSurface,0,0);}
};
struct StreamVolume final:IDirect3DVolume9,ProxyBase {
    SubRes* subp;
    StreamVolume(StreamCore* c,const Info& i,ProxyBase* container,SubRes* shared):ProxyBase(c,Kind::Volume),subp(shared){
        info=i;info.type=D3::kTypeVolume;parent=container;ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVolume9*>(this));}
    ~StreamVolume(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VOLUME_METHODS
    NL_PROXY_COM(IDirect3DVolume9)
    HRESULT local(CmdTag<Cmd::Volume_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    HRESULT local(CmdTag<Cmd::Volume_SetPrivateData>,REFGUID g,const void* d,DWORD n,DWORD f){return setPrivate(g,d,n,f);}
    HRESULT local(CmdTag<Cmd::Volume_GetPrivateData>,REFGUID g,void* d,DWORD* n){return getPrivate(g,d,n);}
    HRESULT local(CmdTag<Cmd::Volume_FreePrivateData>,REFGUID g){return freePrivate(g);}
    HRESULT local(CmdTag<Cmd::Volume_GetDesc>,D3DVOLUME_DESC* d){
        if(!d)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_VOLUME;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->Width=info.w;d->Height=info.h;d->Depth=info.d;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID id,void** pp) override{if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;return parent?parent->unk->QueryInterface(id,pp):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX* lb,const D3DBOX* box,DWORD flags) override{
        return lockImage(*this,*parent,*subp,RouteVolume,0,0,info.w,info.h,info.d,nullptr,lb,nullptr,box,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockBox() override{return unlockImage(*this,*subp,RouteVolume,0,0);}
};


struct StreamTexture final:IDirect3DTexture9,ProxyBase {
    std::vector<SubRes> subs;DWORD lod=0;unsigned autoGenFilter=2;   // D3DTEXF_LINEAR
    StreamTexture(StreamCore* c,UINT w,UINT h,UINT levels,DWORD usage,unsigned fmt,DWORD pool):ProxyBase(c,Kind::Texture){
        info.w=w;info.h=h;info.levels=levels?levels:D3::fullChain(w,h,1);info.usage=usage;info.fmt=fmt;info.pool=pool;info.type=D3::kTypeTexture;
        subs.resize(info.levels);kids.assign(info.levels,nullptr);
        for(UINT i=0;i<info.levels;++i)subs[i].written=(usage&(D3::kUsageRT|D3::kUsageDS))||((usage&D3::kUsageAutoGen)&&i>0);
        if(usage&D3::kUsageAutoGen)for(UINT i=1;i<info.levels;++i)subs[i].shadowDead=true;   // the GPU writes the mips
        ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DTexture9*>(this));}
    ~StreamTexture(){for(auto& s:subs){dropSubShadow(*core,s);dropSubStage(*core,s);}liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_TEXTURE_METHODS
    NL_PROXY_COM(IDirect3DTexture9,IDirect3DBaseTexture9,IDirect3DResource9)
    NL_RES_LOCALS(Texture)
    NL_TEX_LOCALS(Texture)
    HRESULT local(CmdTag<Cmd::Texture_GetLevelDesc>,UINT level,D3DSURFACE_DESC* d){
        if(!d||level>=info.levels)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_SURFACE;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->MultiSampleType=(D3DMULTISAMPLE_TYPE)0;d->MultiSampleQuality=0;
        d->Width=D3::mipDim(info.w,level);d->Height=D3::mipDim(info.h,level);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level,IDirect3DSurface9** pp) override{
        if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;if(level>=info.levels)return D3DERR_INVALIDCALL;
        auto* kid=static_cast<StreamSurface*>(kids[level]);
        if(!kid){
            Info i=info;i.w=D3::mipDim(info.w,level);i.h=D3::mipDim(info.h,level);i.levels=1;
            try{kid=new StreamSurface(core,i,this,&subs[level]);}catch(...){return E_OUTOFMEMORY;}
            kids[level]=kid;adoptKid();   // the new kid's one game reference keeps this texture alive
            recordDerive(*core,this,kid,DeriveSurfaceLevel,level,0);
        }else kid->comAddRef();
        *pp=kid;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE LockRect(UINT level,D3DLOCKED_RECT* lr,const RECT* rect,DWORD flags) override{
        if(level>=info.levels)return D3DERR_INVALIDCALL;
        return lockImage(*this,*this,subs[level],RouteTexture,level,0,D3::mipDim(info.w,level),D3::mipDim(info.h,level),1,lr,nullptr,rect,nullptr,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override{if(level>=info.levels)return D3DERR_INVALIDCALL;return unlockImage(*this,subs[level],RouteTexture,level,0);}
};
struct StreamCubeTexture final:IDirect3DCubeTexture9,ProxyBase {
    std::vector<SubRes> subs;DWORD lod=0;unsigned autoGenFilter=2;
    StreamCubeTexture(StreamCore* c,UINT edge,UINT levels,DWORD usage,unsigned fmt,DWORD pool):ProxyBase(c,Kind::CubeTexture){
        info.w=info.h=edge;info.levels=levels?levels:D3::fullChain(edge,edge,1);info.usage=usage;info.fmt=fmt;info.pool=pool;info.type=D3::kTypeCube;
        subs.resize(std::size_t(info.levels)*6);kids.assign(subs.size(),nullptr);
        for(std::size_t i=0;i<subs.size();++i)subs[i].written=(usage&(D3::kUsageRT|D3::kUsageDS))||((usage&D3::kUsageAutoGen)&&(i%info.levels)>0);
        ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DCubeTexture9*>(this));}
    ~StreamCubeTexture(){for(auto& s:subs){dropSubShadow(*core,s);dropSubStage(*core,s);}liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_CUBETEXTURE_METHODS
    NL_PROXY_COM(IDirect3DCubeTexture9,IDirect3DBaseTexture9,IDirect3DResource9)
    NL_RES_LOCALS(CubeTexture)
    NL_TEX_LOCALS(CubeTexture)
    HRESULT local(CmdTag<Cmd::CubeTexture_GetLevelDesc>,UINT level,D3DSURFACE_DESC* d){
        if(!d||level>=info.levels)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_SURFACE;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->MultiSampleType=(D3DMULTISAMPLE_TYPE)0;d->MultiSampleQuality=0;
        d->Width=D3::mipDim(info.w,level);d->Height=D3::mipDim(info.h,level);return D3D_OK;}
    std::size_t idx(UINT face,UINT level)const{return std::size_t(face)*info.levels+level;}
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face,UINT level,IDirect3DSurface9** pp) override{
        if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;if(level>=info.levels||unsigned(face)>=6)return D3DERR_INVALIDCALL;
        const std::size_t k=idx(unsigned(face),level);auto* kid=static_cast<StreamSurface*>(kids[k]);
        if(!kid){
            Info i=info;i.w=D3::mipDim(info.w,level);i.h=D3::mipDim(info.h,level);i.levels=1;
            try{kid=new StreamSurface(core,i,this,&subs[k]);}catch(...){return E_OUTOFMEMORY;}
            kids[k]=kid;adoptKid();
            recordDerive(*core,this,kid,DeriveCubeFace,unsigned(face),level);
        }else kid->comAddRef();
        *pp=kid;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face,UINT level,D3DLOCKED_RECT* lr,const RECT* rect,DWORD flags) override{
        if(level>=info.levels||unsigned(face)>=6)return D3DERR_INVALIDCALL;
        return lockImage(*this,*this,subs[idx(unsigned(face),level)],RouteCube,level,unsigned(face),D3::mipDim(info.w,level),D3::mipDim(info.h,level),1,lr,nullptr,rect,nullptr,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face,UINT level) override{
        if(level>=info.levels||unsigned(face)>=6)return D3DERR_INVALIDCALL;return unlockImage(*this,subs[idx(unsigned(face),level)],RouteCube,level,unsigned(face));}
};
struct StreamVolumeTexture final:IDirect3DVolumeTexture9,ProxyBase {
    std::vector<SubRes> subs;DWORD lod=0;unsigned autoGenFilter=2;
    StreamVolumeTexture(StreamCore* c,UINT w,UINT h,UINT d,UINT levels,DWORD usage,unsigned fmt,DWORD pool):ProxyBase(c,Kind::VolumeTexture){
        info.w=w;info.h=h;info.d=d;info.levels=levels?levels:D3::fullChain(w,h,d);info.usage=usage;info.fmt=fmt;info.pool=pool;info.type=D3::kTypeVolumeTexture;
        subs.resize(info.levels);kids.assign(info.levels,nullptr);
        ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVolumeTexture9*>(this));}
    ~StreamVolumeTexture(){for(auto& s:subs){dropSubShadow(*core,s);dropSubStage(*core,s);}liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VOLUMETEXTURE_METHODS
    NL_PROXY_COM(IDirect3DVolumeTexture9,IDirect3DBaseTexture9,IDirect3DResource9)
    NL_RES_LOCALS(VolumeTexture)
    NL_TEX_LOCALS(VolumeTexture)
    HRESULT local(CmdTag<Cmd::VolumeTexture_GetLevelDesc>,UINT level,D3DVOLUME_DESC* d){
        if(!d||level>=info.levels)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_VOLUME;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;
        d->Width=D3::mipDim(info.w,level);d->Height=D3::mipDim(info.h,level);d->Depth=D3::mipDim(info.d,level);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT level,IDirect3DVolume9** pp) override{
        if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;if(level>=info.levels)return D3DERR_INVALIDCALL;
        auto* kid=static_cast<StreamVolume*>(kids[level]);
        if(!kid){
            Info i=info;i.w=D3::mipDim(info.w,level);i.h=D3::mipDim(info.h,level);i.d=D3::mipDim(info.d,level);i.levels=1;
            try{kid=new StreamVolume(core,i,this,&subs[level]);}catch(...){return E_OUTOFMEMORY;}
            kids[level]=kid;adoptKid();recordDerive(*core,this,kid,DeriveVolumeLevel,level,0);
        }else kid->comAddRef();
        *pp=kid;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE LockBox(UINT level,D3DLOCKED_BOX* lb,const D3DBOX* box,DWORD flags) override{
        if(level>=info.levels)return D3DERR_INVALIDCALL;
        return lockImage(*this,*this,subs[level],RouteVolumeTexture,level,0,D3::mipDim(info.w,level),D3::mipDim(info.h,level),D3::mipDim(info.d,level),nullptr,lb,nullptr,box,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockBox(UINT level) override{if(level>=info.levels)return D3DERR_INVALIDCALL;return unlockImage(*this,subs[level],RouteVolumeTexture,level,0);}
};

#undef NL_TEX_LOCALS
