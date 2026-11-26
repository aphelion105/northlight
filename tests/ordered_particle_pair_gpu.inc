#include "game_particle_pair.h"
#include "effect_coverage.h"

static void orderedParticlePair(PaletteDevice& d){
    using NorthlightDrawSnapshot::Ref;
    using namespace NorthlightOrderedParticles;
    constexpr UINT size=64;
    const D3DVERTEXELEMENT9 layout[]={
        {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,16,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
        {0,32,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    struct Vertex {float position[4],color[4],uv[2];};
    const Vertex quads[]={
        {{-.6f,-.6f,.5f,1},{1,0,0,.5f},{-.25f,0}},{{.6f,-.6f,.5f,1},{1,0,0,.5f},{1.25f,0}},
        {{-.6f,.6f,.5f,1},{1,0,0,.5f},{-.25f,1}},{{.6f,.6f,.5f,1},{1,0,0,.5f},{1.25f,1}},
        {{-.4f,-.4f,.5f,1},{0,1,0,.5f},{0,0}},{{.4f,-.4f,.5f,1},{0,1,0,.5f},{1,0}},
        {{-.4f,.4f,.5f,1},{0,1,0,.5f},{0,1}},{{.4f,.4f,.5f,1},{0,1,0,.5f},{1,1}}};
    const std::uint16_t indices[]={0,1,2,2,1,3,4,5,6,6,5,7};
    const DWORD vertex[]={0xfffe0300,0x0200001f,0x80000000,dst(1,0),0x0200001f,0x8000000a,dst(1,1),
        0x0200001f,0x80000005,dst(1,2,3),0x0200001f,0x80000000,dst(6,0),
        0x0200001f,0x8000000a,dst(6,1),0x0200001f,0x80000005,dst(6,2,3),
        0x02000001,dst(6,0),src(1,0),0x02000001,dst(6,1),src(1,1),0x02000001,dst(6,2,3),src(1,2),65535};
    const DWORD pixel[]={0xffff0300,0x0200001f,0x8000000a,dst(1,0),
        0x02000001,dst(8,0),src(1,0),65535};
    const DWORD textured[]={0xffff0300,0x0200001f,0x8000000a,dst(1,0),
        0x0200001f,0x90000000,dst(10,1),0x03000042,dst(0,0),src(1,0),src(10,1),
        0x03000005,dst(8,0),src(1,0),src(0,0),65535};
    const DWORD atlasPixel[]={0xffff0300,0x0200001f,0x8000000a,dst(1,0),0x0200001f,0x80000005,dst(1,1,3),
        0x0200001f,0x90000000,dst(10,0),0x03000042,dst(0,0),src(1,1),src(10,0),
        0x03000005,dst(8,0),src(1,0),src(0,0),65535};
    std::vector<DWORD> transformedVertex(vertex,vertex+std::size(vertex)-4);
    transformedVertex.insert(transformedVertex.begin()+1,{0x05000051,0xa00f0000,0x3f800000,0,0,0});
    transformedVertex.insert(transformedVertex.end(),{0x04000004,0x80070000,0x90c40002,0xa0d00000,0xa0c50000,
        0x03000008,0xe0010002,0xa0f40006,0x80e40000,0x03000008,0xe0020002,0xa0f40007,0x80e40000,
        0x03000005,0x800f0001,0x90e40001,0xa0000009});
    for(unsigned row=31;row<=43;++row)
        transformedVertex.insert(transformedVertex.end(),{0x03000005,0x800f0001,0x80e40001,0xa0000000u|row});
    transformedVertex.insert(transformedVertex.end(),{0x03000005,0xe00f0001,0x80e40001,0xa055000b,65535});
    Ref<IDirect3DSurface9> target,readback;
    Ref<IDirect3DVertexDeclaration9> decl;
    Ref<IDirect3DVertexBuffer9> vb;
    Ref<IDirect3DIndexBuffer9> ib;
    Ref<IDirect3DVertexShader9> vs,atlasVs;
    Ref<IDirect3DPixelShader9> ps,texturedPs,atlasPs;
    Ref<IDirect3DTexture9> texture,initialTexture,atlasSources[4];
    must(d.CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,target.out(),nullptr));
    must(d.CreateOffscreenPlainSurface(size,size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,readback.out(),nullptr));
    must(d.CreateVertexDeclaration(layout,decl.out()));
    must(d.CreateVertexBuffer(2*sizeof quads,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,vb.out(),nullptr));
    must(d.CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,ib.out(),nullptr));
    must(d.CreateVertexShader(vertex,vs.out()));
    must(d.CreateVertexShader(transformedVertex.data(),atlasVs.out()));
    must(d.CreatePixelShader(pixel,ps.out()));
    must(d.CreatePixelShader(textured,texturedPs.out()));
    must(d.CreatePixelShader(atlasPixel,atlasPs.out()));
    must(d.CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,texture.out(),nullptr));
    must(d.CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,initialTexture.out(),nullptr));
    const auto fillTexture=[](IDirect3DTexture9* tex,DWORD rgba){
        D3DLOCKED_RECT locked{};must(tex->LockRect(0,&locked,nullptr,0));
        std::memcpy(locked.pBits,&rgba,sizeof rgba);must(tex->UnlockRect(0));
    };
    fillTexture(texture.p,0xffffffff);fillTexture(initialTexture.p,0xff40ffff);
    for(unsigned tile=0;tile<3;++tile){
        must(d.CreateTexture(16,16,5,0,D3DFMT_DXT5,D3DPOOL_MANAGED,atlasSources[tile].out(),nullptr));
        for(unsigned mip=0;mip<5;++mip){
            const unsigned blocks=(unsigned(16>>mip)+3)/4;
            D3DLOCKED_RECT locked{};must(atlasSources[tile]->LockRect(mip,&locked,nullptr,0));
            for(unsigned y=0;y<blocks;++y)for(unsigned x=0;x<blocks;++x){
                const unsigned char colors[][2]={{0x00,0xf8},{0xe0,0x07},{0x1f,0x00}};
                const unsigned char bc[]={255,200,0x88,0xc6,0xfa,0x88,0xc6,0xfa,255,255,
                    colors[tile][0],colors[tile][1],0xe4,
                    static_cast<unsigned char>((x+y+mip)%2?0xe4:0x1b),0xe4,0xe4};
                std::memcpy(static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch+16*x,bc,sizeof bc);
            }
            must(atlasSources[tile]->UnlockRect(mip));
        }
    }
    must(d.CreateTexture(128,64,6,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,atlasSources[3].out(),nullptr));
    for(unsigned mip=0;mip<6;++mip){
        D3DLOCKED_RECT locked{};must(atlasSources[3]->LockRect(mip,&locked,nullptr,0));
        for(unsigned y=0;y<(64u>>mip);++y)for(unsigned x=0;x<(128u>>mip);++x){
            const DWORD color=0xc0803030u|((x*23+y*37+mip*11)&255);
            std::memcpy(static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch+4*x,&color,sizeof color);
        }
        must(atlasSources[3]->UnlockRect(mip));
    }
    {
        NorthlightGameParticleAtlas atlas(&d);
        const auto admit=[&](unsigned a,unsigned b){
            NorthlightGameParticleAtlas::Selection selected;
            for(unsigned i=0;i<3;++i){atlas.nextFrame();selected=atlas.material(atlasSources[a].p,atlasSources[b].p,true);}
            assert(selected.texture);
            return selected;
        };
        const auto ab=admit(0,1),bc=admit(1,2);
        assert(ab.texture==bc.texture&&ab.secondTile==bc.firstTile&&bc.secondTile!=ab.firstTile);
        const auto ca=atlas.material(atlasSources[2].p,atlasSources[0].p,true);
        assert(ca.texture==ab.texture&&ca.firstTile==bc.secondTile&&ca.secondTile==ab.firstTile);
    }
    const bool copies=NorthlightReplayCopies::enabled.exchange(true);
    auto* raw=ib.p;
    NorthlightTrackedBuffers::wrap<IDirect3DIndexBuffer9,ForwardIDirect3DIndexBuffer9>(&ib.p,&d,true);
    assert(raw!=ib.p);
    auto fillIndices=[&](bool corrupt){
        std::uint16_t values[std::size(indices)];
        std::copy_n(indices,std::size(indices),values);
        if(corrupt)std::swap(values[6],values[7]);
        void* bytes=nullptr;must(ib.p->Lock(0,sizeof values,&bytes,0));
        std::memcpy(bytes,values,sizeof values);must(ib.p->Unlock());
    };
    fillIndices(false);
    NorthlightReplayCopies::Slot* slot=nullptr;
    assert(!NorthlightReplayCopies::read(raw,0,sizeof indices,slot)&&slot);
    assert(NorthlightReplayCopies::beginFill(*slot,sizeof indices));
    const auto* bytes=reinterpret_cast<const unsigned char*>(indices);
    slot->data.assign(bytes,bytes+sizeof indices);
    assert(NorthlightReplayCopies::endFill(*slot,true));
    NorthlightReplayCopies::unpin(*slot);
    DeviceMirror mirror;
    ExtensionDevice ext(&d,&mirror);
    MirrorGuard owner(mirror.gate);
    const std::unordered_map<void*,NorthlightShaderReads::ReadSet> reads{
        {vs.p,NorthlightShaderReads::scan(reinterpret_cast<const std::uint32_t*>(vertex),std::size(vertex))},
        {atlasVs.p,NorthlightShaderReads::scan(reinterpret_cast<const std::uint32_t*>(transformedVertex.data()),transformedVertex.size())},
        {ps.p,NorthlightShaderReads::scan(reinterpret_cast<const std::uint32_t*>(pixel),std::size(pixel))},
        {texturedPs.p,NorthlightShaderReads::scan(reinterpret_cast<const std::uint32_t*>(textured),std::size(textured))},
        {atlasPs.p,NorthlightShaderReads::scan(reinterpret_cast<const std::uint32_t*>(atlasPixel),std::size(atlasPixel))}};
    assert(reads.at(texturedPs.p).exact&&reads.at(texturedPs.p).sampledSamplers==2);
    NorthlightGameParticlePair pair(ext,mirror,reads,true,true);
    must(ext.SetDepthStencilSurface(nullptr));
    must(ext.SetRenderTarget(0,target.p));
    must(ext.SetVertexDeclaration(decl.p));
    must(ext.SetVertexShader(vs.p));
    must(ext.SetPixelShader(ps.p));
    must(ext.SetIndices(raw));
    must(ext.SetStreamSource(0,vb.p,0,sizeof(Vertex)));
    must(ext.SetRenderState(D3DRS_ZENABLE,FALSE));
    must(ext.SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    must(ext.SetRenderState(D3DRS_STENCILENABLE,FALSE));
    must(ext.SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
    must(ext.SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    must(ext.SetRenderState(D3DRS_COLORWRITEENABLE,0xf));
    must(ext.SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
    must(ext.SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
    must(ext.SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
    must(ext.SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));
    must(ext.SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    must(ext.SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE));
    for(auto rs:{D3DRS_ALPHABLENDENABLE,D3DRS_ZWRITEENABLE}){
        DWORD value=0;must(ext.GetRenderState(rs,&value));
    }
    const Draw a{0,4,2},b{4,4,2};
    const auto upload=[&](UINT offset,const Vertex* source,UINT bytes,DWORD flags){
        void* targetBytes=nullptr;
        must(vb.p->Lock(offset,bytes,&targetBytes,flags));std::memcpy(targetBytes,source,bytes);must(vb.p->Unlock());
    };
    enum Mode { Serial, Reversed, SerialSampled, SerialAtlas, Joined, Overlap, BadIndices, Barrier, Sampled, Atlas, Routed };
    const auto render=[&](Mode mode,bool atlasWarm=false,unsigned firstTile=0,unsigned secondTile=1,bool gap=false,
                          bool varying=false,bool drain=false,bool sameTexture=false,bool differentPayload=false,
                          bool fallbackUpload=false){
        constexpr UINT half=sizeof quads/2;
        const UINT secondOffset=gap?2*half:half;
        const Draw next=gap?Draw{8,4,2}:b;
        const bool sampled=mode==SerialSampled||mode==Sampled;
        const bool useAtlas=mode==SerialAtlas||mode==Atlas||mode==Routed;
        pair.nextFrame();
        fillIndices(mode==BadIndices);
        must(ext.SetVertexShader(useAtlas?atlasVs.p:vs.p));
        must(ext.SetPixelShader(useAtlas?atlasPs.p:sampled?texturedPs.p:ps.p));
        const DWORD stage=useAtlas?0:1;
        auto* nextTexture=useAtlas?atlasSources[secondTile].p:texture.p;
        float sourceRows[35][4]={{varying?.55f:1.f,2,3,4},{},{0,varying?.6f:1.f,0,0}};
        float nextRows[35][4]={{varying?.85f:1.f,2,3,4},{},{0,varying?1.2f:1.f,0,0}};
        for(unsigned row=31;row<=43;++row){
            sourceRows[row-9][0]=varying?.97f:1.f;
            nextRows[row-9][0]=varying?1.02f:1.f;
        }
        float lookaheadRows[35][4];
        if(differentPayload){
            std::memcpy(lookaheadRows,nextRows,sizeof nextRows);
            std::memcpy(lookaheadRows[0],sourceRows[0],sizeof sourceRows[0]);
        }
        float spare[128];
        for(unsigned i=0;i<std::size(spare);++i)spare[i]=float(i+1)+(differentPayload?1000.f:0.f);
        const unsigned rowCount=varying?35:1;
        must(ext.SetTexture(stage,useAtlas?atlasSources[firstTile].p:sampled?initialTexture.p:nullptr));
        if(useAtlas){
            must(ext.SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));
            must(ext.SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_ANISOTROPIC));
            must(ext.SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_ANISOTROPIC));
            must(ext.SetSamplerState(0,D3DSAMP_MAXANISOTROPY,16));
            must(ext.SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR));
            must(ext.SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0));
            must(ext.SetRenderState(D3DRS_WRAP0,0));
            must(ext.SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_POINT));
            must(ext.SetTexture(1,initialTexture.p));
            const float transform[]={.75f,.25f,0,0,-.25f,.75f,0,.25f};
            must(ext.SetVertexShaderConstantF(6,transform,2));
            must(ext.SetVertexShaderConstantF(9,sourceRows[0],35));
            must(ext.SetVertexShaderConstantF(224,spare,32));
            NorthlightParticleUploads::atlasUploads().watch(vb.p);
            NorthlightParticleUploads::atlasUploads().invalidate(vb.p,0,half,true);
        }
        const auto retain=[&](unsigned offset,const Vertex* vertices){
            if(fallbackUpload)NorthlightParticleUploads::cache().write(vb.p,1,offset,vertices,half,false);
            else NorthlightParticleUploads::atlasUploads().write(vb.p,offset,half,reinterpret_cast<const unsigned char*>(vertices));
        };
        if(useAtlas)retain(0,quads);
        upload(0,quads,mode==Reversed?sizeof quads:half,D3DLOCK_DISCARD);
        IDirect3DVertexBuffer9* stream=nullptr;
        UINT offset=0,stride=0;
        must(ext.GetStreamSource(0,&stream,&offset,&stride));stream->Release();
        IDirect3DIndexBuffer9* bound=nullptr;
        must(ext.GetIndices(&bound));bound->Release();
        IDirect3DVertexDeclaration9* active=nullptr;
        must(ext.GetVertexDeclaration(&active));active->Release();
        IDirect3DPixelShader9* color=nullptr;
        must(ext.GetPixelShader(&color));color->Release();
        IDirect3DVertexShader9* pos=nullptr;
        must(ext.GetVertexShader(&pos));pos->Release();
        must(ext.Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));
        must(d.BeginScene());
        struct Coverage {
            ExtensionDevice& device;
            bool capture;
            NorthlightEffectCoverage::Capture masks;
            unsigned passes=0;
            static HRESULT paint(void* context,unsigned,IDirect3DPixelShader9* pixel,void* data,HRESULT(*submit)(void*)){
                auto& state=*static_cast<Coverage*>(context);
                ++state.passes;
                must(state.device.SetRenderState(D3DRS_STENCILREF,42));
                const auto draw=[&]{return submit(data);};
                const HRESULT hr=state.capture||pixel?
                    state.masks.draw(&state.device,state.capture?NorthlightEffectCoverage::Write::Alpha:
                        NorthlightEffectCoverage::Write::None,draw,false,pixel):draw();
                must(state.device.SetRenderState(D3DRS_STENCILREF,0));
                return hr;
            }
        } coverage{ext,mode==Routed||(mode==SerialAtlas&&(firstTile==3||secondTile==3||sameTexture))};
        if(mode>=Joined){
            assert(pair.arm({a,next,vb.p,mode==Overlap?0u:secondOffset,half,sameTexture?~0u:stage,9,rowCount,
                             useAtlas&&!sameTexture?nextTexture:nullptr,differentPayload?lookaheadRows[0]:nextRows[0]}));
            ext.setIndexedPair(&pair,[](void* data){must(static_cast<NorthlightGameParticlePair*>(data)->barrier());},
                [](void* data,UINT reg,const float* values,UINT count){return static_cast<NorthlightGameParticlePair*>(data)->allowsConstants(reg,values,count);},
                [](void* data,DWORD stage,IDirect3DBaseTexture9* texture){return static_cast<NorthlightGameParticlePair*>(data)->allowsTexture(stage,texture);});
            must(ext.SetRenderState(D3DRS_STENCILREF,8));
        }
        const auto draw=[&](Draw args){
            auto dip=[&]{return ext.DrawIndexedPrimitive(D3DPT_TRIANGLELIST,args.base,0,args.vertices,0,args.primitives);};
            const auto serial=[&]{return useAtlas?Coverage::paint(&coverage,0,nullptr,&dip,[](void* context){return (*static_cast<decltype(dip)*>(context))();}):dip();};
            const NorthlightGameParticlePair::Coverage plane=mode==Atlas||mode==Routed?
                NorthlightGameParticlePair::Coverage{&coverage,&Coverage::paint,0}:NorthlightGameParticlePair::Coverage{};
            must(mode>=Joined?pair.submit(args,true,serial,plane):serial());
        };
        d.calls=d.pixelBinds=0;pair.saved=0;
        draw(mode==Reversed?b:a);
        if(mode!=Reversed)upload(secondOffset,quads+4,half,D3DLOCK_NOOVERWRITE);
        if(useAtlas)retain(secondOffset,quads+4);
        must(ext.SetVertexShaderConstantF(9,nextRows[0],rowCount));
        if(!sameTexture)must(ext.SetTexture(stage,nextTexture));
        if(mode==Barrier||drain)must(ext.SetRenderState(D3DRS_STENCILREF,9));
        draw(mode==Reversed?a:next);
        if(mode>=Joined){must(pair.flush());ext.setIndexedPair(nullptr,nullptr,nullptr);}
        const bool merged=mode==Joined||(mode==Routed&&!drain)||(mode==Atlas&&atlasWarm);
        if(d.calls!=(merged?1u:2u)||pair.saved!=unsigned(merged))
            std::fprintf(stderr,"particle mode=%u calls=%u saves=%u routed=%u proof=%u texture=%u vertex=%u\n",
                         unsigned(mode),d.calls,pair.saved,pair.routedSaved,pair.proofMisses,pair.textureBarriers,pair.atlasVertexMisses);
        assert(d.calls==(merged?1u:2u)&&pair.saved==unsigned(merged));
        if(mode==Routed&&!drain)assert(d.pixelBinds==2);
        if(varying){
            float rows[35][4],bank[128];
            must(d.GetVertexShaderConstantF(9,rows[0],35));must(d.GetVertexShaderConstantF(224,bank,32));
            assert(!std::memcmp(rows,nextRows,sizeof rows)&&!std::memcmp(bank,spare,sizeof bank));
            assert(pair.constantSaved==unsigned(merged));
        }
        if(useAtlas)assert(coverage.passes==(merged?1u:2u));
        must(d.EndScene());
        std::vector<DWORD> result;
        const auto pixels=[&](IDirect3DSurface9* surface){
            must(d.GetRenderTargetData(surface,readback.p));
            D3DLOCKED_RECT locked{};must(readback.p->LockRect(&locked,nullptr,D3DLOCK_READONLY));
            const auto begin=result.size();
            result.resize(begin+size*size);
            for(UINT y=0;y<size;++y)std::memcpy(result.data()+begin+y*size,static_cast<char*>(locked.pBits)+y*locked.Pitch,size*4);
            must(readback.p->UnlockRect());
        };
        pixels(target.p);
        if(coverage.capture){
            auto* mask=coverage.masks.aoTexture(&ext,target.p);
            assert(mask&&coverage.masks.draws==(merged?1u:2u));
            Ref<IDirect3DSurface9> surface;
            must(mask->GetSurfaceLevel(0,surface.out()));
            pixels(surface.p);
        }
        return result;
    };
    const auto expected=render(Serial);
    assert(expected!=render(Reversed));
    for(auto mode:{Joined,Overlap,BadIndices,Barrier})assert(expected==render(mode));
    const auto texturedExpected=render(SerialSampled);
    assert(texturedExpected!=expected&&texturedExpected==render(Sampled));
    const auto atlasExpected=render(SerialAtlas);
    assert(atlasExpected!=expected);
    for(unsigned i=0;i<3;++i)assert(atlasExpected==render(Atlas,true));
    assert(pair.atlasSaved==1&&pair.atlasBuilt()==1);
    const auto sharedExpected=render(SerialAtlas,false,1,2,true);
    assert(sharedExpected!=atlasExpected);
    for(unsigned i=0;i<3;++i)assert(sharedExpected==render(Atlas,true,1,2,true));
    assert(atlasExpected==render(Atlas,true));
    const auto reverseExpected=render(SerialAtlas,false,2,0,true);
    assert(reverseExpected==render(Atlas,true,2,0,true)&&pair.atlasBuilt()==0);
    for(const auto a:{0u,3u}){
        const auto b=3-a;
        const auto serial=render(SerialAtlas,false,a,b,true);
        assert(serial==render(Routed,false,a,b,true)&&pair.routedSaved==1);
        Ref<IDirect3DBaseTexture9> restored;
        DWORD min=0;
        must(d.GetTexture(1,restored.out()));
        must(d.GetSamplerState(1,D3DSAMP_MINFILTER,&min));
        assert(restored.p==initialTexture.p&&min==D3DTEXF_POINT);
    }
    const auto varying=render(SerialAtlas,false,0,3,true,true);
    assert(varying!=render(SerialAtlas,false,0,3,true));
    assert(varying==render(Routed,false,0,3,true,true));
    assert(varying==render(Routed,false,0,3,true,true,true));
    assert(varying==render(Routed,false,0,3,true,true,false,false,true));
    assert(varying==render(Routed,false,0,3,true,true,false,false,false,true));
    const auto sameTexture=render(SerialAtlas,false,0,0,false,true,false,true);
    assert(sameTexture==render(Routed,false,0,0,false,true,false,true));
    NorthlightParticleUploads::atlasUploads().forget(vb.p);
    NorthlightParticleUploads::cache().forget(vb.p);
    NorthlightReplayCopies::enabled.store(copies);
    std::puts("ORDERED_PARTICLE_PAIR_PASS color order exact, state barriers preserved, shared atlas 2->1 with disjoint vertex ranges");
    std::puts("PARTICLE_TEXTURE_PAIR_PASS BC3 and RGBA8 2->1, exact scene and alpha coverage pixels, sampler restore");
    std::puts("PARTICLE_PAIR_CONSTANTS_PASS 15 changed VS rows, shared texture, exact pixels, fallback order and register restoration");
}
