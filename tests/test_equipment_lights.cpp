// northlight-test: requires=cxx
#include "equipment_lights.h"
#include "equipment_light_proof.h"
#include "local_light_selection.h"
#include "quality_settings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <sstream>
#include <cstdio>
using namespace NorthlightEquipmentLights;
static std::array<float,16> identity(){std::array<float,16> m{};m[0]=m[5]=m[10]=m[15]=1;return m;}
int main(){
    float camera[3]={100,200,300},bank[44]={};auto inv=identity();inv[12]=100;inv[13]=200;inv[14]=300;
    bank[0]=.2f;bank[1]=.4f;bank[2]=.8f;bank[16]=2;bank[17]=-3;bank[18]=4;bank[32]=1;bank[36]=.2f;bank[40]=.01f;
    Native n;assert(decode(bank,0,inv.data(),camera,n));assert(n.position[0]==102&&n.position[1]==197&&n.position[2]==304);
    assert(!decode(bank,4,inv.data(),camera,n));float old=bank[18];bank[18]=1000;assert(!decode(bank,0,inv.data(),camera,n));bank[18]=old;
    old=bank[1];bank[1]=NAN;assert(!decode(bank,0,inv.data(),camera,n));bank[1]=old;
    assert(decode(bank,0,inv.data(),camera,n));auto l=light(n,1,1);assert(NorthlightLocalLights::valid(l));assert(l.diffuse[2]>l.diffuse[1]&&l.diffuse[1]>l.diffuse[0]);assert(l.attenuationEnd<=12);
    assert(std::fabs(l.diffuse[0]/l.diffuse[2]-.25f)<1e-6f); // Actual animated RGB hue, no item/texture name heuristic.
    // 12340 CM2Light defaults are denominator (0,.7,.03). At one yard
    // the compact fit preserves the native source energy (below its cap),
    // rather than applying the MPQ intensity for a second time.
    Native nativeDefault{};nativeDefault.color[0]=.35f;nativeDefault.color[1]=.14f;nativeDefault.color[2]=.07f;nativeDefault.attenuation[1]=.7f;nativeDefault.attenuation[2]=.03f;
    const auto fittedDefault=light(nativeDefault,3,1);assert(std::fabs(fittedDefault.diffuse[0]-.35f/.73f)<1e-6f);
    assert(std::fabs(fittedDefault.diffuse[1]/fittedDefault.diffuse[0]-.4f)<1e-6f);
    std::vector<NorthlightLocalLights::Light> fixed{l};assert(staticDuplicate(n,fixed));fixed[0].position[0]+=1;assert(!staticDuplicate(n,fixed));
    Tracker t;assert(t.admit(bank));assert(!t.admit(bank));for(unsigned i=1;i<MaxBanksPerFrame;++i){bank[0]=float(i);assert(t.admit(bank));}bank[0]=100;assert(!t.admit(bank));assert(t.banks==MaxBanksPerFrame);
    // A/B/A and unused constants used to fill the bank budget again, starving
    // later character draws although no new native source had been seen.
    Tracker memo;bank[0]=.2f;assert(memo.admit(bank,1,false));bank[0]=.3f;assert(memo.admit(bank,1,false));bank[0]=.2f;assert(!memo.admit(bank,1,false));
    bank[3]=17;bank[4]=99;bank[19]=-11;bank[33]=7;assert(!memo.admit(bank,1,false));assert(memo.banks==2&&memo.bankDuplicates==2);
    std::array<float,44> ordered{};for(unsigned i=0;i<2;++i)for(unsigned k=0;k<3;++k){ordered[4*i+k]=float(i+k+1);ordered[4*(4+i)+k]=float(i+k+10);ordered[4*(8+k)+i]=float(i+k+20);}
    assert(memo.admit(ordered.data(),2));auto swapped=ordered;
    for(unsigned k=0;k<3;++k){std::swap(swapped[k],swapped[4+k]);std::swap(swapped[16+k],swapped[20+k]);std::swap(swapped[4*(8+k)],swapped[4*(8+k)+1]);}
    assert(!memo.admit(swapped.data(),2));
    CaptureBudget budget;Tracker reserved;
    for(unsigned i=0;i<400;++i)if(budget.take(false,reserved.banks,reserved.backgroundBanks)){bank[0]=float(i);reserved.admit(bank,1,false);}
    assert(budget.backgroundReads==BackgroundBanks&&reserved.backgroundBanks==BackgroundBanks&&reserved.banks==BackgroundBanks);
    // The late body draw still gets room, within the unchanged total caps.
    assert(budget.take(true,reserved.banks,reserved.backgroundBanks));bank[0]=.9f;assert(reserved.admit(bank,1,true));
    assert(budget.actorReads==1&&reserved.banks==BackgroundBanks+1&&budget.bankLimited==400-BackgroundBanks);
    budget.nextFrame();for(unsigned i=0;i<200;++i)budget.take(false,0,0);assert(budget.backgroundReads==BackgroundReads&&budget.reads==BackgroundReads);
    for(unsigned i=0;i<200;++i)budget.take(true,0,0);assert(budget.reads==MaxReadsPerFrame&&budget.actorReads==MaxReadsPerFrame-BackgroundReads);
    assert(MaxReadsPerFrame==64&&MaxBanksPerFrame==16);budget.nextFrame();assert(!budget.reads&&!budget.candidates);
    t.nextFrame();assert(t.admit(bank));std::vector<NorthlightLocalLights::Light> none;
    t.observe(n,1000,none);t.observe(n,1000,none);assert(t.count==1);t.publish(1000);t.publish(1050);assert(t.outputCount==1&&t.output[0].diffuse[2]>0);
    const auto id=t.output[0].sourceId;n.position[0]+=.1f;n.color[2]=.6f;n.color[1]=.3f;n.color[0]=.15f;t.observe(n,1060,none);t.publish(1060);assert(t.count==1&&t.output[0].sourceId==id);
    t.publish(1160);t.publish(1260);t.publish(1360);assert(!t.count&&!t.outputCount); // No permanent lights after model/visibility disappears.
    t.reset();t.observe(n,0xfffffff0u,none);t.publish(0xfffffff0u);t.publish(0x20u);assert(t.count==1);t.publish(0x200u);t.publish(0x300u);assert(!t.count); // DWORD wrap.
    for(unsigned i=0;i<Capacity+4;++i){n.position[0]=float(i*2);t.observe(n,2000,none);}assert(t.count==Capacity&&t.dropped==4);
    t.reset();t.observe(n,3000,{light(n,2,1)});assert(!t.count&&t.duplicates==1);
    // A closer new source wins a full budget; an almost equal newcomer does
    // not churn the incumbent, and a stale source is immediately replaceable.
    t.reset();for(unsigned i=0;i<Capacity;++i){n.position[0]=camera[0]+20+2*i;t.observe(n,3000,none,camera);}
    n.position[0]=camera[0]+1;t.observe(n,3010,none,camera);bool near=false;for(unsigned i=0;i<t.count;++i)near|=t.entries[i].source.position[0]==n.position[0];assert(near&&t.count==Capacity);
    t.reset();
    // Static publication after capture must remove the previous dynamic light
    // AND the selection's already visible/fading entry, in this same frame.
    n.position[0]=camera[0]+2;Tracker publication;publication.observe(n,4000,none);publication.publish(4000);publication.publish(4050);
    NorthlightLocalLightSelection::Tracker pubSelect;pubSelect.update(none,camera,8,.016f,false,publication.output.data(),publication.outputCount);
    const auto staticSource=light(n,77,1);std::vector<NorthlightLocalLights::Light> appeared{staticSource};
    publication.publish(4060,&appeared);assert(!publication.count&&publication.suppressedCount==1);
    for(unsigned i=0;i<publication.suppressedCount;++i)pubSelect.forget(publication.suppressed[i]);
    auto appearedOut=pubSelect.update(appeared,camera,8,.016f,true);assert(appearedOut.count==1);
    // When FGL removes the source, its native replacement must also remove the
    // old static selection tail instead of adding two versions for .4 seconds.
    publication.observe(n,4070,none);publication.publish(4070);publication.publish(4120);
    replaceFadingDuplicates(pubSelect,publication.output.data(),publication.outputCount);
    auto disappearedOut=pubSelect.update(none,camera,8,.016f,true,publication.output.data(),publication.outputCount);assert(disappearedOut.count==1);
    // Additional lights share the original global limit and deterministic sort.
    fixed={l};Tracker e;e.observe(n,4000,none);e.publish(4000);e.publish(4050);
    NorthlightLocalLightSelection::Tracker select;auto out=select.update(fixed,camera,8,.016f,false,e.output.data(),e.outputCount);assert(out.count==2);
    NorthlightQuality::Settings settings;assert(!settings.equipmentLights);std::vector<std::string> problems;NorthlightQuality::apply({{"EquipmentLights","1",1}},settings,"test",'f',problems,true);assert(settings.equipmentLights==1);
    assert(!NorthlightEquipmentLightProof::analyze(nullptr,0));
    std::puts("equipment lights: world transform, actual hue, invalid input, bank revisit/order/unused-slot dedup, reserved late actor admission under unchanged 64/16 caps, nearest admission, static appears/disappears, movement, fade/TTL, clock wrap, shared selection limit, opt-in PASS");
}
