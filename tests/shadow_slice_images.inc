// Uses the native model/camera fixture's frozen current bank and source mesh.
static void shadowSliceImages(const std::string& dir,IDirect3DVertexDeclaration9* declaration,const std::vector<unsigned char>& mesh,unsigned vertices,unsigned indices){
    SavedState saved(d);assert(saved.ok);
    auto output=target(128,128,D3DFMT_R32F,4);IDirect3DSurface9* depth=nullptr;ok(d->CreateDepthStencilSurface(128,128,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    IDirect3DTexture9* alpha=nullptr;ok(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&alpha,nullptr));D3DLOCKED_RECT locked{};ok(alpha->LockRect(0,&locked,nullptr,0));
    const DWORD pattern[]={0x00ffffff,0x40ffffff,0xc0ffffff,0xffffffff};for(unsigned y=0;y<2;++y)std::memcpy(static_cast<char*>(locked.pBits)+y*locked.Pitch,pattern+y*2,8);ok(alpha->UnlockRect(0));
    auto directional=ps(dir+"ShadowReplayPS.bin"),point=ps(dir+"LocalShadowPS.bin");
    for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,output.s));ok(d->SetDepthStencilSurface(depth));viewport(output);ok(d->SetVertexDeclaration(declaration));
    for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_WRAP0,D3DRS_WRAP7})ok(d->SetRenderState(key,0));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));bind(0,alpha);
    const float sphere[]={1000000,1.f/128,0,0};ok(d->SetPixelShaderConstantF(1,sphere,1));unsigned cases=0,programs=0;
    float original[256][4];ok(d->GetVertexShaderConstantF(0,original[0],256));
    for(const char* skin:{"rigid","skinned","weighted","weighted4","envelope"}){
        const auto bytes=read(dir+"model-vs3-"+skin+".bin");std::vector<uint32_t> full,compact;
        assert(NorthlightShadowShader::patch(reinterpret_cast<const uint32_t*>(bytes.data()),bytes.size()/4,full));
        assert(NorthlightModelNormalSlice::shadowVertex(full.data(),full.size(),compact)&&compact.size()<full.size());
        IDirect3DVertexShader9 *before=nullptr,*after=nullptr;ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(full.data()),&before));ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(compact.data()),&after));
        unsigned opaquePixels=0;
        for(auto* pixel:{directional,point})for(float opacity:{0.f,.35f,.85f,1.f})for(unsigned mode=0;mode<4;++mode)for(float cutoff:{-1.f,.5f}){
            auto native=std::array<std::array<float,4>,256>{};std::memcpy(native.data(),original,sizeof original);native[11][3]=opacity;ok(d->SetVertexShaderConstantF(0,native[0].data(),256));
            const float material[]={float(mode),0,0,cutoff};ok(d->SetPixelShaderConstantF(0,material,1));ok(d->SetPixelShader(pixel));std::vector<unsigned char> reference;
            for(auto* vertex:{before,after}){ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->SetVertexShader(vertex));ok(d->BeginScene());ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,vertices,indices/3,mesh.data()+12+48*vertices,D3DFMT_INDEX32,mesh.data()+12,48));ok(d->EndScene());auto image=pixels(output);
                if(vertex==before){reference=std::move(image);if(cutoff<0)for(size_t at=0;at<reference.size();at+=4){float z;std::memcpy(&z,reference.data()+at,4);opaquePixels+=z!=1.f;}}
                else assert(reference==image);
            }++cases;
        }
        assert(opaquePixels>256);++programs;std::printf("SHADOW_SLICE_IMAGE_CASE program=%s fullWords=%zu compactWords=%zu opaquePixels=%u\n",skin,full.size(),compact.size(),opaquePixels);before->Release();after->Release();
    }
    directional->Release();point->Release();alpha->Release();depth->Release();output.read->Release();output.s->Release();output.t->Release();
    std::printf("SHADOW_SLICE_IMAGE_PASS programs=%u cases=%u pixels=bitexact directionalAndPoint=1 nativeAlphaModes=4 textureCutouts=1\n",programs,cases);
}
