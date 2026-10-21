#pragma once
#include <array>
#include <cstdint>
namespace NorthlightWaterMaterial {
// Weak identities; texture creation invalidates reused addresses. Verified liquid
// shaders teach the actual lookup texture, including HD client replacements.
class Detector {
    struct Entry {const void* texture=nullptr;bool learned=false;};
    std::array<Entry,32> entries{};unsigned next=0;
public:
    void forget(const void* texture){for(auto& e:entries)if(e.texture==texture)e={};}
    void noteCreate(const void* texture,unsigned w,unsigned h,unsigned usage){
        forget(texture);
        if(texture&&!usage&&(w==2||w==4||w==8)&&h==w*8)entries[next++%entries.size()]={texture,false};
    }
    bool candidate(const void* texture)const{if(texture)for(const auto& e:entries)if(e.texture==texture)return true;return false;}
    void learn(const void* texture){if(!texture)return;for(auto& e:entries)if(e.texture==texture){e.learned=true;return;}entries[next++%entries.size()]={texture,true};}
    bool matches(const void* lookup,unsigned w,unsigned h,unsigned usage)const{
        if(!lookup||usage||!w||!h)return false;
        for(const auto& e:entries)if(e.texture==lookup)return e.learned||(w==h&&(w==128||w==256||w==512||w==1024));
        return false;
    }
};
}
