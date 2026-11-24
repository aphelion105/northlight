#pragma once
#include "gx_indexed_12340.h"

namespace NorthlightM2Draw {
using NorthlightGx12340::Range;

constexpr unsigned PaletteFirst=31,MaxBones=75;
inline std::uint64_t paletteHash(const float* rows,unsigned bones){
    const auto* bytes=reinterpret_cast<const unsigned char*>(rows);
    std::uint64_t hash=0xcbf29ce484222325ull;
    for(unsigned i=0;i<bones*3*4*sizeof(float);++i)hash=(hash^bytes[i])*0x100000001b3ull;
    return hash;
}

// Identity words from the submitting instance. Replay never follows these
// addresses. The palette itself travels in the ordered D3D constant commands.
struct Section {
    std::uint32_t model,section,material;
    std::uint16_t bones,combo;
    Range range;
    bool paletteAudited=false;
    std::uint64_t auditedPalette=0,producerFrame=0;
};

struct ConsumerStats {
    std::uint64_t producerFrame=0;
    unsigned seen=0,eligible=0,rangeMismatch=0,coverageMismatch=0,queued=0,immutable=0;
    unsigned paletteChecks=0,paletteMismatch=0,paletteUnavailable=0;

    bool admit(const Section& section,const Range* range,bool opaque){
        producerFrame=section.producerFrame;++seen;
        if(!range||!(*range==section.range)){++rangeMismatch;return false;}
        if(!opaque){++coverageMismatch;return false;}
        ++eligible;return true;
    }
    void packet(const Section& section,bool retained,unsigned first,unsigned count,const float* bank){
        ++queued;immutable+=retained;
        if(!section.paletteAudited)return;
        const unsigned end=PaletteFirst+3u*section.bones;
        if(!bank||!section.bones||section.bones>MaxBones||first>PaletteFirst||count<end-first){++paletteUnavailable;return;}
        ++paletteChecks;
        paletteMismatch+=paletteHash(bank+PaletteFirst*4,section.bones)!=section.auditedPalette;
    }
};

inline thread_local const Section* current=nullptr;
struct Scope {
    const Section* before=current;
    explicit Scope(const Section* section){current=section;}
    ~Scope(){current=before;}
};
}
