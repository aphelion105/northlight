#pragma once
// Diagnostics only (SPELLSPAN, Diagnostics=1 sampled frames): what the replay thread executes between the first and the last deferred draw of a spell layer segment, by kind.
// The replay loop (replay_thread.h, execute) counts every command into `tracker.live` while `tracker.on`; the Device tells the tracker about each deferred draw and the
// tracker keeps, per segment (a run of deferred draws between two drains), the counts at the first and the last one. One thread: the one that executes the stream
// (the Device runs on it too); nothing here is atomic. With CommandStream=0 nothing counts, so no line is written.
#include <cstdint>
namespace NorthlightSpan {
enum Kind:unsigned{Rs,Ss,Tex,VsF,PsF,VsI,PsI,Shader,Decl,Stream,Index,Viewport,Fixed,Draw,Lock,Upload,Other,KindCount};
struct Counts{
    uint64_t calls[KindCount]={},words[KindCount]={}; // words: the payload in 32-bit words (float4 = 4, a render state = 1, a viewport = 6, a transform = 16 ...)
    bool isSet(unsigned k)const{return k<=Fixed;}
    uint64_t sets()const{uint64_t n=0;for(unsigned k=0;k<=Fixed;++k)n+=calls[k];return n;}
    uint64_t setWords()const{uint64_t n=0;for(unsigned k=0;k<=Fixed;++k)n+=words[k];return n;}
    void add(const Counts& a,const Counts& b){for(unsigned k=0;k<KindCount;++k){calls[k]+=a.calls[k]-b.calls[k];words[k]+=a.words[k]-b.words[k];}} // += a-b
};
struct Tracker{
    bool on=false;Counts live;
    struct Frame{unsigned segments=0,deferred=0;Counts diff;} frame;
    bool open=false;Counts start,end;unsigned segDeferred=0,segDrains=0;
    void count(Kind k,uint64_t words=1){++live.calls[k];live.words[k]+=words;}
    // A draw was deferred; `drains` is the layer's running drain count: a change since the last deferred draw closed the previous segment.
    void deferred(unsigned drains){
        if(open&&drains!=segDrains)close();
        if(!open){open=true;start=live;segDeferred=0;segDrains=drains;}
        end=live;++segDeferred;
    }
    void close(){if(!open)return;open=false;++frame.segments;frame.deferred+=segDeferred;frame.diff.add(end,start);}
    // Draws other than the deferred ones inside the spans: the diff covers (first, last] of each segment, so it holds deferred-1 deferred draws per segment.
    uint64_t otherDraws()const{const uint64_t d=frame.diff.calls[Draw],inside=frame.deferred-frame.segments;return d>inside?d-inside:0;}
    void clearFrame(){frame=Frame{};}
};
inline Tracker tracker;
}
