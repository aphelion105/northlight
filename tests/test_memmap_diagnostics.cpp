// Host test of the pure MEMMAP logic (address_space_snapshot.h) with fake VirtualQuery region lists.
#include "address_space_snapshot.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
using namespace NorthlightMemMap;
static std::vector<std::string> lines;
static void sink(const char* l){lines.emplace_back(l);}
static void name(std::uint64_t base,unsigned,char* out,std::size_t n){std::snprintf(out,n,"mod%llx.dll",(unsigned long long)base);}
static const std::uint64_t M=1u<<20;
static Region R(std::uint64_t base,std::uint64_t alloc,std::uint64_t size,unsigned st,unsigned type,unsigned prot=4){Region r;r.base=base;r.allocBase=alloc;r.size=size;r.state=st;r.type=type;r.protect=prot;return r;}
static bool has(const char* needle){for(auto& l:lines)if(l.find(needle)!=std::string::npos)return true;return false;}
int main(){
    Collector a;a.begin();
    a.add(R(0x10000,0,0x1000,Free,NoType));
    a.add(R(0x400000,0x400000,0x1000,Commit,Image,2));a.add(R(0x401000,0x400000,3*M,Commit,Image,0x20));      // image 0x400000: 3 MiB + 4 KiB, all committed
    a.add(R(0x1000000,0x1000000,10*M,Commit,Private));a.add(R(0x1A00000,0x1000000,30*M,Reserve,Private));    // private 40 MiB, 10 committed
    a.add(R(0x4000000,0x4000000,50*M,Free,NoType));
    a.add(R(0x8000000,0x8000000,5*M,Commit,Mapped));
    a.add(R(0x9000000,0x9000000,2*M,Reserve,Private));
    const Totals& t=a.totals();
    assert(t.regions==8&&t.allocations==4&&t.freeBytes==50*M+0x1000&&t.largestFree==50*M);
    assert(t.commit[Image]==3*M+0x1000&&t.commit[Private]==10*M&&t.reserve[Private]==32*M&&t.commit[Mapped]==5*M);
    // regions of one AllocationBase around a free gap are two allocations
    Alloc best[TopCount];std::size_t n=a.top(best,TopCount);assert(n==4&&best[0].base==0x1000000&&best[0].total==40*M&&best[0].committed==10*M&&best[1].base==0x8000000);
    n=a.top(best,2);assert(n==2&&best[0].total>=best[1].total&&best[1].base==0x8000000);
    // second snapshot: private heap shrank by 20 MiB (changed), mapped gone, a 8 MiB private new, a 512 KiB new (ignored)
    Collector b;b.begin();
    b.add(R(0x400000,0x400000,3*M+0x1000,Commit,Image));
    b.add(R(0x1000000,0x1000000,20*M,Commit,Private));
    b.add(R(0x4000000,0x4000000,8*M,Commit,Private));
    b.add(R(0x5000000,0x5000000,M/2,Commit,Private));
    b.add(R(0x9000000,0x9000000,2*M,Reserve,Private));
    Diff d;diff(a.allocs(),b.allocs(),d);
    assert(d.addedCount==1&&d.added[0].a.base==0x4000000&&d.addedBytes==8*M);
    assert(d.goneCount==1&&d.gone[0].a.base==0x8000000&&d.goneBytes==5*M);
    assert(d.changedCount==1&&d.changed[0].a.base==0x1000000&&d.changed[0].before==40*M&&d.changedDelta==20*M&&d.changedGrew==0);
    report(sink,"device=2 point=create frame=0 tick=1",b,&a,&name,0.5);
    assert(has("MEMMAP summary device=2 point=create")&&has("MEMMAP top ")&&has("module=mod400000.dll")&&has("MEMMAP diff-new ")&&has("MEMMAP diff-gone ")&&has("MEMMAP diff-changed ")&&has("wasMiB=40.0"));
    assert(has("netMiB=-17.0"));
    lines.clear();report(sink,"x",a,nullptr,nullptr,0.0);assert(has("previous=none")&&!has("module="));
    // identical maps: nothing
    Diff same;diff(a.allocs(),a.allocs(),same);assert(!same.addedCount&&!same.goneCount&&!same.changedCount);
    // diff list cap: counts and bytes stay exact while stored entries are capped
    Collector many,none;many.begin();none.begin();for(unsigned i=0;i<100;++i)many.add(R(0x10000000+std::uint64_t(i)*0x1000000,0x10000000+std::uint64_t(i)*0x1000000,2*M,Commit,Private));
    diff(none.allocs(),many.allocs(),d);assert(d.addedCount==100&&d.nAdded==DiffCap&&d.addedBytes==200*M);
    // capacity: past MaxAllocs the allocations are dropped, regions of a dropped allocation do not leak into the previous one
    Collector full;full.begin();for(unsigned i=0;i<MaxAllocs+10;++i){full.add(R(0x10000+std::uint64_t(i)*0x20000,0x10000+std::uint64_t(i)*0x20000,0x1000,Commit,Private));full.add(R(0x11000+std::uint64_t(i)*0x20000,0x10000+std::uint64_t(i)*0x20000,0x1000,Reserve,Private));}
    assert(full.allocs().size()<=full.allocs().capacity()&&full.totals().dropped>=10&&full.allocs().back().total==0x2000);
    Collector swapA,swapB;swapA.begin();swapA.add(R(0x10000,0x10000,0x1000,Commit,Private));swapA.swap(swapB);assert(swapB.allocs().size()==1&&swapA.allocs().empty());
    // ---- task 42: size histogram, module totals, mapped, stacks/heaps tagging, diff classes ----
    {
        // 16 identical 28 MiB reserve-only private slots at a fixed stride, two 4 MiB ones, one committed 28 MiB, mapped reserve-only 2 MiB x3, an image
        Collector h;h.begin();
        const std::uint64_t slot=0x1c30000;                       // 29,425,664 B = 28.19 MiB (the 0x1c30000 stride seen on Windows/NVIDIA)
        for(unsigned i=0;i<16;++i)h.add(R(0x20000000+std::uint64_t(i)*slot,0x20000000+std::uint64_t(i)*slot,slot,Reserve,Private,0));
        h.add(R(0x40000000,0x40000000,4*M,Reserve,Private,0));h.add(R(0x41000000,0x41000000,4*M+0x3000,Reserve,Private,0));   // +12 KiB rounds into the same 64 KiB bucket
        h.add(R(0x42000000,0x42000000,slot,Commit,Private));
        for(unsigned i=0;i<3;++i)h.add(R(0x50000000+std::uint64_t(i)*0x1000000,0x50000000+std::uint64_t(i)*0x1000000,2*M,Reserve,Mapped,0));
        h.add(R(0x60000000,0x60000000,M,Commit,Image));
        std::vector<SizeGroup> g;g.reserve(MaxAllocs);sizeHistogram(h.allocs(),g);
        assert(round64k(0x1000)==0&&round64k(0x8000)==0x10000&&round64k(4*M+0x3000)==4*M);
        // reserve-only first: 28 MiB x16 (448+ MiB) leads, then the 4 MiB pair, then the mapped 2 MiB x3, then the committed group
        assert(g.size()==4&&g[0].cls==0&&g[0].type==Private&&g[0].count==16&&g[0].total==16*slot&&g[0].size==round64k(slot));
        assert(g[1].cls==0&&g[1].type==Private&&g[1].count==2&&g[1].total==8*M+0x3000);
        assert(g[2].cls==0&&g[2].type==Mapped&&g[2].count==3&&g[2].total==6*M&&g[3].cls==1&&g[3].count==1&&g[3].committed==slot);
        lines.clear();emitSizeHist(sink,"L",g);
        assert(has("class=reserve-only groups=3 allocations=21")&&has("class=committed groups=1 allocations=1")&&has("#0 type=PRIVATE sizeMiB=28.19 count=16 totalMiB=451.0 commitMiB=0.0"));
        // more groups than HistLines are cut at HistLines per class, never silently: the header carries the full group count
        Collector wide;wide.begin();for(unsigned i=0;i<40;++i)wide.add(R(0x10000000+std::uint64_t(i)*0x4000000,0x10000000+std::uint64_t(i)*0x4000000,(2+i)*M,Reserve,Private,0));
        sizeHistogram(wide.allocs(),g);lines.clear();emitSizeHist(sink,"W",g);
        assert(has("class=reserve-only groups=40")&&has("#15 type=")&&!has("#16 type="));
        // module totals: two allocations of one module sum, an unnamed image is grouped, order by size
        Collector mc;mc.begin();
        mc.add(R(0x70000000,0x70000000,3*M,Commit,Image));mc.add(R(0x71000000,0x71000000,M,Commit,Image));mc.add(R(0x72000000,0x72000000,5*M,Commit,Image));mc.add(R(0x73000000,0x73000000,0x10000,Commit,Image));
        auto nm=[](std::uint64_t base,unsigned,char* out,std::size_t n){std::snprintf(out,n,"%s",base==0x72000000||base==0x71000000?"big.dll":base==0x70000000?"small.dll":"");};
        std::vector<ModuleRow> rows;moduleTotals(mc.allocs(),nm,rows);
        assert(rows.size()==3&&!std::strcmp(rows[0].name,"big.dll")&&rows[0].total==6*M&&rows[0].count==2&&!std::strcmp(rows[1].name,"small.dll")&&!std::strcmp(rows[2].name,"(unnamed)")&&rows[2].total==0x10000);
        lines.clear();emitModules(sink,"M",rows);assert(has("MEMMAP modules M count=3")&&has("remainderModules=0")&&has("#0 name=big.dll sizeMiB=6.0"));
        std::vector<ModuleRow> many;for(unsigned i=0;i<25;++i){ModuleRow r;std::snprintf(r.name,sizeof r.name,"m%u.dll",i);r.total=(100-i)*M;r.count=1;many.push_back(r);}
        lines.clear();emitModules(sink,"M",many);assert(has("shown=20 remainderModules=5")&&has("#19 name=m19.dll")&&!has("#20 name="));
        // mapped: names only for MAPPED, top is restricted to the type
        Alloc maps[MappedLines];std::size_t mn=h.top(maps,MappedLines,Mapped);assert(mn==3&&maps[0].type==Mapped&&maps[0].total==2*M);
        lines.clear();report(sink,"R",h,nullptr,&name,0.0);assert(has("MEMMAP sizehist R")&&has("MEMMAP modules R")&&has("MEMMAP mapped R count=3 totalMiB=6.0 shown=3")&&has("type=MAPPED prot=0x00 file=mod50000000.dll"));
    }
    {
        // stack and heap tagging
        Collector c;c.begin();
        c.add(R(0x100000,0x100000,0xF0000,Reserve,Private,0));c.add(R(0x1F0000,0x100000,0x10000,Commit,Private,0x104));        // stack A: 1 MiB, last 64 KiB committed (guard)
        c.add(R(0x300000,0x300000,0x200000,Commit,Private));                                                                  // heap segment
        c.add(R(0x600000,0x600000,0x80000,Reserve,Private,0));c.add(R(0x680000,0x600000,0x80000,Commit,Private));            // stack B: 1 MiB, half committed
        c.add(R(0x900000,0x900000,0x400000,Commit,Mapped));
        assert(c.find(0x1FFFFF)&&c.find(0x1FFFFF)->base==0x100000&&!c.find(0x2FFFFF)&&c.find(0x300000)->base==0x300000&&!c.find(0x50));
        StackSpan sp[4];sp[0].base=0x200000;sp[0].limit=0x1F0000;sp[1].base=0x700000;sp[1].limit=0x680000;sp[2].base=0x700000;sp[2].limit=0x680000;   // a duplicate span counts once
        sp[3].base=0x9999000;sp[3].limit=0x9990000;                                                                           // no such allocation: unmatched
        const StackStats st=tagStacks(c,sp,4);
        assert(st.threads==4&&st.count==2&&st.unmatched==1&&st.total==0x200000&&st.committed==0x10000+0x80000&&st.reserveOnly==0x200000-0x90000&&st.largest==0x100000);
        const std::uint64_t hb[3]={0x300000,0x100000,0x12345678};     // the second is already a stack: a heap tag must not take it over
        assert(tagHeaps(c,hb,3)==2);
        assert(c.find(0x100000)->owner==Stack&&c.find(0x300000)->owner==Heap&&c.find(0x900000)->owner==Unowned);
        assert(!c.tagAt(0x7777777,Heap)&&c.tagAt(0x900000,Heap)&&c.find(0x900000)->owner==Heap&&c.tagAt(0x900000,Stack)&&c.find(0x900000)->owner==Stack);   // stack overrides heap
        Collector im;im.begin();im.add(R(0x400000,0x400000,M,Commit,Image));assert(im.find(0x400000)->owner==Module&&im.tagAt(0x400000,Stack)&&im.find(0x400000)->owner==Module);
        lines.clear();report(sink,"T",c,nullptr,nullptr,0.0);assert(has("owner=stack")&&has("owner=heap"));   // top lines mark both
        lines.clear();emitStacks(sink,"T",st,5,1);assert(has("MEMMAP stacks T count=2 reserveMiB=1.4 commitMiB=0.6 largestMiB=1.0 totalMiB=2.0 threads=4 enumerated=5 unreadable=1 unmatched=1"));
    }
    {
        // diff classes: sums by type x commit/reserve-only and owner, not capped by the stored lists
        Collector p,q;p.begin();q.begin();
        for(unsigned i=0;i<60;++i){const std::uint64_t b=0x10000000+std::uint64_t(i)*0x400000;q.add(R(b,b,2*M,Reserve,Private,0));}                       // 60 new reserve-only 2 MiB (list cap is 48)
        p.add(R(0x30000000,0x30000000,8*M,Commit,Mapped));                                                                                    // gone mapped committed
        p.add(R(0x40000000,0x40000000,4*M,Commit,Private));q.add(R(0x40000000,0x40000000,4*M,Commit,Private));q.add(R(0x40400000,0x40000000,6*M,Reserve,Private,0));   // grown by 6 MiB reserve-only
        p.add(R(0x50000000,0x50000000,10*M,Commit,Private));q.add(R(0x50000000,0x50000000,3*M,Commit,Private));                              // shrank by 7 MiB committed
        // owners: tag a stack among the new ones and the grown one as heap
        q.tagAt(0x10000000,Stack);q.tagAt(0x40000000,Heap);
        Diff d;diff(p.allocs(),q.allocs(),d);
        assert(d.addedCount==60&&d.nAdded==DiffCap&&d.addedBytes==120*M&&d.cls[KNew][Private][1]==120*M&&d.cls[KNew][Private][0]==0&&d.own[KNew][Stack]==2*M&&d.own[KNew][Unowned]==118*M);
        assert(d.cls[KGone][Mapped][0]==8*M&&d.cls[KGone][Mapped][1]==0);
        assert(d.cls[KGrown][Private][1]==6*M&&d.cls[KGrown][Private][0]==0&&d.own[KGrown][Heap]==6*M);
        assert(d.cls[KShrunk][Private][0]==7*M&&d.cls[KShrunk][Private][1]==0&&d.own[KShrunk][Unowned]==7*M);
        lines.clear();emitDiffClasses(sink,"D",d);assert(lines.size()==1&&lines[0].size()<767);
        assert(has("new{image=0.0/0.0 mapped=0.0/0.0 private=0.0/120.0 | stack=2.0 heap=0.0 module=0.0 unknown=118.0}")&&has("gone{image=0.0/0.0 mapped=8.0/0.0")&&has("grown{")&&has("heap=6.0")&&has("shrunk{"));
        lines.clear();report(sink,"D",q,&p,nullptr,0.0);assert(has("MEMMAP diff-classes D ")&&has("MEMMAP diff-new D count=60 totalMiB=120.0 shown=48"));
        // owner is carried through the diff of two snapshots (previous keeps its own tags)
        Collector s1,s2;s1.begin();s2.begin();s1.add(R(0x80000000,0x80000000,2*M,Commit,Private));s1.tagAt(0x80000000,Stack);
        diff(s1.allocs(),s2.allocs(),d);assert(d.cls[KGone][Private][0]==2*M&&d.own[KGone][Stack]==2*M);
    }
    {   // job payload fits the sampler's slot
        static_assert(sizeof(Job)<=1024,"MEMMAP job must fit Sampler::JobBytes");
        Job j;std::snprintf(j.label,sizeof j.label,"device=1 point=periodic");assert(!deferredRequest.load());deferredRequest.store(true);assert(deferredRequest.exchange(false)&&!deferredRequest.load());
    }
    {   // task 42 review: async limits cap the lists per point; the full limits keep today's sizes
        static_assert(sizeof(Job)<=1024,"job fits the sampler slot");
        const Limits a=asyncLimits(),f;assert(a.top==12&&a.hist==12&&a.mapped==8&&a.diff==32&&f.diff==48&&f.hist==16&&f.mapped==12);
        Collector p,q;p.begin();q.begin();
        for(unsigned i=0;i<60;++i){const std::uint64_t b=0x10000000+std::uint64_t(i)*0x400000;q.add(R(b,b,(2+i)*M,Reserve,Private,0));}
        for(unsigned i=0;i<20;++i){const std::uint64_t b=0x60000000+std::uint64_t(i)*0x1000000;q.add(R(b,b,(2+i)*M,Commit,Mapped));}
        Scratch sc;auto count=[&](const char* pre){unsigned n=0;for(auto& l:lines)if(l.find(pre)==0)++n;return n;};
        lines.clear();report(sink,"A",q,&p,&name,0.0,&sc,a);
        assert(count("MEMMAP top A #")==12&&count("MEMMAP sizehist A class=reserve-only #")==12&&count("MEMMAP mapped A #")==8&&count("MEMMAP diff-new A #")==32&&has("diff-new A count=80 totalMiB=")&&has("shown=32"));
        lines.clear();report(sink,"F",q,&p,&name,0.0,&sc,f);
        assert(count("MEMMAP sizehist F class=reserve-only #")==16&&count("MEMMAP mapped F #")==12&&count("MEMMAP diff-new F #")==48);
    }
    std::puts("PASS memmap logic: grouping by AllocationBase, totals per type, top-N, new/gone/changed diff with caps, formats, capacity guard, size histogram, module totals, stack/heap tagging, diff classes.");
}
