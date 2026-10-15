// Task 43 (B): unused-capture memory (unused_capture.h): streak, information frames, refresh, root cell, staleness, bound.
#include "unused_capture.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightUnusedCapture;
int main(){
    const float a[3]={10.2f,20.7f,3.f},sameCell[3]={10.9f,20.1f,3.4f},other[3]={11.2f,20.7f,3.f};
    assert(key(1,100,500,a)==key(1,100,500,sameCell)&&key(1,100,500,a)!=key(1,100,500,other)&&key(1,100,500,a)!=key(2,100,500,a)&&key(1,100,500,a)!=key(1,101,500,a));
    Memory m;const auto k=key(1,100,500,a);
    assert(!m.predict(k)); /* unknown */
    for(unsigned i=0;i<MinStreak-1;++i){m.observe(k,false,true);m.endFrame();assert(!m.predict(k));}
    m.observe(k,false,true);m.endFrame();
    unsigned yes=0,no=0;for(unsigned i=0;i<RefreshEvery*3;++i){(m.predict(k)?yes:no)++;m.endFrame();}
    assert(no==3&&yes==RefreshEvery*3-3); /* every RefreshEvery-th time the capture is due */
    { /* frames without every consumer prove nothing: no streak, and no reset either */
        Memory n;for(unsigned i=0;i<50;++i){n.observe(k,false,false);n.endFrame();}assert(!n.predict(k)&&n.size()==0);
        for(unsigned i=0;i<MinStreak;++i){n.observe(k,false,true);n.endFrame();n.observe(k,false,false);n.endFrame();}
        assert(n.size()==1);
    }
    { /* a use forgets the entry at once */
        Memory n;for(unsigned i=0;i<MinStreak;++i){n.observe(k,false,true);n.endFrame();}assert(n.predict(k));
        n.observe(k,true,false);assert(n.size()==0&&!n.predict(k));
    }
    { /* unseen for more than Stale capture frames: forgotten */
        Memory n;for(unsigned i=0;i<MinStreak;++i){n.observe(k,false,true);n.endFrame();}
        for(unsigned i=0;i<Stale+1;++i)n.endFrame();assert(!n.predict(k)&&n.size()==0);
    }
    { /* bounded */
        Memory n;for(unsigned i=0;i<MaxEntries+100;++i){const float r[3]={float(i),0,0};n.observe(key(1,1,1,r),false,true);}assert(n.size()==MaxEntries);
    }
    { /* a draw that moves to another cell restarts */
        Memory n;for(unsigned i=0;i<MinStreak;++i){n.observe(k,false,true);n.endFrame();}assert(n.predict(k));assert(!n.predict(key(1,100,500,other)));
    }
    { /* saturation: 40 walkers (a new key per 1 yd step) must not starve a stationary unused NPC added later */
        Memory n;for(unsigned f=0;f<20000;++f){for(int w=0;w<40;++w){const float r[3]={float(w*50)+f*0.3f,float(w),0};const auto q=key(0x1000+w,100,500,r);n.predict(q);n.observe(q,false,true);}n.endFrame();}
        assert(n.size()<=MaxEntries);
        const float st[3]={-500,-500,0};const auto q=key(0x9999,100,500,st);unsigned pred=0;
        for(unsigned i=0;i<100;++i){pred+=n.predict(q);n.observe(q,false,true);n.endFrame();}
        assert(pred>=75);
    }
    std::puts("unused capture ok");return 0;
}
