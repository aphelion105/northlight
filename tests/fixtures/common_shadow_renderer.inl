// Included ONLY by the generated experimental WorldRenderer test overlay.
// The production renderer and its DLL contain none of this route or switch.
struct CommonExperiment {
    bool enabled=false,timing=false;
    unsigned sample=0,worldDraws=0,boundaryDraws=0,staticRenders=0,staticReuses=0;
    static constexpr unsigned Samples=32;
    IDirect3DQuery9* stamps[Samples][4]{};
    std::chrono::steady_clock::time_point cpu[Samples][4];
    IDirect3DPixelShader9 *filter=nullptr,*replay=nullptr,*blocksPS=nullptr,*tilesPS=nullptr,*seedPS=nullptr;
    IDirect3DTexture9 *blocks[2]{},*tiles[2]{};
    IDirect3DSurface9 *blockSurface[2]{},*tileSurface[2]{};
    IDirect3DTexture9* staticDepth[2]{};IDirect3DSurface9* staticSurface[2]{};
    float matrix[2][16]{};uint64_t mesh[2]{},signature[2]{};bool valid[2]{};
    ~CommonExperiment(){
        for(auto& frame:stamps)for(auto& q:frame)drop(q);
        drop(filter);drop(replay);drop(blocksPS);drop(tilesPS);drop(seedPS);
        for(unsigned s=0;s<2;++s){drop(blockSurface[s]);drop(tileSurface[s]);drop(blocks[s]);drop(tiles[s]);drop(staticSurface[s]);drop(staticDepth[s]);}
    }
    bool init(IDirect3DDevice9* d){
        if(filter)return true;
        if(FAILED(d->CreatePixelShader(kCommonDirectionalShader,&filter))||
           FAILED(d->CreatePixelShader(kCommonReplayPSShader,&replay))||
           FAILED(d->CreatePixelShader(kCommonBlocksShader,&blocksPS))||
           FAILED(d->CreatePixelShader(kCommonTilesShader,&tilesPS))||
           FAILED(d->CreatePixelShader(kCommonSeedDepthShader,&seedPS)))return false;
        for(unsigned s=0;s<2;++s){
            if(FAILED(d->CreateTexture(256,256,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&blocks[s],nullptr))||
               FAILED(blocks[s]->GetSurfaceLevel(0,&blockSurface[s]))||
               FAILED(d->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&tiles[s],nullptr))||
               FAILED(tiles[s]->GetSurfaceLevel(0,&tileSurface[s]))||
               FAILED(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&staticDepth[s],nullptr))||
               FAILED(staticDepth[s]->GetSurfaceLevel(0,&staticSurface[s])))return false;
        }
        for(auto& frame:stamps)for(auto& q:frame)if(FAILED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&q)))return false;
        return true;
    }
    void stamp(unsigned stage){
        if(!timing)return;
        assert(sample<Samples&&stage<4);
        cpu[sample][stage]=std::chrono::steady_clock::now();
        assert(SUCCEEDED(stamps[sample][stage]->Issue(D3DISSUE_END)));
    }
} commonExperiment;

// Fill the existing fine target with world casters BEFORE the selected actors.
// Statics may come from their exact cache. There is no actor-only map or
// post-raster depth union; cache construction and seeding are both charged.
bool commonWorldCasters(const float* matrix,unsigned source,size_t& replayConstantBytes,size_t& replayConstantCalls){
    auto& experiment=commonExperiment;
    const uint64_t signature=staticSignature(matrix);
    // No motion tolerance, frozen light, or stale live terrain. A different
    // continuous projection invalidates the cache even within a lattice cell.
    const bool reusable=experiment.valid[source]&&experiment.mesh[source]==meshGeneration&&
        experiment.signature[source]==signature&&!std::memcmp(matrix,experiment.matrix[source],64)&&
        !liveDirectionalIndexCount&&liveTerrainChunks.empty();
    if(!reusable){
    experiment.valid[source]=false;
    d->SetTexture(0,nullptr);
    if(FAILED(d->SetRenderTarget(0,experiment.staticSurface[source]))||
       FAILED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0)))return false;
    d->SetVertexDeclaration(shadowDecl);d->SetVertexShader(cachedShadowVS);
    d->SetVertexShaderConstantF(0,matrix,4);d->SetPixelShader(cachedShadowPS);
    d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
    d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    d->SetStreamSourceFreq(0,1);d->SetStreamSourceFreq(1,1);
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,state,D3DTEXF_LINEAR);
    for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})d->SetSamplerState(0,state,D3DTADDRESS_WRAP);
    UINT page=UINT_MAX;
    for(const auto& batch:batches){
        if(batch.pointOnly||(batch.terrain&&liveTerrainChunks.count({batch.chunkX,batch.chunkY})))continue;
        if(NorthlightShadowBounds::directionalClipRejectAffine(batch.boundsLow,batch.boundsHigh,matrix))continue;
        const float material[]={1,1,1,uploadedAlphaCutoffs[batch.material]};
        d->SetPixelShaderConstantF(0,material,1);d->SetTexture(0,materials[batch.material]);
        if(!bindMeshPage(batch,page)||FAILED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count)))return false;
        ++experiment.worldDraws;
    }
    if(liveDirectionalIndexCount){
        const float opaque[]={1,1,1,-1};d->SetPixelShaderConstantF(0,opaque,1);d->SetTexture(0,nullptr);
        d->SetVertexDeclaration(liveTerrainDecl);d->SetStreamSource(0,liveTerrainGPU.vertices(),0,sizeof(StaticShadow::Vertex));d->SetIndices(liveIndicesGPU);
        if(FAILED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,liveTerrainGPU.vertexCapacity(),UINT(liveIndexBase+liveTerrainIndexCount),UINT(liveDirectionalIndexCount/3))))return false;
        ++experiment.worldDraws;
    }
    // Exact current fine projection: do not reuse a coarser plan that can omit
    // upstream casters. Its selection/plan cost belongs to the new total too.
    if(!drawStaticCasters(matrix))return false;
    experiment.worldDraws+=unsigned(staticCasters.stats().drawCalls);
    std::memcpy(experiment.matrix[source],matrix,64);experiment.mesh[source]=meshGeneration;experiment.signature[source]=signature;
    experiment.valid[source]=true;++experiment.staticRenders;
    }else ++experiment.staticReuses;
    // Seed both colour depth and the hardware Z buffer; subsequent animated
    // geometry writes directly into this same final near map. No depth union.
    if(FAILED(d->SetRenderTarget(0,actorShadowSurface[source])))return false;
    d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);
    d->SetPixelShader(experiment.seedPS);d->SetTexture(0,experiment.staticDepth[source]);
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,state,D3DTEXF_POINT);
    for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})d->SetSamplerState(0,state,D3DTADDRESS_CLAMP);
    d->SetStreamSourceFreq(0,1);d->SetStreamSourceFreq(1,1);
    if(FAILED(quad(1024,1024)))return false;
    d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,state,D3DTEXF_LINEAR);
    for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})d->SetSamplerState(0,state,D3DTADDRESS_WRAP);
    d->SetPixelShader(experiment.replay);
    float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrix,rows);
    NorthlightReplayPoses::Pass<BOOL> poses;NorthlightReplayDrawState::Cache bindings(d);
    NorthlightRenderThreadProbe::Off split;
    for(size_t index=0;index<replays.size();++index){
        if(std::binary_search(actorReplays.begin(),actorReplays.end(),uint32_t(index)))continue;
        const auto& p=replays[index];
        if(p->pointBounds.valid&&NorthlightShadowBounds::clipRejectAffine(vec(p->pointBounds.low),vec(p->pointBounds.high),matrix))continue;
        if(!submitReplay(p.get(),bindings,poses,rows,matrix,split,replayConstantBytes,replayConstantCalls,[&](HRESULT hr,const char* reason){return check(hr,reason);}))return false;
        ++experiment.boundaryDraws;
    }
    return true;
}

bool commonPrepare(const bool* active){
    auto& experiment=commonExperiment;
    for(unsigned source=0;source<2;++source){
        if(!active[source])continue;
        d->SetTexture(13,nullptr);d->SetTexture(10,nullptr);
        if(FAILED(d->SetRenderTarget(0,experiment.blockSurface[source])))return false;
        d->SetTexture(12,actorShadow[source]);d->SetPixelShader(experiment.blocksPS);
        if(FAILED(quad(256,256)))return false;
        if(FAILED(d->SetRenderTarget(0,experiment.tileSurface[source])))return false;
        d->SetTexture(13,experiment.blocks[source]);d->SetPixelShader(experiment.tilesPS);
        if(FAILED(quad(64,64)))return false;
    }
    d->SetTexture(13,nullptr);return true;
}
