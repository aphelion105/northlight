#pragma once
#include <d3d9.h>
#include "local_light_scissor.h"

namespace NorthlightLocalLightScissor {

// Shared by the direct lamp, fog, and point correction passes. The camera
// projection and conservative rectangles are supplied by the frame.
class DrawState {
public:
    struct Work {
        unsigned batches=0,clipped=0,skipped=0;
        double coverage=0;
    };

    const Work& work()const{return work_;}
    void resetWork(){work_={};}
    void skipBatches(unsigned count){work_.batches+=count;work_.skipped+=count;}

    bool apply(IDirect3DDevice9* d,const View& view,const Rect& rect){
        if(!Enabled)return true;
        ++work_.batches;
        if(!view.valid){
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            work_.coverage+=1;
            return true;
        }
        const double whole=double(view.halfW)*view.halfH;
        const long area=rect.area();
        work_.coverage+=whole>0?double(area)/whole:1.;
        if(!area){++work_.skipped;return false;}
        if(area<whole)++work_.clipped;
        const RECT clip={rect.left,rect.top,rect.right,rect.bottom};
        d->SetScissorRect(&clip);
        return true;
    }

    bool applySphere(IDirect3DDevice9* d,const View& view,const float (&sphere)[4]){
        if(!Enabled)return true;
        Bounds bounds;
        if(view.valid)add(view,sphere,bounds);
        return apply(d,view,rect(view,bounds));
    }

private:
    Work work_;
};

}
