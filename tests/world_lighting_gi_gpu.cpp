// Compare the production merged pass with independent lighting + GI.
#include "lighting_gpu_support.h"
int main(int argc,char** argv){
 assert(argc==5);const std::string root=argv[2],proto=argv[3];const bool timing=std::atoi(argv[4]);const unsigned W=timing?1920:97,H=timing?1080:65;
 auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto* api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SEPARATEALPHABLENDENABLE})ok(d->SetRenderState(s,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
 std::vector<IDirect3DTexture9*> owned;
 for(unsigned i=0;i<16;++i){auto t=pixels(d,1,1,[&](unsigned,unsigned){return std::array<float,4>{1,1,1,1};});owned.push_back(t);ok(d->SetTexture(i,t));for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(i,s,D3DTEXF_POINT));ok(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto s:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(i,s,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));}
 auto bind=[&](unsigned slot,unsigned w,unsigned h,auto pixel){auto t=pixels(d,w,h,pixel);owned.push_back(t);ok(d->SetTexture(slot,t));};
 bind(1,W,H,[&](unsigned x,unsigned y){float z=4+8.f*x/W;float depth=1000.f/999.8f*(1-.2f/z);if(!timing&&y>H-7)depth=1;return std::array<float,4>{depth,0,0,0};});
 bind(14,W,H,[&](unsigned x,unsigned){return std::array<float,4>{0,0,-1,x%3?.8f:0.f};});
 bind(11,1,1,[&](unsigned,unsigned){return std::array<float,4>{6.5f,1,0,0};});
 bind(10,16,4,[&](unsigned x,unsigned y){const int a=int(x%4),b=int(y),c=int(x/4);return std::array<float,4>{float(a<2?a:a-4),float(b<2?b:b-4),float(c<2?c:c-4),1};});
 bind(7,16,24,[&](unsigned,unsigned){return std::array<float,4>{100,10000,1,5.f};});
 for(unsigned i=4;i<7;++i)bind(i,16,4,[&](unsigned x,unsigned){return std::array<float,4>{.3f+float(x%3)*.5f,0,0,0};});
 bind(13,72,72,[](unsigned,unsigned){return std::array<float,4>{0,0,0,1};});
 bind(8,48,4,[&](unsigned,unsigned){return std::array<float,4>{.2f,0,0,0};});
 float c[128][4]{};c[0][0]=.5f/W;c[0][1]=.5f/H;c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=1;c[1][2]=1;c[2][0]=1;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;for(unsigned i=0;i<4;++i)c[3+i][i]=1;for(unsigned i=0;i<4;++i)c[7+i][i]=c[11+i][i]=.01f;c[16][2]=-1;c[18][0]=.2f;c[18][1]=.3f;c[18][2]=.4f;c[19][3]=16;c[20][0]=4;c[20][1]=.7f;c[20][2]=.8f;c[20][3]=1;c[24][3]=5;c[27][1]=1;c[28][2]=-1;c[29][0]=.8f;c[29][1]=.6f;c[29][2]=.4f;c[33][0]=W*2;c[33][1]=H*2;c[33][2]=W;c[33][3]=H;c[73][2]=1;
 c[82][0]=8;c[82][1]=c[82][2]=1.f/72;c[82][3]=1.f/8;
 IDirect3DSurface9 *light=nullptr,*base=nullptr,*prelit=nullptr,*back=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&light,nullptr));ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&base,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&back,nullptr));
 ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&prelit,nullptr));
 auto gi=shader(d,root+"/WorldGI.bin");IDirect3DQuery9 *a=nullptr,*b=nullptr,*hz=nullptr;for(auto p:{&a,&b})ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,p));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&hz));const auto get=[&](IDirect3DQuery9* q){UINT64 v=0;HRESULT hr;while((hr=q->GetData(&v,sizeof v,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);return v;};
 auto quad=[&]{const Vertex v[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof v[0]));};
 auto image=[&](IDirect3DSurface9* s){ok(d->GetRenderTargetData(s,back));D3DLOCKED_RECT lock{};ok(back->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<uint16_t> out(size_t(W)*H*4);for(unsigned y=0;y<H;++y)std::memcpy(out.data()+size_t(y)*W*4,(char*)lock.pBits+y*lock.Pitch,W*8);ok(back->UnlockRect());return out;};
 unsigned cases=0,prelitCases=0,darkPixels=0;float maximum=0;std::array<std::vector<uint16_t>,16> noMix,halfMix,ordinary;
 {const unsigned soft=0;const std::string name="WorldLightingUnshadowed";auto old=shader(d,root+"/"+name+".bin"),fused=shader(d,proto+"/"+name+"GI.bin");
 for(unsigned blend=0;blend<(timing?1u:3u);++blend)for(unsigned test=0;test<(timing?2u:16u);++test){c[66][0]=blend*.5f;const unsigned sources=1+test%2;c[20][1]=(test%4)*2.f/3;c[66][1]=float(int(test%4)-1);c[18][3]=test%3*.2f;c[24][2]=test%5==2?2.f:0.f;c[20][3]=test%6!=3;c[24][3]=timing?6.f:5.f+float(test%4)*.15f;c[30][0]=!timing&&test%7==4?1.f:0.f;ok(d->SetPixelShaderConstantF(0,c[0],128));std::vector<uint16_t> reference,baseline,prelitReference;
 for(unsigned pass=0;pass<(timing?12u:2u);++pass){const bool merge=timing?(pass%4==1||pass%4==2):pass!=0;
 ok(hz->Issue(D3DISSUE_END));ok(a->Issue(D3DISSUE_END));const auto start=std::chrono::steady_clock::now();ok(d->BeginScene());ok(d->SetRenderTarget(0,light));D3DVIEWPORT9 vp{0,0,W,H,0,1};ok(d->SetViewport(&vp));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
 ok(d->SetRenderTarget(2,prelit));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE2,15));
 for(unsigned source=0;source<sources;++source){float color[4]={source?.1f:.8f,.6f,.4f,test%2?0.f:1.f};ok(d->SetPixelShaderConstantF(17,color,1));ok(d->SetRenderTarget(1,source?nullptr:base));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,source!=0));ok(d->SetPixelShader(merge&&source+1==sources?fused:old));quad();}
 ok(d->SetRenderTarget(1,nullptr));ok(d->SetRenderTarget(2,nullptr));if(!merge){ok(d->SetRenderTarget(1,prelit));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));ok(d->SetPixelShader(gi));quad();ok(d->SetRenderTarget(1,nullptr));}ok(d->EndScene());const auto end=std::chrono::steady_clock::now();ok(b->Issue(D3DISSUE_END));const double gpu=double(get(b)-get(a))*1000/get(hz);
 if(timing){printf("FUSED_GI_TIMING soft=%u sources=%u pass=%u merged=%u cpuMs=%.6f gpuMs=%.6f\n",soft,sources,pass,unsigned(merge),std::chrono::duration<double,std::milli>(end-start).count(),gpu);fflush(stdout);}else{auto out=image(light),baseOut=image(base),prelitOut=image(prelit);if(!pass){reference=std::move(out);baseline=std::move(baseOut);prelitReference=std::move(prelitOut);}else{assert(baseOut==baseline);for(size_t i=0;i<out.size();++i){if(i%4==3){assert(out[i]==reference[i]);continue;}const float a=half(reference[i]),b=half(out[i]),error=std::fabs(a-b);maximum=std::max(maximum,error);assert(std::isfinite(a)&&std::isfinite(b)&&error<=.002f+.002f*std::max(std::fabs(a),std::fabs(b)));}
 for(size_t i=0;i<prelitOut.size();++i){const float v=half(prelitOut[i]),ref=half(prelitReference[i]);assert(std::isfinite(v)&&std::fabs(v-ref)<=.002f+.002f*std::max(std::fabs(v),std::fabs(ref)));
  if(test%2&&(!blend||!c[20][3]))assert(v==0); // No roof visibility proof: no sunlight leak into authored interiors.
  if(i%4==3){assert(v>=0&&v<=1);if(!blend||!c[20][3])assert(v==0);darkPixels+=v>.01f;}
 }
 if(!blend){noMix[test]=prelitOut;ordinary[test]=out;}else{assert(ordinary[test]==out);if(blend==1)halfMix[test]=prelitOut;else for(size_t i=0;i<prelitOut.size();++i){const float midpoint=(half(noMix[test][i])+half(prelitOut[i]))*.5f;assert(std::fabs(half(halfMix[test][i])-midpoint)<.003f);}}
 ++prelitCases;++cases;}}
 }
 }old->Release();fused->Release();}
 if(!timing){
  // Exercise the final consumer, including sky and self-lit receivers. The
  // aux texture contains independent daylight plus the probe contribution.
  std::memset(c,0,sizeof c);c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.2f;c[0][3]=1000;
  c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;c[25][1]=c[25][2]=1;
  c[33][0]=c[33][2]=W;c[33][1]=c[33][3]=H;c[81][0]=1;c[115][0]=c[115][3]=1;
  c[18][0]=.4f;c[18][1]=.3f;c[18][2]=.2f;
  auto solid=[&](unsigned slot,std::array<float,4> v){bind(slot,1,1,[&](unsigned,unsigned){return v;});};
  solid(0,{.4f,.3f,.2f,.7f});solid(7,{0,0,0,0});solid(8,{0,0,0,0});solid(9,{0,0,0,1});
  solid(10,{0,0,0,1});solid(11,{0,0,0,0});solid(12,{.4f,.3f,.2f,1});solid(13,{0,0,0,0});
  unsigned consumerCases=0;
  for(bool hdr:{false,true})for(const char* receiver:{"WorldPrelitComposite","WorldComposite","WorldUnlitComposite"}){
   const bool prelitReceiver=std::strstr(receiver,"Prelit"),unlitReceiver=std::strstr(receiver,"Unlit");
   auto ps=shader(d,proto+"/"+receiver+(hdr?"Hdr":"")+".bin");
   solid(2,hdr?std::array<float,4>{.132868f,.073239f,.033105f,1}:std::array<float,4>{0,0,0,0});
   for(bool positive:{false,true}){
    std::vector<uint16_t> results[2][3];
    for(unsigned lamp=0;lamp<2;++lamp)for(unsigned mix=0;mix<3;++mix){
     solid(3,{lamp*.3f,lamp*.1f,lamp*.04f,0});
     const float amount=mix*.5f;
     solid(14,{.03f+(positive?.12f*amount:0),.04f+(positive?.08f*amount:0),.05f+(positive?.04f*amount:0),positive?0:.75f*amount});
     ok(d->SetPixelShaderConstantF(0,c[0],128));ok(d->SetRenderTarget(0,light));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
     ok(d->SetPixelShader(ps));ok(d->BeginScene());quad();ok(d->EndScene());results[lamp][mix]=image(light);++consumerCases;
    }
    for(size_t i=0;i<results[0][0].size();++i){
     const unsigned y=unsigned(i/4/W);const bool protectedPixel=i%4==3||y>H-7||!prelitReceiver;
     for(unsigned lamp=0;lamp<2;++lamp){const auto& r=results[lamp];const float a=half(r[0][i]),b=half(r[1][i]),e=half(r[2][i]);
      assert(std::isfinite(a)&&std::isfinite(b)&&std::isfinite(e));
      if(protectedPixel)assert(r[0][i]==r[1][i]&&r[1][i]==r[2][i]);
      else if(!lamp||hdr){assert(std::fabs(b-(a+e)*.5f)<.001f);assert(positive?e>a:e<a);}
     }
     // Fresh direct lamp radiance is added after GI; changing this blend
     // cannot dim that contribution in the HDR radiance target.
     if(hdr)for(unsigned mix=1;mix<3;++mix){const float delta=half(results[1][mix][i])-half(results[0][mix][i]);
      const float reference=half(results[1][0][i])-half(results[0][0][i]);assert(std::fabs(delta-reference)<.001f);}
    }
   }
   ps->Release();
  }
  printf("PRELIT_COMPOSITE_PASS cases=%u midpoint=1 skyUnlitOrdinaryExact=1 hdrDirectLampIndependent=1\n",consumerCases);
  // A dark probe replaces all ambient at full GI strength. Neither the final
  // ordinary receiver nor the shadow-removal filter may restore 45-55% of it.
  // This is an energy oracle, independent of the shader's removal expression.
  unsigned darkRoomCases=0;
  solid(12,{.4f,.3f,.2f,1});solid(14,{0,0,0,1});solid(15,{8,8,8,8});
  for(float strength:{0.f,.25f,.5f,.75f,1.f,2.f})for(float shadowStrength:{1.f,1.25f,2.f}){
   const float removal=std::min(strength,1.f);c[20][1]=strength;c[115][0]=shadowStrength;
   solid(8,{-.4f*removal,-.3f*removal,-.2f*removal,0});
   for(bool hdr:{false,true})for(bool lamp:{false,true}){
    auto ps=shader(d,proto+std::string("/WorldComposite")+(hdr?"Hdr":"")+".bin");
    solid(2,hdr?std::array<float,4>{.132868f,.073239f,.033105f,1}:std::array<float,4>{0,0,0,0});solid(3,{lamp?.02f:0.f,lamp?.01f:0.f,lamp?.005f:0.f,0});
    ok(d->SetPixelShaderConstantF(0,c[0],128));ok(d->SetPixelShader(ps));ok(d->BeginScene());quad();ok(d->EndScene());
    auto values=image(light);
    for(unsigned k=0;k<3;++k){
     const float native=hdr?(k==0?.132868f:k==1?.073239f:.033105f):(k==0?.4f:k==1?.3f:.2f);
     const float actual=half(values[(W*H/2)*4+k]);
     if(!lamp){if(std::fabs(actual-native*(1-removal))>=.001f)std::fprintf(stderr,"DARK_ROOM strength=%g shadow=%g hdr=%u channel=%u actual=%g expected=%g\n",strength,shadowStrength,unsigned(hdr),k,actual,native*(1-removal));assert(std::fabs(actual-native*(1-removal))<.001f);}
     else assert(actual>native*(1-removal)+.0001f);
    }
    ps->Release();++darkRoomCases;
   }
   // A constant field must survive both temporal variants and standalone
   // smoothing, including when shadow alpha enables the spatial kernel.
   c[20][2]=1;c[30][1]=c[30][2]=1;c[30][3]=30;c[57][0]=0;
   solid(3,{0,0,0,0});solid(14,{0,0,1,1});
   for(const char* name:{"RemovalSmooth","TemporalLightMerged","TemporalLightMergedZeroWeight"}){
    auto ps=shader(d,proto+"/"+name+".bin");ok(d->SetPixelShaderConstantF(0,c[0],128));ok(d->SetPixelShader(ps));
    ok(d->BeginScene());quad();ok(d->EndScene());auto values=image(light);
    for(unsigned k=0;k<3;++k){const float expected=-(k==0?.4f:k==1?.3f:.2f)*removal;
     assert(std::fabs(half(values[(W*H/2)*4+k])-expected)<.001f);}
    ps->Release();++darkRoomCases;
   }
  }
  // Half ambient, half directional. Turning GI up can remove the ambient
  // completely, while the existing directional art limit still owns its half.
  c[20][1]=1;c[115][0]=1;
  solid(8,{-.8f,-.6f,-.4f,0});solid(12,{.8f,.6f,.4f,0});solid(3,{0,0,0,0});
  for(bool hdr:{false,true}){
   auto ps=shader(d,proto+std::string("/WorldComposite")+(hdr?"Hdr":"")+".bin");
   solid(2,hdr?std::array<float,4>{.132868f,.073239f,.033105f,1}:std::array<float,4>{0,0,0,0});
   ok(d->SetPixelShaderConstantF(0,c[0],128));ok(d->SetPixelShader(ps));ok(d->BeginScene());quad();ok(d->EndScene());
   auto values=image(light);
   for(unsigned k=0;k<3;++k){
    const float native=hdr?(k==0?.132868f:k==1?.073239f:.033105f):c[18][k];
    assert(std::fabs(half(values[(W*H/2)*4+k])-native*.5f*.55f)<.001f);
   }
   ps->Release();++darkRoomCases;
  }
  // Keep variation in GI on a planar receiver, even in full shadow and with
  // eligible history. The standalone temporal pass reads s9 as its input.
  auto raw=[&](unsigned x,unsigned){return std::array<float,4>{x%2?-.4f:-.2f,-.3f,-.2f,0};};
  bind(8,W,H,raw);solid(12,{.4f,.3f,.2f,0});c[57][0]=.9f;
  c[3][0]=c[4][1]=c[5][2]=c[53][0]=c[54][1]=c[55][2]=1;
  solid(1,{1000.f/999.8f*(1-.2f/8),0,0,0});solid(13,{8,0,0,0});solid(15,{8,0,0,0});
  for(const char* name:{"RemovalSmooth","TemporalLight","TemporalLightMerged","TemporalLightMergedZeroWeight"}){
   if(!std::strcmp(name,"TemporalLight")){bind(9,W,H,raw);solid(14,{-.25f,-.3f,-.2f,0});}
   else {solid(9,{-.25f,-.3f,-.2f,0});solid(14,{0,0,1,1});}
   auto ps=shader(d,proto+"/"+name+".bin");
   ok(d->SetPixelShaderConstantF(0,c[0],128));ok(d->SetPixelShader(ps));ok(d->BeginScene());quad();ok(d->EndScene());
   auto values=image(light);
   for(unsigned x=2;x<W-2;++x)assert(std::fabs(half(values[((H/2)*W+x)*4])-raw(x,0)[0])<.001f);
   ps->Release();++darkRoomCases;
  }
  // Two projected planes meet at a diagonal. Only neighbouring samples on
  // the fold may blend, and the correction must follow the local baseline.
  const auto fold=[](float x,float y){return x-.6f*y-32.f;};
  const auto foldLight=[&](unsigned x,unsigned y){
   const float light=fold(float(x),float(y))<0?-.3f:-.2f;
   return std::array<float,4>{light,light,light,0};
  };
  bind(8,W,H,foldLight);bind(9,W,H,foldLight);
  bind(12,W,H,[&](unsigned x,unsigned y){
   const float baseline=fold(float(x),float(y))<0?.4f:.8f;
   return std::array<float,4>{baseline,baseline,baseline,1};
  });
  c[0][0]=.5f/W;c[0][1]=.5f/H;c[33][0]=2*W;c[33][1]=2*H;c[57][0]=0;
  for(bool gap:{false,true}){
   bind(1,2*W,2*H,[&](unsigned x,unsigned y){
    const float edge=fold((float(x)-1)*.5f,(float(y)-1)*.5f);
    const float depth=.975f+std::max(edge,0.f)*.00009f+(gap&&edge>=0?.004f:0.f);
    return std::array<float,4>{depth,0,0,0};
   });
   for(bool water:{false,true}){
    c[30][0]=water;c[20][1]=1;c[30][1]=c[30][2]=1;
    solid(11,{4,1,0,0});
    ok(d->SetPixelShaderConstantF(0,c[0],128));
    std::vector<uint16_t> reference;
    for(const char* name:{"TemporalLight","TemporalLightMerged","TemporalLightMergedZeroWeight"}){
     auto ps=shader(d,proto+"/"+name+".bin");
     ok(d->SetPixelShader(ps));ok(d->BeginScene());quad();ok(d->EndScene());
     auto values=image(light);ps->Release();
     if(reference.empty())reference=values;else assert(values==reference);
     unsigned softened=0;
     for(unsigned y=1;y<H-1;++y)for(unsigned x=1;x<W-1;++x){
      const float edge=fold(float(x),float(y));
      const auto i=(y*W+x)*4;
      const float delta=half(values[i])-foldLight(x,y)[0];
      assert(values[i+3]==0);
      if(gap||water||std::fabs(edge)>1)assert(std::fabs(delta)<.001f);
      else if(std::fabs(delta)>.01f){assert(edge<0?delta>0:delta<0);++softened;}
     }
     if(!gap&&!water)assert(softened>H);
    }
   }
  }
  printf("GI_CREASE_PASS diagonal=1 silhouette=1 water=1 alphaExact=1 variantsExact=1\n");
  printf("GI_DARK_ROOM_PASS cases=%u fullAmbientRemoval=1 mixedDirectional=1 receiverHistory=1 lampsPreserved=1\n",darkRoomCases);
 }
 assert(timing||darkPixels);printf("FUSED_GI_PASS cases=%u maxAbs=%.9f baselineAlphaExact=1 prelitCases=%u darkPixels=%u mix0_50_100=1 ordinaryUnaffected=1\n",cases,maximum,prelitCases,darkPixels);return 0;
}

