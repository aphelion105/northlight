#include "lighting_pair_timing.h"
// Four native lamps on the WMO blend modes recorded in Wintergarde Keep.
template<class Draw,class Read,class OldDraw> void nativeLayeredLightChecks(IDirect3DDevice9* d,NorthlightNativePointSubmit& owner,float (&constants)[256][4],Draw submit,Read read,OldDraw oldDraw){
    for(unsigned slot=0;slot<4;++slot){
        constants[17+slot][0]=.25f;constants[17+slot][1]=.2f;constants[17+slot][2]=.1f;
        constants[21+slot][0]=float(slot)-1.5f;constants[21+slot][1]=0;constants[21+slot][2]=4;constants[21+slot][3]=1;
        constants[25][slot]=.2f;constants[26][slot]=.1f;constants[27][slot]=.01f;
    }
    float original[16],zero[16];std::copy_n(constants[17],16,original);std::copy_n(original,16,zero);
    for(unsigned slot=0;slot<4;++slot)for(unsigned c=0;c<3;++c)zero[slot*4+c]=0;
    unsigned cases=0,permutations=0;float worstSwitch=0;bool damaged=false;
    for(auto blend:std::initializer_list<std::pair<DWORD,DWORD>>{{D3DBLEND_SRCALPHA,D3DBLEND_ZERO},{D3DBLEND_INVSRCALPHA,D3DBLEND_ONE},{D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA}}){
        must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));must(d->SetRenderState(D3DRS_SRCBLEND,blend.first));must(d->SetRenderState(D3DRS_DESTBLEND,blend.second));
        must(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
        assert(NorthlightNativeDirect::receiverOwnership(false,true,true,true,blend.first,blend.second,D3DBLENDOP_ADD));
        assert(!NorthlightNativeDirect::receiverOwnership(false,true,false,true,blend.first,blend.second,D3DBLENDOP_ADD));
        assert(!NorthlightNativeDirect::receiverOwnership(false,false,true,true,blend.first,blend.second,D3DBLENDOP_ADD));
        auto render=[&](unsigned mode){
            must(d->SetVertexShaderConstantF(0,constants[0],256));if(mode==1)must(d->SetVertexShaderConstantF(17,zero,4));
            must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());
            if(mode==2)must(owner.draw(d,{15,false,original,zero},false,damaged,submit));
            else if(mode==3)must(owner.draw(d,{3,false,original,zero},false,damaged,submit)); // Old two-source fallback leaves two lamps.
            else must(submit());
            must(d->EndScene());float restored[16];must(d->GetVertexShaderConstantF(17,restored,4));
            assert(!std::memcmp(restored,mode==1?zero:original,sizeof restored));return read();
        };
        const auto native=render(0),expected=render(1),fixed=render(2),two=render(3);
        assert(fixed==expected);float signal=0,leak=0;
        for(size_t i=0;i<native.size();++i){if(i%4==3){assert(native[i]==fixed[i]);continue;}signal=std::max(signal,native[i]-expected[i]);leak=std::max(leak,two[i]-expected[i]);}
        assert(signal>.01f&&leak>.01f);++cases;
        std::printf("LAYERED_WMO sources=4 src=%lu dst=%lu fourSourceError=0 oldTwoSourceLeak=%g\n",(unsigned long)blend.first,(unsigned long)blend.second,leak);
        // The client can reorder its four slots as the camera moves. Preserve
        // the same physical sources while changing only their bank order.
        // Full ownership must remain exact; a two-slot correction switches
        // which lamps leak even though neither the lamps nor receiver moved.
        float bank[11][4];std::copy_n(constants[17],44,bank[0]);
        unsigned order[]={0,1,2,3};std::vector<float> firstLimited;
        do {
            for(unsigned slot=0;slot<4;++slot){
                std::copy_n(bank[order[slot]],4,constants[17+slot]);
                std::copy_n(bank[4+order[slot]],4,constants[21+slot]);
                for(unsigned row=0;row<3;++row)constants[25+row][slot]=bank[8+row][order[slot]];
            }
            std::copy_n(constants[17],16,original);std::copy_n(original,16,zero);
            for(unsigned slot=0;slot<4;++slot)for(unsigned channel=0;channel<3;++channel)zero[slot*4+channel]=0;
            const auto independent=render(1),all=render(2),limited=render(3);
            assert(all==independent&&all==expected);
            if(firstLimited.empty())firstLimited=limited;
            else for(size_t i=0;i<limited.size();++i)if(i%4!=3)worstSwitch=std::max(worstSwitch,std::fabs(limited[i]-firstLimited[i]));
            ++permutations;
        } while(std::next_permutation(order,order+4));
        std::copy_n(bank[0],44,constants[17]);std::copy_n(constants[17],16,original);
        if(std::getenv("NORTHLIGHT_LIGHTING_BENCH")){
            const bool control=std::getenv("NORTHLIGHT_LIGHTING_CONTROL")!=nullptr;
            char name[64];std::snprintf(name,sizeof name,"layered-%lu-%lu-%s",(unsigned long)blend.first,(unsigned long)blend.second,control?"AA":"AB");
            lightingPairTiming(d,name,64,[&](unsigned){must(d->SetVertexShaderConstantF(0,constants[0],256));},
                [&](unsigned mode){return mode&&!control?owner.draw(d,{15,false,original,zero},false,damaged,submit):oldDraw();});
        }
    }
    for(unsigned factor:{3u,4u,9u,10u,11u})assert(!NorthlightNativeDirect::receiverOwnership(false,true,true,true,factor,1,1));
    assert(permutations==72&&worstSwitch>.001f&&!damaged);
    std::printf("LAYERED_WMO_GPU_PASS cases=%u permutations=%u allFourRemoved=1 alphaPreserved=1 constantsRestored=1 allFourSwitch=0 twoSlotSwitch=%g\n",cases,permutations,worstSwitch);
}
