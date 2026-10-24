#include "local_light_trace.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace NorthlightLocalLightTrace;
int main(){
    NorthlightLocalLightSelection::Selection selection;
    std::array<NorthlightLocalLights::Light,11> published{};
    for(unsigned i=0;i<published.size();++i){published[i].sourceId=100+i;published[i].position[0]=float(i);published[i].diffuse[1]=.5f;}
    selection.count=20;
    for(unsigned i=0;i<selection.count;++i){selection.sources[i].sourceId=200+i;selection.position[i][0]=float(i);selection.color[i][2]=.25f;selection.fog[i][0]=.003f;}
    selection.sources[1].sourceId=103;selection.sources[8].sourceId=100;selection.sources[16].sourceId=104;
    const auto before=selection;const auto sources=published;
    Frame trace;trace.begin(true,published.data(),published.size(),selection);
    assert(trace.count==8&&trace.omitted==3&&trace.rows[0].slot==8&&trace.rows[3].slot==1&&trace.rows[4].slot==16);
    assert(trace.rows[1].direct==Stage::NotSelected&&trace.rows[0].direct==Stage::NotReached);
    assert(trace.rows[0].position==selection.position[8]&&trace.rows[0].color==selection.color[8]&&trace.rows[0].fog==selection.fog[8]);
    const Rect direct{0,2,30,40},fog{4,5,60,70};
    trace.batch(false,0,16,direct,true,0);
    assert(trace.rows[0].direct==Stage::Submitted&&trace.rows[3].direct==Stage::Submitted&&trace.rows[4].direct==Stage::NotReached);
    trace.batch(false,16,4,direct,true,std::int32_t(-2147467259));
    assert(trace.rows[4].direct==Stage::Failed&&trace.rows[4].directResult<0);
    trace.batch(true,8,4,fog,false,0);
    assert(trace.rows[0].volume==Stage::Clipped&&trace.rows[3].volume==Stage::NotReached&&trace.rows[0].fogRect.right==60);
    assert(trace.rows[0].directRect.left==0&&trace.rows[0].directRect.top==2);
    trace.batch(true,0,4,fog,true,0);assert(trace.rows[3].volume==Stage::Submitted);
    assert(!std::memcmp(&before,&selection,sizeof selection)&&!std::memcmp(sources.data(),published.data(),sizeof published));
    trace.begin(false,published.data(),11,selection);trace.batch(false,0,16,direct,true,0);
    assert(!trace.enabled&&trace.count==0&&trace.omitted==0);
    trace.begin(true,nullptr,11,selection);assert(trace.count==0);
    trace.begin(true,published.data(),0,selection);assert(trace.count==0);
    std::puts("Bounded source identity, reordered slots, submit/clip/failure, exact inputs and disabled path pass");
}
