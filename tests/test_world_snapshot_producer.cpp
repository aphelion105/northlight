// The Python runner inserts StreamDevice::onDraw verbatim into this device.
#include "game_snapshot.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <d3d9.h>
using namespace NorthlightStream;
struct StreamVertexShader {unsigned tags=NorthlightShaderTags::kWorld|NorthlightShaderTags::kWmo;std::uint64_t hash=1;};
template<class T> struct Slot {T v{};bool known=false;void learn(T value){v=value;known=true;}};
struct ProxyBase {struct {UINT w=3840,h=2160;}info;static ProxyBase* of(IDirect3DSurface9*){return nullptr;}};
namespace StreamState {inline void bind(ProxyBase*& dst,ProxyBase* src){dst=src;}}
struct Device {
    ProxyBase target;
    struct State {StreamVertexShader* vs=nullptr;Slot<D3DVIEWPORT9> viewport;Slot<DWORD> rs[256];ProxyBase* rt[1]{};bool rtKnown[1]{true};} st;
    Device(){st.rt[0]=&target;}
    bool backBuffer(ProxyBase* p){return p==&target;}
    HRESULT GetRenderTarget(unsigned,IDirect3DSurface9**){return E_FAIL;}
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
    for(unsigned reject=0;reject<8;++reject){Device menu;StreamVertexShader model;model.tags=NorthlightShaderTags::kWorld;menu.st.vs=&model;
        menu.actualViewport={0,0,3840,2160,0,1};
        switch(reject){case 1:menu.st.rt[0]=nullptr;break;case 2:menu.actualViewport.Width=128;break;
            case 3:menu.actualViewport.MinZ=.94f;break;case 4:menu.actualZ=0;break;case 5:menu.actualFunc=8;break;
            case 6:menu.st.rtKnown[0]=false;break;case 7:menu.viewportFails=true;break;}
        menu.onDraw(4);assert(menu.policy.menu==(reject==0)&&!menu.policy.world);
        assert(menu.snapshots.back()==(reject==0?Trigger::Menu:Trigger::FrameStart));
        if(!reject){menu.onDraw(4);assert(menu.snapshots.size()==1);menu.policy.onPresent();menu.onDraw(4);assert(menu.snapshots.size()==2);}
    }
    std::puts("PASS production onDraw: sky skipped, invalidated state recovered, failed Gets retain World trigger, known state avoids device reads");
}
