#pragma once
// Sample the GPU resource; DEFAULT textures have no readable CPU lock.
inline std::vector<uint8_t> readShadowAlpha(IDirect3DDevice9* d,IDirect3DTexture9* texture){
    D3DSURFACE_DESC desc{};assert(SUCCEEDED(texture->GetLevelDesc(0,&desc)));
    IDirect3DStateBlock9* state=nullptr;assert(SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&state)));
    IDirect3DSurface9 *previous=nullptr,*depth=nullptr,*target=nullptr,*readback=nullptr;
    assert(SUCCEEDED(d->GetRenderTarget(0,&previous)));d->GetDepthStencilSurface(&depth);
    assert(SUCCEEDED(d->SetDepthStencilSurface(nullptr)));
    assert(SUCCEEDED(d->CreateRenderTarget(desc.Width,desc.Height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr)));
    assert(SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr)));
    assert(SUCCEEDED(d->SetRenderTarget(0,target)));D3DVIEWPORT9 view{0,0,desc.Width,desc.Height,0,1};assert(SUCCEEDED(d->SetViewport(&view)));
    assert(SUCCEEDED(d->SetVertexShader(nullptr))&&SUCCEEDED(d->SetPixelShader(nullptr)));
    for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_LIGHTING,D3DRS_FOGENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})assert(SUCCEEDED(d->SetRenderState(s,FALSE)));
    assert(SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,15)));
    assert(SUCCEEDED(d->SetTexture(0,texture)));
    assert(SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1))&&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE|D3DTA_ALPHAREPLICATE)));
    assert(SUCCEEDED(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE)));
    assert(SUCCEEDED(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0))&&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE)));
    for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})assert(SUCCEEDED(d->SetSamplerState(0,s,D3DTEXF_POINT)));
    assert(SUCCEEDED(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE))&&SUCCEEDED(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,0)));
    struct V{float x,y,z,w,u,v;};const float x=float(desc.Width)-.5f,y=float(desc.Height)-.5f;
    const V quad[]={{-.5f,-.5f,.5f,1,0,0},{x,-.5f,.5f,1,1,0},{-.5f,y,.5f,1,0,1},{x,y,.5f,1,1,1}};
    assert(SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1))&&SUCCEEDED(d->BeginScene()));assert(SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V))));assert(SUCCEEDED(d->EndScene()));
    assert(SUCCEEDED(d->GetRenderTargetData(target,readback)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
    std::vector<uint8_t> result(size_t(desc.Width)*desc.Height);
    for(unsigned j=0;j<desc.Height;++j)for(unsigned i=0;i<desc.Width;++i)result[size_t(j)*desc.Width+i]=static_cast<uint8_t*>(lock.pBits)[size_t(j)*lock.Pitch+i*4];
    assert(SUCCEEDED(readback->UnlockRect()));assert(SUCCEEDED(d->SetRenderTarget(0,previous))&&SUCCEEDED(d->SetDepthStencilSurface(depth))&&SUCCEEDED(state->Apply()));
    state->Release();previous->Release();if(depth)depth->Release();target->Release();readback->Release();return result;
}
