// The Python runner inserts StreamDevice::onDraw verbatim into this device.
#include "game_snapshot.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <d3d9.h>
using namespace NorthlightStream;
struct StreamVertexShader {unsigned tags=NorthlightShaderTags::kWorld|NorthlightShaderTags::kWmo;std::uint64_t hash=1;};
template<class T> struct Slot {T v{};bool known=false;void learn(T value){v=value;known=true;}};
struct Device {
    struct State {StreamVertexShader* vs=nullptr;Slot<D3DVIEWPORT9> viewport;Slot<DWORD> rs[256];} st;
    TriggerPolicy policy;bool capture=true;std::uint64_t drawOrdinal=0;
    D3DVIEWPORT9 actualViewport{};DWORD actualZ=1,actualFunc=4;
    bool viewportFails=false,depthFails=false;unsigned viewportGets=0,depthGets=0;
    std::vector<Trigger> snapshots;
    int GetViewport(D3DVIEWPORT9* out){++viewportGets;*out=actualViewport;return viewportFails?-1:0;}
    int GetRenderState(unsigned state,DWORD* out){++depthGets;*out=state==D3DRS_ZENABLE?actualZ:actualFunc;return depthFails?-1:0;}
    void takeSnapshot(Trigger t){if(t!=Trigger::None)snapshots.push_back(t);}
#include "world_snapshot_on_draw.inc"
};
int main(){
    StreamVertexShader vs;Device d;d.st.vs=&vs;
    d.actualViewport={0,0,3840,2160,NorthlightWorldDrawDomain::kSkyMinZ,1};
    d.onDraw(4);assert(d.snapshots==std::vector<Trigger>{Trigger::FrameStart});
    assert(!d.policy.world&&d.viewportGets==1&&!d.depthGets);
    // A state-block/target transition invalidates viewport and depth knowledge.
    // Failed Get must not spend World; a later successful Get must recover it.
    d.st.viewport.known=false;d.actualViewport={0,0,3840,2160,0,.94f};d.depthFails=true;
    d.onDraw(4);assert(!d.policy.world&&d.snapshots.size()==1);
    assert(d.viewportGets==2&&d.depthGets==2);
    d.depthFails=false;d.onDraw(4);assert(d.policy.world&&d.snapshots.back()==Trigger::World);
    assert(d.viewportGets==2&&d.depthGets==4);
    for(unsigned i=0;i<100;++i)d.onDraw(4);
    assert(d.snapshots.size()==2&&d.viewportGets==2&&d.depthGets==4);
    d.policy.onPresent();d.onDraw(4);assert(d.snapshots.size()==3&&d.snapshots.back()==Trigger::World);
    assert(d.viewportGets==2&&d.depthGets==4); // known-state fast path
    d.policy.onPresent();d.st.viewport.known=false;d.viewportFails=true;
    d.onDraw(4);assert(!d.policy.world&&d.snapshots.back()==Trigger::FrameStart);
    d.viewportFails=false;d.onDraw(4);assert(d.policy.world&&d.snapshots.back()==Trigger::World);
    std::puts("PASS production onDraw: sky skipped, invalidated state recovered, failed Gets retain World trigger, known state avoids device reads");
}
