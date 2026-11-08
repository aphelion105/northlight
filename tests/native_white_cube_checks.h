#pragma once
struct NativeWhiteMeter:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;unsigned reads=0,writes=0,creates=0;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT f,float* p,UINT n)override{++reads;return real->GetPixelShaderConstantF(f,p,n);}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT f,float* p,UINT n)override{++reads;return real->GetVertexShaderConstantF(f,p,n);}
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD s,IDirect3DBaseTexture9** p)override{++reads;return real->GetTexture(s,p);}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD* p)override{++reads;return real->GetSamplerState(s,k,p);}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD s,IDirect3DBaseTexture9* p)override{++writes;return real->SetTexture(s,p);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD p)override{++writes;return real->SetSamplerState(s,k,p);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT f,const float* p,UINT n)override{++writes;return real->SetPixelShaderConstantF(f,p,n);}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT f,const float* p,UINT n)override{++writes;return real->SetVertexShaderConstantF(f,p,n);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{++writes;return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* p)override{++writes;return real->SetVertexShader(p);}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* p,IDirect3DPixelShader9** o)override{++creates;return real->CreatePixelShader(p,o);}
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* p,IDirect3DVertexShader9** o)override{++creates;return real->CreateVertexShader(p,o);}
};
template<class Submit,class Read,class Fill> void nativeWhiteCubes(IDirect3DDevice9* d,NorthlightNativeLocalDraw light,
    IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,Submit submit,Read read,Fill fill,bool two){
    const auto draw=[&](auto call){assert(SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0)));assert(SUCCEEDED(d->BeginScene()));assert(SUCCEEDED(call()));assert(SUCCEEDED(d->EndScene()));return read();};
    fill(0);const auto original=draw(submit);NativeWhiteMeter meter(d);NorthlightNativeLocalLighting neutral(&meter);
    auto b=light,c=light;b.slot=1;c.slot=2;light.cubeIsWhite=b.cubeIsWhite=c.cubeIsWhite=true;
    for(unsigned count=1;count<=3;++count){const auto image=draw([&]{return neutral.draw(light,vs,ps,submit,count>=2?&b:nullptr,nullptr,nullptr,count==3?&c:nullptr);});assert(image==original);}
    assert(neutral.whiteCubeSkips==3&&!neutral.coldPairs&&!neutral.draws&&!meter.reads&&!meter.writes&&!meter.creates);
#ifdef NORTHLIGHT_TEST_NEUTRAL_GAINS
    const auto savedGain=light.gain;fill(1);light.cubeIsWhite=b.cubeIsWhite=c.cubeIsWhite=false;
    for(float gain:{0.f,-0.f})for(unsigned count=1;count<=3;++count){light.gain=b.gain=c.gain=gain;
        const auto image=draw([&]{return neutral.draw(light,vs,ps,submit,count>=2?&b:nullptr,nullptr,nullptr,count==3?&c:nullptr);});assert(image==original);}
    assert(neutral.zeroGainSkips==6&&!neutral.coldPairs&&!neutral.draws&&!meter.reads&&!meter.writes&&!meter.creates);
    light.gain=savedGain;std::puts("NATIVE_ZERO_GAIN_PASS allNeutral=6 privateReads=0 privateWrites=0 shaderCreates=0 exactNative=1");
#endif
    if(!two){std::puts("NATIVE_WHITE_CUBE_PASS dualUnsupported=1 allWhite=exact");return;}
    IDirect3DCubeTexture9* white=nullptr;assert(SUCCEEDED(d->CreateCubeTexture(256,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&white,nullptr)));
    const auto fillWhite=[&](bool entry){for(unsigned face=0;face<6;++face){D3DLOCKED_RECT lock;assert(SUCCEEDED(white->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0)));for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=entry&&x<128?0.f:1.f;assert(SUCCEEDED(white->UnlockRect(D3DCUBEMAP_FACES(face),0)));}};
    fill(2);
    for(bool whiteFirst:{false,true}){
        fillWhite(false);NorthlightNativeLocalLighting mixed(&meter);auto a=light;b=light;b.slot=1;
        a.cube=whiteFirst?white:light.cube;b.cube=whiteFirst?light.cube:white;a.cubeIsWhite=b.cubeIsWhite=false;
        for(unsigned frame=1;frame<=3;++frame){a.rendererFrame=frame;draw([&]{return mixed.draw(a,vs,ps,submit,&b);});}
        const auto creates=meter.creates;
#ifdef NORTHLIGHT_TEST_NEUTRAL_GAINS
        a.cubeIsWhite=b.cubeIsWhite=false;
        for(unsigned slot=0;slot<2;++slot){a.gain=slot==0?0.f:1.f;b.gain=slot==1?0.f:1.f;
            mixed.testZeroGainFastPath=false;const auto control=draw([&]{return mixed.draw(a,vs,ps,submit,&b);});
            mixed.testZeroGainFastPath=true;const auto optimized=draw([&]{return mixed.draw(a,vs,ps,submit,&b);});assert(control==optimized);}
        assert(mixed.zeroGainBypasses);a.gain=b.gain=1;
        std::puts("NATIVE_ZERO_GAIN_MIXED_PASS firstAndSecond=exact unchangedRecipe=1");
#endif
        const auto mark=[&](bool proof){a.cubeIsWhite=whiteFirst&&proof;b.cubeIsWhite=!whiteFirst&&proof;};
        for(float gain:{0.f,.3f,1.f}){
            (whiteFirst?a:b).gain=gain;mark(false);const auto control=draw([&]{return mixed.draw(a,vs,ps,submit,&b);});mark(true);const auto optimized=draw([&]{return mixed.draw(a,vs,ps,submit,&b);});assert(control==optimized);
        }
        (whiteFirst?a:b).gain=1;
        fillWhite(true);mark(false);const auto entry=draw([&]{return mixed.draw(a,vs,ps,submit,&b);});const auto entryRepeat=draw([&]{return mixed.draw(a,vs,ps,submit,&b);});assert(entry==entryRepeat);
        fillWhite(false);mark(false);const auto removed=draw([&]{return mixed.draw(a,vs,ps,submit,&b);});mark(true);const auto removedProof=draw([&]{return mixed.draw(a,vs,ps,submit,&b);});assert(removed==removedProof&&removed!=entry);
        assert(meter.creates==creates&&!mixed.whiteCubeSkips&&mixed.multiDraws&&mixed.whiteCubeBypasses);
    }
    white->Release();fill(0);
    assert(meter.reads&&meter.writes&&meter.creates);std::puts("NATIVE_WHITE_CUBE_PASS dual=exact firstAndSecond=1 gains=0,.3,1 casterEntryRemoval=1 noVariantOrCreateChange=1");
}
