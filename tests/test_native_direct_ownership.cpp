#include "native_direct_ownership.h"
#include <cassert>
#include <cstdio>
static bool previousAuthoredSlot(const NorthlightLocalLights::Light& light,const NorthlightEquipmentLights::Native& point){
    using namespace NorthlightNativeDirect;
    if(light.kind!=2||(light.flags&NorthlightLocalLights::Flash))return false;
    for(unsigned k=0;k<3;++k)if(!std::isfinite(light.position[k])||!std::isfinite(point.position[k])||!std::isfinite(point.color[k])||point.color[k]<0||!std::isfinite(point.attenuation[k])||point.attenuation[k]<0)return false;
    NorthlightLocalLights::Light native=light;std::copy_n(point.position,3,native.position);std::copy_n(point.color,3,native.diffuse);
    native=NorthlightLocalLights::nativeTransport(native,point.attenuation);
    for(unsigned k=0;k<3;++k)if(std::fabs(light.position[k]-native.position[k])>.005f||!close(light.diffuse[k],native.diffuse[k],.0001f))return false;
    return close(light.attenuationStart,native.attenuationStart,.0001f)&&close(light.attenuationEnd,native.attenuationEnd,.0001f);
}
struct MatchBinding {const NorthlightNativeLights::Point* native=nullptr;const NorthlightLocalLights::Light* authored=nullptr;};
static std::array<int,4> previousMatches(const NorthlightEquipmentLights::Native* values,const bool* valid,unsigned count,const MatchBinding* bindings,unsigned bindingCount){
    std::array<int,4> result{{-1,-1,-1,-1}};
    for(unsigned slot=0;slot<count;++slot)if(valid[slot])for(unsigned i=0;i<bindingCount;++i){const auto& candidate=bindings[i];
        if(candidate.authored?!NorthlightNativeDirect::uniqueAuthoredSlot(*candidate.authored,values,valid,count,slot):!NorthlightNativeDirect::same(values[slot],*candidate.native))continue;
        if(result[slot]>=0){result[slot]=-2;break;}result[slot]=int(i);
    }
    return result;
}
static void matchAllSlots(){
    using namespace NorthlightNativeDirect;
    NorthlightNativeLights::Point points[8]{};NorthlightLocalLights::Light lights[8]{};MatchBinding bindings[128];
    unsigned comparisons=0;
    for(unsigned trial=0;trial<128;++trial){
        for(unsigned i=0;i<8;++i){auto& p=points[i];p.position[0]=-8565+float(i%((trial%4)+1));p.position[1]=1011;p.position[2]=95;
            p.color[0]=float((trial+i)%5+1);p.color[1]=.3f*p.color[0];p.color[2]=.1f*p.color[0];p.attenuation[1]=.7f;p.attenuation[2]=.03f;
            NorthlightLocalLights::Light raw{};raw.kind=2;raw.flags=2;std::copy_n(p.position,3,raw.position);std::copy_n(p.color,3,raw.diffuse);lights[i]=NorthlightLocalLights::nativeTransport(raw,p.attenuation);
            if((trial+i)%13==0)lights[i].flags|=NorthlightLocalLights::Flash;
            if((trial+i)%11==0)lights[i].position[0]+=.0051f;
            if((trial+i)%17==0)lights[i].diffuse[0]=NAN;
        }
        for(unsigned i=0;i<128;++i)bindings[i]=(trial+i)%3?MatchBinding{nullptr,&lights[(trial+i)%8]}:MatchBinding{&points[(trial+i)%8],nullptr};
        NorthlightEquipmentLights::Native values[4];
        for(unsigned s=0;s<4;++s){const auto& p=points[(trial+s)%8];std::copy_n(p.position,3,values[s].position);std::copy_n(p.color,3,values[s].color);std::copy_n(p.attenuation,3,values[s].attenuation);}
        for(unsigned count=1;count<=4;++count)for(unsigned mask=0;mask<16;++mask){bool valid[4];for(unsigned s=0;s<4;++s)valid[s]=(mask&(1u<<s))!=0;
            for(unsigned size:{0u,1u,2u,4u,8u,64u,128u}){
                assert(matchDecoded(values,valid,count,bindings,size)==previousMatches(values,valid,count,bindings,size));++comparisons;}
        }
    }
    // An ambiguous first slot cannot be omitted from another slot's authored
    // uniqueness proof, even though its own binding search has already stopped.
    NorthlightEquipmentLights::Native values[4]{};bool valid[4]={true,true};
    for(unsigned s=0;s<2;++s){values[s].color[0]=10.f*(s+1);values[s].color[1]=5.f*(s+1);values[s].color[2]=float(s+1);values[s].attenuation[1]=.7f;values[s].attenuation[2]=.03f;}
    std::copy_n(values[0].color,3,points[0].color);std::copy_n(values[0].position,3,points[0].position);std::copy_n(values[0].attenuation,3,points[0].attenuation);
    points[1]=points[0];NorthlightLocalLights::Light raw{};raw.kind=2;std::copy_n(values[1].color,3,raw.diffuse);lights[0]=NorthlightLocalLights::nativeTransport(raw,values[1].attenuation);
    bindings[0]={&points[0],nullptr};bindings[1]={&points[1],nullptr};bindings[2]={nullptr,&lights[0]};
    const auto ambiguous=matchDecoded(values,valid,2,bindings,3);assert(ambiguous[0]==-2&&ambiguous[1]==-1);
    assert(ambiguous==previousMatches(values,valid,2,bindings,3));
    assert((matchDecoded(values,valid,5,bindings,3)==std::array<int,4>{{-1,-1,-1,-1}}));
    std::printf("NATIVE_MATCH_ALL_SLOTS_PASS comparisons=%u authored_unique=exact multiple_bindings=ambiguous stopped_slot_uniqueness=preserved no_persistent_cache=1\n",comparisons);
}
static void bankMemoParity(){
    using namespace NorthlightNativeDirect;
    float bank[44]{},view[16]{},camera[3]{};for(unsigned i=0;i<16;++i)view[i]=i%5==0?1.f:0;
    NorthlightNativeLights::Point points[4]{};NorthlightLocalLights::Light lights[4]{};MatchBinding bindings[4];
    for(unsigned s=0;s<4;++s){bank[s*4]=.8f;bank[s*4+1]=.4f;bank[s*4+2]=.2f;bank[(4+s)*4]=float(s);bank[(8+1)*4+s]=.7f;bank[(8+2)*4+s]=.03f;
        points[s].position[0]=float(s);points[s].color[0]=.8f;points[s].color[1]=.4f;points[s].color[2]=.2f;points[s].attenuation[1]=.7f;points[s].attenuation[2]=.03f;
        NorthlightLocalLights::Light raw{};raw.kind=2;std::copy_n(points[s].position,3,raw.position);std::copy_n(points[s].color,3,raw.diffuse);lights[s]=NorthlightLocalLights::nativeTransport(raw,points[s].attenuation);
        bindings[s]=s&1?MatchBinding{&points[s],nullptr}:MatchBinding{nullptr,&lights[s]};}
    BankMemo memo;
    const auto check=[&](unsigned count,const MatchBinding* table,unsigned size,int expectedHit=-1){
        BankMatch expected;NorthlightEquipmentLights::decodeBank(bank,count,view,camera,expected.values,expected.valid);expected.bindings=matchDecoded(expected.values,expected.valid,count,table,size);
        bool hit=false;const auto& value=memo.resolve(bank,count,view,camera,table,size,hit);
        if(expectedHit>=0)assert(hit==bool(expectedHit));
        assert(!std::memcmp(value.values,expected.values,sizeof value.values));assert(!std::memcmp(value.valid,expected.valid,sizeof value.valid));assert(value.bindings==expected.bindings);
    };
    for(unsigned count=1;count<=4;++count){check(count,bindings,4,0);check(count,bindings,4,1);}
    // Every key component, including unused floats, signed zero and NaN
    // payloads, is compared exactly; no rejected slot retains older output.
    for(unsigned group=0;group<3;++group){float* input=group==0?bank:group==1?view:camera;const unsigned size=group==0?44:group==1?16:3;
        for(unsigned i=0;i<size;++i){const float saved=input[i];
            for(float value:{NAN,INFINITY,-INFINITY,-0.f,0.f,1.f,100000.f}){input[i]=value;check(4,bindings,4);check(4,bindings,4,1);}
            input[i]=saved;check(4,bindings,4);}
    }
    MatchBinding other[4];std::copy_n(bindings,4,other);check(4,other,4,0);check(4,other,3,0);check(4,other,3,1);
    // Source inventory mutation invalidates the owner's binding generation.
    lights[0].diffuse[0]=0;memo.reset();check(4,bindings,4,0);check(4,bindings,4,1);
    points[1].color[0]=0;memo.reset();check(4,bindings,4,0);
    memo.reset();check(4,bindings,0,0);check(4,bindings,0,1);
    bool hit=true;const auto& invalid=memo.resolve(nullptr,4,view,camera,bindings,4,hit);assert(!hit);
    for(bool valid:invalid.valid)assert(!valid);for(int index:invalid.bindings)assert(index==-1);
    check(4,bindings,4,0);
    std::puts("NATIVE_BANK_MEMO_PASS exact_bank_count_view_camera table_identity_count reset invalid_results_cleared indices_only live_state_not_retained");
}
int main(){
    matchAllSlots();
    bankMemoParity();
    {struct Binding {const NorthlightNativeLights::Point* native=nullptr;const NorthlightLocalLights::Light* authored=nullptr;float ownerGain=0;};
     for(bool terrain:{false,true}){float bank[60]{},view[16]{},camera[3]{};for(unsigned i=0;i<16;++i)view[i]=i%5==0?1.f:0.f;
         NorthlightNativeLights::Point points[5]{};Binding bindings[5];const unsigned count=terrain?5:4;
         for(unsigned s=0;s<count;++s){auto& p=points[s];p.position[0]=float(s+1);p.color[0]=.8f;p.color[1]=.4f;p.color[2]=.2f;p.attenuation[0]=1;
             bindings[s]={&p,nullptr,s==0?1.f:s==1?.5f:0.f};
             for(unsigned k=0;k<3;++k){bank[terrain?12*s+k:4*(4+s)+k]=p.position[k];bank[terrain?12*s+4+k:4*s+k]=p.color[k];bank[terrain?12*s+8+k:4*(8+k)+s]=p.attenuation[k];}}
         float original[60];std::copy_n(bank,60,original);NorthlightNativeDirect::ownExtraBank(terrain,bank,view,camera,bindings,count);
         for(unsigned i=0;i<60;++i){float expected=original[i];for(unsigned s=0;s<count;++s){const unsigned rgb=terrain?12*s+4:4*s;if(i>=rgb&&i<rgb+3)expected*=1-bindings[s].ownerGain;}assert(bank[i]==expected);}
         std::copy_n(original,60,bank);bindings[1]=bindings[0];NorthlightNativeDirect::ownExtraBank(terrain,bank,view,camera,bindings,2);assert(!std::memcmp(bank,original,sizeof bank));
         bindings[0]={&points[count-1],nullptr,1};NorthlightNativeDirect::ownExtraBank(terrain,bank,view,camera,bindings,1);
         const unsigned last=terrain?12*(count-1)+4:4*(count-1);for(unsigned k=0;k<3;++k)assert(bank[last+k]==0);
     }
     std::puts("NATIVE_EXTRA_OWNER_PASS model4 terrain5 full_and_partial_handoff unmatched_preserved ambiguous_preserved RGB_only=1");}
    using namespace NorthlightNativeDirect;
    NorthlightNativeLights::Snapshot scene;scene.valid=1;scene.scene=1;scene.count=1;auto& point=scene.lights[0];point.id=1;
    point.position[0]=-8564.9423828125f;point.position[1]=1011.7713012695312f;point.position[2]=99.7315444946289f;
    point.color[0]=4;point.color[1]=2.2f;point.color[2]=.8f;point.attenuation[1]=.7f;point.attenuation[2]=.03f;
    NorthlightLocalLights::Light raw{};std::copy_n(point.position,3,raw.position);std::copy_n(point.color,3,raw.diffuse);raw.kind=2;raw.flags=2;raw.sourceId=14933062424609180460ull;
    const auto light=NorthlightLocalLights::nativeTransport(raw,point.attenuation);
    {
        auto shifted=light;shifted.position[2]+=.4f;
        assert(!ownershipGain(shifted,point)); // Coordinates alone cannot authorize a handoff.
        point.emitterOffset[2]=.4f;const auto resolved=NorthlightNativeLights::emitterPosition(point);
        assert(ownershipGain(shifted,point,resolved.data())==1);
        for(float& value:shifted.diffuse)value*=.5f;
        assert(ownershipGain(shifted,point,resolved.data())==.5f);
        shifted.position[0]+=.01f;assert(!ownershipGain(shifted,point,resolved.data()));
        point.emitterOffset[2]=0;
    }
    {
        NorthlightEquipmentLights::Tracker::Entry entry;entry.id=6;entry.seen=1000;
        std::copy_n(point.position,3,entry.source.position);std::copy_n(point.color,3,entry.source.color);std::copy_n(point.attenuation,3,entry.source.attenuation);
        auto selected=light;selected.sourceId=0xfec0000000000000ull|entry.id;selected.flags=0;
        NorthlightNativeLights::Snapshot unavailable;assert(!source(selected,unavailable));
        assert(observedOwnershipGain(selected,entry,1000,unavailable)==1&&observedOwnershipGain(selected,entry,1099,unavailable)==1);
        assert(observedOwnershipGain(selected,entry,1000,scene)==1);
        entry.nativeId=123;assert(observedOwnershipGain(selected,entry,1000,unavailable)==1);
        assert(observedOwnershipGain(selected,entry,1000,scene)==1); // Unrelated scene observations do not revoke this source.
        entry.nativeId=point.id;assert(!observedOwnershipGain(selected,entry,1000,scene));entry.nativeId=0;
        assert(!observedOwnershipGain(selected,entry,1100,unavailable));entry.seen=0xfffffff0u;assert(observedOwnershipGain(selected,entry,0x10u,unavailable)==1);entry.seen=1000;
        for(float fade:{0.f,.25f,.5f,1.f}){auto fading=selected;for(float& c:fading.diffuse)c*=fade;assert(observedOwnershipGain(fading,entry,1000,unavailable)==fade);}
        for(unsigned control=0;control<7;++control){auto changed=selected;auto witness=entry;
            switch(control){case 0:witness.nativeId=point.id;break;case 1:changed.sourceId++;break;case 2:changed.sourceId=raw.sourceId;break;
                case 3:changed.diffuse[1]+=.01f;break;case 4:changed.position[0]+=.02f;break;case 5:changed.attenuationEnd+=1;break;case 6:changed.flags|=NorthlightLocalLights::Flash;break;}
            assert(!observedOwnershipGain(changed,witness,1000,scene));
        }
        std::puts("NATIVE_OBSERVED_OWNER_PASS missing_scene_inventory exact_selected_draw_source fresh_100ms fade clock_wrap identity_hue_position_falloff_and_expiry_controls no_shadow_flag_added");
    }
    assert(ownershipGain(light,point)==1); // Attached/root classification cannot toggle direct RGB ownership.
    point.flags=NorthlightNativeLights::RootAnchor;
    assert(ownershipGain(light,point)==1);
    for(float fade:{0.f,.25f,.5f,1.f}){auto selected=light;for(float& c:selected.diffuse)c*=fade;assert(ownershipGain(selected,point)==fade);}
    for(unsigned control=0;control<7;++control){auto selected=light;auto native=point;
        switch(control){case 0:native.position[0]=NAN;break;case 1:selected.position[0]+=.02f;break;case 2:selected.diffuse[1]+=.01f;break;
            case 3:selected.attenuationEnd+=1;break;case 4:selected.flags|=NorthlightLocalLights::Flash;break;case 5:selected.diffuse[0]=NAN;break;case 6:selected.kind=3;break;}
        assert(ownershipGain(selected,native)==0);
    }
    // The same selected lamp can occupy any native slot, or disappear from the
    // game's bank. Only the former gets removed; other slots and alpha survive.
    struct OwnerBinding {float ownerGain;};const OwnerBinding owners[]={{1},{.5f},{NAN},{0}};
    float input[44],rgb[16];for(unsigned i=0;i<44;++i)input[i]=float(i+1);
    BankMatch owned;for(unsigned s=0;s<4;++s){owned.valid[s]=true;owned.bindings[s]=int(s);}
    assert(ownedBank(input,4,owned,owners,rgb)==3);
    for(unsigned s=0;s<4;++s)for(unsigned c=0;c<4;++c)assert(rgb[s*4+c]==input[s*4+c]*(c==3||s>=2?1:s==0?0:.5f));
    owned.bindings[0]=-1;owned.bindings[1]=-2;assert(!ownedBank(input,4,owned,owners,rgb)&&!std::memcmp(input,rgb,16*sizeof(float)));
    owned.bindings[0]=0;owned.valid[0]=false;assert(!ownedBank(input,4,owned,owners,rgb));
    std::puts("NATIVE_POINT_OWNER_PASS exact_scene fade missing_slot ambiguity alpha unselected_and_mismatched_sources_preserved");
    assert(source(light,scene)==&point);
    NorthlightEquipmentLights::Native value;std::copy_n(point.position,3,value.position);std::copy_n(point.color,3,value.color);std::copy_n(point.attenuation,3,value.attenuation);
    assert(slot(value,scene)==&point);
    auto changed=light;changed.kind=3;assert(!source(changed,scene));changed=light;changed.position[0]+=.02f;assert(!source(changed,scene));
    changed=light;changed.diffuse[0]*=.9f;assert(!source(changed,scene));changed=light;changed.attenuationEnd=24;assert(!source(changed,scene));
    auto moved=value;moved.attenuation[2]=.04f;assert(!slot(moved,scene));moved=value;moved.color[0]+=.01f;assert(!slot(moved,scene));moved=value;moved.position[2]+=.02f;assert(!slot(moved,scene));
    scene.lights[1]=point;scene.lights[1].id=2;scene.count=2;assert(!source(light,scene)&&!slot(value,scene));
    scene.count=0;assert(!source(light,scene)&&!slot(value,scene));scene.count=1;scene.valid=0;assert(!source(light,scene)&&!slot(value,scene));
    // A missing slot on one receiver does not remove scene-level presence.
    scene.valid=1;moved=value;moved.position[0]+=10;assert(!slot(moved,scene)&&source(light,scene)==&point);
    // The native scene inventory can omit a lamp that is present in this draw.
    scene.clear();assert(!source(light,scene)&&authoredSlot(light,value));
    NorthlightEquipmentLights::Native values[4]={value};bool valid[4]={true};
    assert(uniqueAuthoredSlot(light,values,valid,4,0));
    values[1]=value;valid[1]=true;assert(!uniqueAuthoredSlot(light,values,valid,4,0)&&!uniqueAuthoredSlot(light,values,valid,4,1));
    values[1].position[0]+=1;assert(uniqueAuthoredSlot(light,values,valid,4,0)&&!uniqueAuthoredSlot(light,values,valid,4,1));
    changed=light;changed.sourceId++;assert(sameAuthored(light,changed));changed.position[0]+=.02f;assert(!sameAuthored(light,changed));
    changed=light;changed.diffuse[1]+=.01f;assert(!authoredSlot(changed,value));
    changed=light;changed.attenuationEnd+=1;assert(!authoredSlot(changed,value));
    changed=light;changed.position[0]=std::numeric_limits<float>::quiet_NaN();assert(!authoredSlot(changed,value)&&!sameAuthored(light,changed));
    unsigned comparisons=0;
    for(unsigned mode=0;mode<16;++mode)for(unsigned trial=0;trial<4096;++trial){
        auto v=value;auto l=light;const unsigned k=trial%3;
        const float shift=(int(trial%257)-128)*.0001f;
        switch(mode){
        case 0:v.position[k]+=shift;break;
        case 1:l.position[k]+=shift;break;
        case 2:v.color[k]*=float(trial%65)/32;break;
        case 3:v.attenuation[k]+=shift;break;
        case 4:l.diffuse[k]+=shift;break;
        case 5:l.attenuationStart+=shift;break;
        case 6:l.attenuationEnd+=shift;break;
        case 7:v.position[k]=std::numeric_limits<float>::quiet_NaN();break;
        case 8:v.color[k]=std::numeric_limits<float>::infinity();break;
        case 9:v.attenuation[k]=-1;break;
        case 10:l.kind=trial%4;break;
        case 11:l.flags|=NorthlightLocalLights::Flash;break;
        case 12:l.position[k]=v.position[k]=0;v.position[k]+=shift;break;
        case 13:l.position[k]=v.position[k]=99999;v.position[k]+=shift;break;
        case 14:l.diffuse[k]=std::numeric_limits<float>::quiet_NaN();break;
        case 15:l.attenuationEnd=std::numeric_limits<float>::infinity();break;
        }
        assert(authoredSlot(l,v)==previousAuthoredSlot(l,v));++comparisons;
    }
    std::printf("NATIVE_AUTHORED_POSITION_GATE_PASS comparisons=%u same_tolerance_and_result=1\n",comparisons);
    std::puts("NATIVE_AUTHORED_SLOT_PASS incomplete_inventory exact_slot duplicate_slot_and_authored_rejected position_color_range_controls");
    std::puts("NATIVE_DIRECT_OWNERSHIP_PASS exact_current_scene unique_slot duplicate_rejected absent_receiver_slot_preserves_presence");
}
