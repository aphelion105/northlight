#pragma once
#include <cstdint>

namespace NorthlightM2Draw {
// Identity words from the submitting instance. Replay never follows these
// addresses. The palette itself travels in the ordered D3D constant commands.
struct Section {
    std::uint32_t model,section,material;
    std::uint16_t bones,combo;
};
static_assert(sizeof(Section)==16);
inline thread_local const Section* current=nullptr;
struct Scope {
    const Section* before=current;
    explicit Scope(const Section* section){current=section;}
    ~Scope(){current=before;}
};
}
