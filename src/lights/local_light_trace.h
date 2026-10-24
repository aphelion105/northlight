#pragma once
#include "local_light_selection.h"

// Sampled submission ledger. Successful draw submission says nothing about a
// light's pixel contribution; that requires the matching scene/depth capture.
namespace NorthlightLocalLightTrace {
using Rect=NorthlightLocalLightScissor::Rect;
enum class Stage {NotSelected,NotReached,Clipped,Submitted,Failed};
inline const char* name(Stage stage){
    switch(stage){case Stage::NotSelected:return "not-selected";case Stage::NotReached:return "not-reached";
    case Stage::Clipped:return "clipped";case Stage::Submitted:return "submitted";default:return "failed";}
}
struct Row {
    NorthlightLocalLights::Light published{};unsigned slot=~0u;
    NorthlightLocalLightSelection::Constant position{},color{},fog{};
    Rect directRect{},fogRect{};Stage direct=Stage::NotSelected,volume=Stage::NotSelected;
    std::int32_t directResult=0,fogResult=0;
};
struct Frame {
    static constexpr unsigned Capacity=8;
    std::array<Row,Capacity> rows{};unsigned count=0,omitted=0;
    bool enabled=false;Stage combine=Stage::NotReached;
    void begin(bool on,const NorthlightLocalLights::Light* published,unsigned size,const NorthlightLocalLightSelection::Selection& selection){
        enabled=on;count=omitted=0;combine=Stage::NotReached;if(!on||!published)return;
        count=std::min(size,Capacity);omitted=size-count;
        for(unsigned i=0;i<count;++i){auto& r=rows[i];r={};r.published=published[i];
            for(unsigned j=0;j<selection.count;++j)if(selection.sources[j].sourceId==r.published.sourceId){
                r.slot=j;r.position=selection.position[j];r.color=selection.color[j];r.fog=selection.fog[j];
                r.direct=r.volume=Stage::NotReached;break;}}
    }
    void batch(bool volume,unsigned first,unsigned size,const Rect& rect,bool drawn,std::int32_t result){
        if(!enabled)return;
        for(unsigned i=0;i<count;++i){auto& r=rows[i];if(r.slot==~0u||r.slot<first||r.slot-first>=size)continue;
            const Stage stage=!drawn?Stage::Clipped:result<0?Stage::Failed:Stage::Submitted;
            if(volume){r.fogRect=rect;r.volume=stage;r.fogResult=result;}else{r.directRect=rect;r.direct=stage;r.directResult=result;}}
    }
};
}
