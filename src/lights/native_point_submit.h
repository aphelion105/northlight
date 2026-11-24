#pragma once
#include <d3d9.h>

// Own the temporary light-bank replacement around one native receiver draw.
// Captures and subsequent draws see the original bank.
class NorthlightNativePointSubmit {
public:
    struct Bank {
        // Proven model/WMO slots use c17..20, terrain uses c28..36.
        unsigned mask;
        bool terrain;
        const float* original;
        const float* replacement;
    };
    struct Work {
        unsigned draws=0, slots=0, terrainDraws=0;
    };

    bool ownsExtra() const { return extraOwnership_; }
    const Work& work() const { return work_; }
    void endFrame() { work_={}; }

    template<class Draw>
    HRESULT draw(IDirect3DDevice9* device, const Bank& bank, bool extra, bool& failed, Draw&& submit) {
        const auto nativeSubmit=[&]() -> HRESULT {
            struct RestoreExtra {
                bool& value;
                bool previous;
                ~RestoreExtra() { value=previous; }
            } saved{extraOwnership_,extraOwnership_};
            extraOwnership_=extra;
            return submit();
        };
        if (!bank.mask) return nativeSubmit();

        unsigned first=0,last=4;
        while (!(bank.mask&(1u<<first))) ++first;
        while (!(bank.mask&(1u<<(last-1)))) --last;
        const unsigned start=bank.terrain?29+3*first:17+first;
        const unsigned count=bank.terrain?3*(last-first)-2:last-first;
        const unsigned offset=bank.terrain?12*first+4:4*first;
        const auto restore=[&] {
            HRESULT result=device->SetVertexShaderConstantF(start,bank.original+offset,count);
            if (FAILED(result)) result=device->SetVertexShaderConstantF(start,bank.original+offset,count);
            if (FAILED(result)) failed=true;
            return result;
        };
        if (FAILED(device->SetVertexShaderConstantF(start,bank.replacement+offset,count))) {
            const HRESULT back=restore();
            return FAILED(back)?back:nativeSubmit();
        }

        struct RestoreBank {
            decltype(restore)& action;
            bool done=false;
            ~RestoreBank() { if (!done) action(); }
        } saved{restore};
        ++work_.draws;
        if (bank.terrain) ++work_.terrainDraws;
        for (unsigned slot=0;slot<4;++slot) work_.slots+=(bank.mask>>slot)&1;

        const HRESULT result=nativeSubmit();
        const HRESULT back=restore();
        saved.done=true;
        return FAILED(back)?back:result;
    }

private:
    bool extraOwnership_=false;
    Work work_{};
};
