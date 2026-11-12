#pragma once
// Optional all-readback digest and actual creation-failure composition oracle.
static void nativeLocalRecord(const std::vector<float>& image){
    if(const char* path=std::getenv("NORTHLIGHT_NATIVE_IMAGE_DIGEST")){uint64_t hash=14695981039346656037ull;const auto* bytes=reinterpret_cast<const unsigned char*>(image.data());
        for(size_t i=0;i<image.size()*sizeof(float);++i)hash=(hash^bytes[i])*1099511628211ull;
        FILE* f=std::fopen(path,"ab");assert(f);std::fprintf(f,"%zu %016llx\n",image.size(),(unsigned long long)hash);std::fclose(f);
    }
}
static std::vector<uint32_t> nativeLocalWords(const DWORD* code){size_t count=1;while(code[count]!=65535){const unsigned op=code[count]&65535,n=op==65534?(code[count]>>16)&32767:(code[count]>>24)&15;count+=n+1;assert(count<65536);}return {reinterpret_cast<const uint32_t*>(code),reinterpret_cast<const uint32_t*>(code)+count+1};}
#ifndef NORTHLIGHT_NATIVE_DUPLICATE_BASELINE
static void nativeLocalCreationFallback(IDirect3DDevice9* d,NorthlightNativeLocalDraw light,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,bool two,const NorthlightNativePOM* pom=nullptr,const NorthlightNativeDisplacement* displacement=nullptr){
    struct Program {std::vector<uint32_t> duplicate,shared,vertex;NorthlightNativeLocalPatch::PixelInfo info;unsigned pomControl=0,displacementControl=0;};std::vector<Program> programs;
    auto* sourceVS=pom?pom->originalVS:displacement?displacement->originalVS:vs;auto* sourcePS=pom?pom->originalPS:ps;
    UINT vb=0,pb=0;assert(SUCCEEDED(sourceVS->GetFunction(nullptr,&vb))&&SUCCEEDED(sourcePS->GetFunction(nullptr,&pb)));std::vector<uint32_t> v(vb/4),p(pb/4);assert(SUCCEEDED(sourceVS->GetFunction(v.data(),&vb))&&SUCCEEDED(sourcePS->GetFunction(p.data(),&pb)));
    for(unsigned count=1;count<=(two?2u:1u);++count){Program program;NorthlightNativeLocalPatch::VertexInfo vi;const unsigned slots[]={light.slot,1};const auto* helper=reinterpret_cast<const uint32_t*>(count==1?kNativeLocalLighting1Shader:kNativeLocalLighting2Shader);const size_t helperWords=count==1?sizeof(kNativeLocalLighting1Shader)/4:sizeof(kNativeLocalLighting2Shader)/4;
        if(!NorthlightNativeLocalPatch::vertex(v.data(),v.size(),slots,count,program.vertex,vi,pom?0xc2u:0))break;
        if(pom){std::vector<uint32_t> material,forwarded;if(NorthlightWaterShaderPatch::patch(program.vertex.data(),program.vertex.size(),forwarded))break;
            if(pom->shared){if(!pom->sharedVertex(reinterpret_cast<const DWORD*>(forwarded.data()),forwarded.size(),material))break;forwarded.swap(material);}program.vertex.swap(forwarded);}
        auto build=[&](const uint32_t* selected,size_t words,bool share,std::vector<uint32_t>& code,NorthlightNativeLocalPatch::PixelInfo& info,unsigned& control){
            if(!NorthlightNativeLocalPatch::pixel(p.data(),p.size(),vi,selected,words,code,info,share))return false;
            if(pom){std::vector<uint32_t> material;if(!NorthlightParallaxPatch::pixel(code.data(),code.size(),pom->dual,material,&control,pom->shared,nullptr,512))return false;unsigned cost=0;std::vector<NorthlightHdrUi::Instruction> instructions;if(!NorthlightHdrUi::parse(material.data(),material.size(),instructions,cost)||cost+3*count>512)return false;code.swap(material);}return true;
        };
        NorthlightNativeLocalPatch::PixelInfo sharedInfo;unsigned ignored=0;
        bool shared=build(helper,helperWords,true,program.shared,sharedInfo,ignored),duplicate=build(helper,helperWords,false,program.duplicate,program.info,program.pomControl);
        if(!shared&&!duplicate){const auto* legacy=reinterpret_cast<const uint32_t*>(count==1?kNativeLocalLightingLegacy1Shader:kNativeLocalLightingLegacy2Shader);const size_t words=count==1?sizeof(kNativeLocalLightingLegacy1Shader)/4:sizeof(kNativeLocalLightingLegacy2Shader)/4;
            shared=build(legacy,words,true,program.shared,sharedInfo,ignored);duplicate=build(legacy,words,false,program.duplicate,program.info,program.pomControl);}
        if(!duplicate)break;if(!shared)program.shared=program.duplicate;
        if(displacement){std::vector<uint32_t> material;NorthlightDisplacementPatch::Binding binding;assert(NorthlightDisplacementPatch::patch(program.vertex.data(),program.vertex.size(),material,binding,true,displacement->gpu));program.vertex.swap(material);program.displacementControl=binding.constant;}
        programs.push_back(std::move(program));
    }
    assert(!programs.empty());
    struct Device:ForwardIDirect3DDevice9 {const std::vector<Program>& programs;bool rejectBoth;unsigned calls=0,rejected=0;Device(IDirect3DDevice9*d,const std::vector<Program>& p,bool all):ForwardIDirect3DDevice9(d),programs(p),rejectBoth(all){}
        HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{++calls;const auto words=nativeLocalWords(code);for(const auto& p:programs)if(words==p.duplicate){if(rejectBoth){++rejected;*out=nullptr;return E_FAIL;}return real->CreatePixelShader(code,out);}for(const auto& p:programs)if(words==p.shared){++rejected;*out=nullptr;return E_FAIL;}assert(false);return E_FAIL;}
    };
    float savedPS[224][4],savedVS[256][4];assert(SUCCEEDED(d->GetPixelShaderConstantF(0,savedPS[0],224))&&SUCCEEDED(d->GetVertexShaderConstantF(0,savedVS[0],256)));
    const D3DSAMPLERSTATETYPE keys[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};DWORD sampler[16][7];IDirect3DBaseTexture9* textures[16]{};
    for(unsigned i=0;i<16;++i){assert(SUCCEEDED(d->GetTexture(i,&textures[i])));for(unsigned k=0;k<7;++k)assert(SUCCEEDED(d->GetSamplerState(i,keys[k],&sampler[i][k])));}
    auto state=[&]{float pixel[224][4],vertex[256][4];assert(SUCCEEDED(d->GetPixelShaderConstantF(0,pixel[0],224))&&SUCCEEDED(d->GetVertexShaderConstantF(0,vertex[0],256)));assert(!std::memcmp(pixel,savedPS,sizeof pixel)&&!std::memcmp(vertex,savedVS,sizeof vertex));IDirect3DPixelShader9* p=nullptr;IDirect3DVertexShader9* v=nullptr;d->GetPixelShader(&p);d->GetVertexShader(&v);assert(p==ps&&v==vs);if(p)p->Release();if(v)v->Release();for(unsigned i=0;i<16;++i){IDirect3DBaseTexture9*t=nullptr;d->GetTexture(i,&t);assert(t==textures[i]);if(t)t->Release();for(unsigned k=0;k<7;++k){DWORD value;d->GetSamplerState(i,keys[k],&value);assert(value==sampler[i][k]);}}};
    for(bool both:{false,true}){Device device(d,programs,both);NorthlightNativeLocalLighting renderer(&device);auto second=light;second.slot=1;second.gain=.375f;second.position[0]+=2;unsigned submitted=0;
        for(unsigned step=0;step<3;++step){light.rendererFrame=1000+step;const unsigned selected=programs.size()==2&&step?1:0;
            auto submit=[&]{++submitted;IDirect3DPixelShader9* bound=nullptr;d->GetPixelShader(&bound);assert(bound);UINT size=0;bound->GetFunction(nullptr,&size);std::vector<uint32_t> code(size/4);bound->GetFunction(code.data(),&size);if(both)assert(bound==ps);else{
                    assert(code==programs[selected].duplicate);float c[10][4];d->GetPixelShaderConstantF(programs[selected].info.control+2,c[0],selected?10:8);float rotation[16];std::memcpy(rotation,light.inverseView,64);rotation[12]=rotation[13]=rotation[14]=0;assert(c[0][3]==.025f&&!std::memcmp(c[1],rotation,64)&&c[7][1]==light.range&&c[7][3]==light.gain);for(unsigned k=0;k<3;++k){assert(c[5][k]==light.position[k]-light.inverseView[12+k]);if(selected)assert(c[8][k]==second.position[k]-light.inverseView[12+k]);}if(selected)assert(c[9][3]==second.gain);
                    if(pom){float actual[12];d->GetPixelShaderConstantF(programs[selected].pomControl,actual,pom->count);assert(!std::memcmp(actual,savedPS[pom->sourceControl],pom->count*16));}
                    if(displacement){float actual[8];d->GetVertexShaderConstantF(programs[selected].displacementControl,actual,displacement->count);assert(!std::memcmp(actual,savedVS[displacement->control],displacement->count*16));}
                }bound->Release();return S_OK;};
            const unsigned before=device.calls;assert(SUCCEEDED(renderer.draw(light,vs,ps,submit,programs.size()==2?&second:nullptr,pom,displacement)));assert(submitted==step+1);state();if(step==2)assert(device.calls==before);
        }
        unsigned expected=0;for(const auto& p:programs)expected+=p.shared==p.duplicate?1:2;assert(device.calls==expected);assert(both?renderer.draws==0:renderer.draws==3);if(!both&&programs.size()==2)assert(renderer.multiDraws==2);
        std::printf("NATIVE_SHARING_CREATE_PASS rejectBoth=%u programs=%zu createCalls=%u rejected=%u submits=%u cached=1 payload=exact-duplicate constants=exact state=exact pom=%u displacement=%u\n",unsigned(both),programs.size(),device.calls,device.rejected,submitted,unsigned(pom!=nullptr),unsigned(displacement!=nullptr));
    }
    for(auto*t:textures)if(t)t->Release();
}

#endif
