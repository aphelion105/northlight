#pragma once
// 0.3.192 (MEMMAP): one-shot address-space snapshots at device lifecycle points (destroy-begin/-end, create,
// frame 300), always on (rare events). Pure part (host-testable, no Win32): Collector groups a VirtualQuery
// walk's regions by AllocationBase, picks the top allocations, diffs against the previous snapshot of the process
// (new / gone / grew-or-shrank, >= 1 MiB) and formats MEMMAP lines through a sink. Win32 part (#ifdef _WIN32):
// the walk, the process heaps, thread stacks, thread/module counts, process memory counters. Only
// kernel32/toolhelp/ntdll(NtQueryInformationThread)/psapi APIs on our own process, nothing in game memory;
// HeapSummary, K32GetProcessMemoryInfo, K32GetMappedFileNameA and NtQueryInformationThread via GetProcAddress
// (Wine may lack them; every missing piece is skipped silently or logged as unavailable). Storage is reserved
// once (a few thousand entries); nothing here allocates after the first snapshot, nothing throws to the caller.
// In-world points (world-entry, memory-deferred, periodic) run on the async memory sampler's thread via a Job.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif
namespace NorthlightMemMap {
enum State:unsigned {Free=0,Commit=1,Reserve=2};
enum Type:unsigned {NoType=0,Image=1,Mapped=2,Private=3};
struct Region {std::uint64_t base=0,allocBase=0,size=0;unsigned state=Free,type=NoType,protect=0;};
enum Owner:unsigned {Unowned=0,Stack=1,Heap=2,Module=3};
inline const char* ownerName(unsigned o){return o==Stack?"stack":o==Heap?"heap":o==Module?"module":"unknown";}
struct Alloc {std::uint64_t base=0,total=0,committed=0;unsigned type=NoType,protect=0,owner=Unowned;};
struct Totals {std::uint64_t commit[4]={},reserve[4]={},freeBytes=0,largestFree=0,regions=0,allocations=0,dropped=0;};
constexpr std::size_t MaxAllocs=4096,TopCount=12,DiffCap=48,HistLines=16,ModuleLines=20,MappedLines=12;
// Lines per list: the game-thread points (backend-loaded .. destroy) keep the full lists; the async in-world points are tighter (task 42 review).
struct Limits {std::size_t top=TopCount,hist=HistLines,modules=ModuleLines,mapped=MappedLines,diff=DiffCap;};
inline Limits asyncLimits(){Limits l;l.top=12;l.hist=12;l.modules=12;l.mapped=8;l.diff=32;return l;}
constexpr std::uint64_t DiffMinBytes=1u<<20;
inline const char* typeName(unsigned t){return t==Image?"IMAGE":t==Mapped?"MAPPED":t==Private?"PRIVATE":"NONE";}
inline double mib(std::uint64_t b){return double(b)/1048576.0;}
// A queued in-world point: label and the preformatted Northlight tally (copied by value into the sampler's job slot).
struct Job {char label[128];char tally[760];std::uint32_t requestTick;};
// Set by the geometry worker on a memory refusal/stall (one relaxed store); the Device consumes it at a frame end.
inline std::atomic<bool> deferredRequest{false};

struct DiffEntry {Alloc a;std::uint64_t before=0;};
enum Kind:unsigned {KNew=0,KGone=1,KGrown=2,KShrunk=3};
struct Diff {
    DiffEntry added[DiffCap],gone[DiffCap],changed[DiffCap];
    std::uint64_t cls[4][4][2]={};   // [kind][type][0 committed,1 reserve-only] bytes of every counted allocation (not capped)
    std::uint64_t own[4][4]={};      // [kind][owner] bytes
    unsigned nAdded=0,nGone=0,nChanged=0;                 // stored (<= DiffCap)
    unsigned addedCount=0,goneCount=0,changedCount=0;     // all
    std::uint64_t addedBytes=0,goneBytes=0,changedDelta=0,changedGrew=0;
};

// Regions arrive in address order, so one AllocationBase's regions are adjacent.
class Collector {
    std::vector<Alloc> allocs_;Totals totals_;std::uint64_t cur_=~0ull;bool curDropped_=false;
public:
    Collector(){allocs_.reserve(MaxAllocs);}
    void begin(){allocs_.clear();totals_=Totals();cur_=~0ull;curDropped_=false;}
    void add(const Region& r){
        ++totals_.regions;
        if(r.state==Free){totals_.freeBytes+=r.size;if(r.size>totals_.largestFree)totals_.largestFree=r.size;cur_=~0ull;return;}
        const unsigned t=r.type<4?r.type:0;
        (r.state==Commit?totals_.commit:totals_.reserve)[t]+=r.size;
        if(r.allocBase!=cur_||cur_==~0ull){
            cur_=r.allocBase;curDropped_=false;
            if(allocs_.size()>=allocs_.capacity()){curDropped_=true;++totals_.dropped;return;}
            Alloc a;a.base=r.allocBase;a.type=t;a.protect=r.protect;if(t==Image)a.owner=Module;allocs_.push_back(a);++totals_.allocations;
        }
        if(curDropped_)return;
        Alloc& a=allocs_.back();a.total+=r.size;if(r.state==Commit)a.committed+=r.size;
    }
    const std::vector<Alloc>& allocs()const{return allocs_;}
    const Totals& totals()const{return totals_;}
    void swap(Collector& o){allocs_.swap(o.allocs_);std::swap(totals_,o.totals_);std::swap(cur_,o.cur_);std::swap(curDropped_,o.curDropped_);}
    // The allocation containing addr (allocations are in ascending base order), or null.
    Alloc* find(std::uint64_t addr){
        std::size_t lo=0,hi=allocs_.size();
        while(lo<hi){const std::size_t mid=lo+(hi-lo)/2;if(allocs_[mid].base<=addr)lo=mid+1;else hi=mid;}
        if(!lo)return nullptr;Alloc& a=allocs_[lo-1];return addr<a.base+a.total?&a:nullptr;
    }
    // Marks the allocation containing addr; a stack beats a heap tag, nothing overwrites a stack or module. False when addr is in no collected allocation.
    bool tagAt(std::uint64_t addr,unsigned owner){
        Alloc* a=find(addr);if(!a)return false;
        if(a->owner==Unowned||(owner==Stack&&a->owner==Heap))a->owner=owner;
        return true;
    }
    // The n largest by total, descending (n <= TopCount or any caller array size); onlyType != 0 restricts to that type. Returns how many.
    std::size_t top(Alloc* out,std::size_t n,unsigned onlyType=0)const{
        std::size_t have=0;
        for(const Alloc& a:allocs_){
            if(onlyType&&a.type!=onlyType)continue;
            std::size_t i;
            if(have<n)i=have++;else if(n&&a.total>out[n-1].total)i=n-1;else continue;
            while(i>0&&out[i-1].total<a.total){out[i]=out[i-1];--i;}
            out[i]=a;
        }
        return have;
    }
};
// Both lists are in ascending base order (a VirtualQuery walk).
inline void diff(const std::vector<Alloc>& prev,const std::vector<Alloc>& cur,Diff& d){
    d=Diff();
    auto push=[](DiffEntry* list,unsigned& n,unsigned& all,const Alloc& a,std::uint64_t before){++all;if(n<DiffCap)list[n++]={a,before};};
    auto whole=[&](unsigned k,const Alloc& a){const unsigned t=a.type&3;d.cls[k][t][0]+=a.committed;d.cls[k][t][1]+=a.total-a.committed;d.own[k][a.owner&3]+=a.total;};
    auto absd=[](std::uint64_t x,std::uint64_t y){return x>y?x-y:y-x;};
    std::size_t i=0,j=0;
    while(i<prev.size()||j<cur.size()){
        if(j>=cur.size()||(i<prev.size()&&prev[i].base<cur[j].base)){
            if(prev[i].total>=DiffMinBytes){push(d.gone,d.nGone,d.goneCount,prev[i],prev[i].total);d.goneBytes+=prev[i].total;whole(KGone,prev[i]);}
            ++i;
        }else if(i>=prev.size()||cur[j].base<prev[i].base){
            if(cur[j].total>=DiffMinBytes){push(d.added,d.nAdded,d.addedCount,cur[j],0);d.addedBytes+=cur[j].total;whole(KNew,cur[j]);}
            ++j;
        }else{
            const std::uint64_t a=prev[i].total,b=cur[j].total,delta=absd(a,b);
            if(delta>=DiffMinBytes){
                push(d.changed,d.nChanged,d.changedCount,cur[j],a);if(b>a)d.changedGrew+=delta;else d.changedDelta+=delta;
                const unsigned k=b>a?KGrown:KShrunk,t=cur[j].type&3;
                d.cls[k][t][0]+=absd(cur[j].committed,prev[i].committed);d.cls[k][t][1]+=absd(b-cur[j].committed,a-prev[i].committed);d.own[k][cur[j].owner&3]+=delta;
            }
            ++i;++j;
        }
    }
}
// Reusable sort storage (reserved once, so the background points allocate nothing after the first snapshot).
struct SizeGroup {unsigned cls=0,type=0;std::uint64_t size=0,count=0,total=0,committed=0;};   // cls 0 reserve-only (committed == 0), 1 has committed pages
struct ModuleRow {char name[48]={};std::uint64_t total=0,committed=0;unsigned count=0;};
struct Scratch {std::vector<SizeGroup> hist;std::vector<ModuleRow> modules;Scratch(){hist.reserve(MaxAllocs);modules.reserve(1024);}};
inline std::uint64_t round64k(std::uint64_t s){return (s+0x8000)&~std::uint64_t(0xFFFF);}
// PRIVATE and MAPPED allocations grouped by (class, type, size rounded to 64 KiB); ordered class asc, total desc.
inline void sizeHistogram(const std::vector<Alloc>& allocs,std::vector<SizeGroup>& g){
    g.clear();
    for(const Alloc& a:allocs){
        if(a.type!=Private&&a.type!=Mapped)continue;
        SizeGroup x;x.cls=a.committed?1:0;x.type=a.type;x.size=round64k(a.total);x.count=1;x.total=a.total;x.committed=a.committed;g.push_back(x);
    }
    std::sort(g.begin(),g.end(),[](const SizeGroup& a,const SizeGroup& b){return a.cls!=b.cls?a.cls<b.cls:a.type!=b.type?a.type<b.type:a.size<b.size;});
    std::size_t out=0;
    for(std::size_t i=0;i<g.size();++i){
        if(out&&g[out-1].cls==g[i].cls&&g[out-1].type==g[i].type&&g[out-1].size==g[i].size){g[out-1].count+=g[i].count;g[out-1].total+=g[i].total;g[out-1].committed+=g[i].committed;}
        else g[out++]=g[i];
    }
    g.resize(out);
    std::sort(g.begin(),g.end(),[](const SizeGroup& a,const SizeGroup& b){return a.cls!=b.cls?a.cls<b.cls:a.total>b.total;});
}
// name(base,type,buf,size) fills buf with a module/file name or "" (may be null).
using NameFn=void(*)(std::uint64_t,unsigned,char*,std::size_t);
using Sink=void(*)(const char*);
// IMAGE allocations summed per module name (an unnamed image counts as "(unnamed)"), largest first.
inline void moduleTotals(const std::vector<Alloc>& allocs,NameFn name,std::vector<ModuleRow>& rows){
    rows.clear();
    for(const Alloc& a:allocs){
        if(a.type!=Image)continue;
        char n[48]="";if(name)name(a.base,Image,n,sizeof n);if(!n[0])std::snprintf(n,sizeof n,"(unnamed)");
        ModuleRow* row=nullptr;for(ModuleRow& r:rows)if(!std::strcmp(r.name,n)){row=&r;break;}
        if(!row){rows.emplace_back();row=&rows.back();std::snprintf(row->name,sizeof row->name,"%s",n);}
        row->total+=a.total;row->committed+=a.committed;++row->count;
    }
    std::sort(rows.begin(),rows.end(),[](const ModuleRow& a,const ModuleRow& b){return a.total>b.total;});
}
// Thread stacks: StackBase (high end) and StackLimit (lowest committed address) from the thread's NT_TIB.
struct StackSpan {std::uint64_t base=0,limit=0;};
struct StackStats {unsigned threads=0,count=0,unmatched=0;std::uint64_t total=0,committed=0,reserveOnly=0,largest=0;};
// Tags the allocation holding each span's stack (owner=stack) and sums them; two spans in one allocation count once.
inline StackStats tagStacks(Collector& c,const StackSpan* spans,std::size_t n){
    StackStats st;
    for(std::size_t i=0;i<n;++i){
        if(!spans[i].base)continue;++st.threads;
        Alloc* a=c.find(spans[i].base-1);if(!a){++st.unmatched;continue;}
        if(a->owner==Stack)continue;
        a->owner=Stack;++st.count;st.total+=a->total;st.committed+=a->committed;st.reserveOnly+=a->total-a->committed;if(a->total>st.largest)st.largest=a->total;
    }
    return st;
}
// A heap handle's AllocationBase is its first segment: tag those allocations owner=heap. Returns how many matched.
inline unsigned tagHeaps(Collector& c,const std::uint64_t* allocBases,std::size_t n){
    unsigned tagged=0;for(std::size_t i=0;i<n;++i)if(allocBases[i]&&c.tagAt(allocBases[i],Heap))++tagged;return tagged;
}
inline void formatAlloc(char* out,std::size_t size,const Alloc& a,NameFn name,std::uint64_t before=~0ull){
    char n[96]="";if(name&&(a.type==Image||a.type==Mapped))name(a.base,a.type,n,sizeof n);
    int w=std::snprintf(out,size,"base=0x%08llx sizeMiB=%.1f commitMiB=%.1f type=%s prot=0x%02x",(unsigned long long)a.base,mib(a.total),mib(a.committed),typeName(a.type),a.protect);
    if(w>0&&std::size_t(w)<size&&(a.owner==Stack||a.owner==Heap))w+=std::snprintf(out+w,size-std::size_t(w)," owner=%s",ownerName(a.owner));
    if(w>0&&std::size_t(w)<size&&before!=~0ull)w+=std::snprintf(out+w,size-std::size_t(w)," wasMiB=%.1f",mib(before));
    if(w>0&&std::size_t(w)<size&&n[0])std::snprintf(out+w,size-std::size_t(w),a.type==Image?" module=%s":" file=%s",n);
}
inline void emitList(Sink sink,const char* kind,const char* label,const DiffEntry* list,unsigned stored,unsigned all,std::uint64_t bytes,NameFn name,bool showBefore,unsigned cap=unsigned(DiffCap)){
    if(stored>cap)stored=cap;
    char line[512];
    std::snprintf(line,sizeof line,"MEMMAP %s %s count=%u totalMiB=%.1f shown=%u",kind,label,all,mib(bytes),stored);sink(line);
    for(unsigned i=0;i<stored;++i){
        char one[256];formatAlloc(one,sizeof one,list[i].a,name,showBefore?list[i].before:~0ull);
        std::snprintf(line,sizeof line,"MEMMAP %s %s #%u %s",kind,label,i,one);sink(line);
    }
}
inline void emitSizeHist(Sink sink,const char* label,const std::vector<SizeGroup>& g,std::size_t cap=HistLines){
    char line[320];
    for(unsigned cls=0;cls<2;++cls){
        const char* cname=cls?"committed":"reserve-only";
        unsigned groups=0,shown=0;std::uint64_t allocs=0,total=0;
        for(const SizeGroup& x:g)if(x.cls==cls){++groups;allocs+=x.count;total+=x.total;}
        std::snprintf(line,sizeof line,"MEMMAP sizehist %s class=%s groups=%u allocations=%llu totalMiB=%.1f (PRIVATE+MAPPED, size rounded to 64 KiB, groups >= 1 MiB)",label,cname,groups,(unsigned long long)allocs,mib(total));sink(line);
        for(const SizeGroup& x:g){
            if(x.cls!=cls||x.total<DiffMinBytes)continue;if(shown>=cap)break;
            std::snprintf(line,sizeof line,"MEMMAP sizehist %s class=%s #%u type=%s sizeMiB=%.2f count=%llu totalMiB=%.1f commitMiB=%.1f",label,cname,shown,typeName(x.type),mib(x.size),(unsigned long long)x.count,mib(x.total),mib(x.committed));sink(line);++shown;
        }
    }
}
inline void emitModules(Sink sink,const char* label,const std::vector<ModuleRow>& rows,std::size_t cap=ModuleLines){
    char line[320];std::uint64_t total=0,commit=0,rest=0;unsigned restCount=0;
    for(std::size_t i=0;i<rows.size();++i){total+=rows[i].total;commit+=rows[i].committed;if(i>=cap){rest+=rows[i].total;++restCount;}}
    const unsigned shown=unsigned(rows.size()<cap?rows.size():cap);
    std::snprintf(line,sizeof line,"MEMMAP modules %s count=%zu totalMiB=%.1f commitMiB=%.1f shown=%u remainderModules=%u remainderMiB=%.1f",label,rows.size(),mib(total),mib(commit),shown,restCount,mib(rest));sink(line);
    for(unsigned i=0;i<shown;++i){
        std::snprintf(line,sizeof line,"MEMMAP modules %s #%u name=%s sizeMiB=%.1f commitMiB=%.1f allocations=%u",label,i,rows[i].name,mib(rows[i].total),mib(rows[i].committed),rows[i].count);sink(line);
    }
}
inline void emitStacks(Sink sink,const char* label,const StackStats& s,unsigned enumerated,unsigned unreadable){
    char line[320];
    std::snprintf(line,sizeof line,"MEMMAP stacks %s count=%u reserveMiB=%.1f commitMiB=%.1f largestMiB=%.1f totalMiB=%.1f threads=%u enumerated=%u unreadable=%u unmatched=%u (reserveMiB = reserve-only part)",
        label,s.count,mib(s.reserveOnly),mib(s.committed),mib(s.largest),mib(s.total),s.threads,enumerated,unreadable,s.unmatched);sink(line);
}
inline void emitDiffClasses(Sink sink,const char* label,const Diff& d){
    char line[768];int w=std::snprintf(line,sizeof line,"MEMMAP diff-classes %s (MiB; type = commit/reserve-only, owner = total; allocations >= 1 MiB or changed by >= 1 MiB)",label);
    static const char* kinds[4]={"new","gone","grown","shrunk"};
    for(unsigned k=0;k<4&&w>0&&std::size_t(w)<sizeof line;++k){
        const std::uint64_t (&c)[4][2]=d.cls[k];
        w+=std::snprintf(line+w,sizeof line-std::size_t(w)," %s{image=%.1f/%.1f mapped=%.1f/%.1f private=%.1f/%.1f | stack=%.1f heap=%.1f module=%.1f unknown=%.1f}",kinds[k],
            mib(c[Image][0]),mib(c[Image][1]),mib(c[Mapped][0]),mib(c[Mapped][1]),mib(c[Private][0]),mib(c[Private][1]),mib(d.own[k][Stack]),mib(d.own[k][Heap]),mib(d.own[k][Module]),mib(d.own[k][Unowned]));
    }
    sink(line);
}
// Summary, top, size histogram, module, mapped and diff lines for what `cur` holds, diffed against `prev` (null on the first snapshot).
inline void report(Sink sink,const char* label,const Collector& cur,const Collector* prev,NameFn name,double walkMs,Scratch* scratch=nullptr,const Limits& lim=Limits()){
    char line[640];const Totals& t=cur.totals();
    const std::uint64_t commit=t.commit[1]+t.commit[2]+t.commit[3],reserve=t.reserve[1]+t.reserve[2]+t.reserve[3];
    std::snprintf(line,sizeof line,"MEMMAP summary %s commitMiB=%.1f reserveOnlyMiB=%.1f image=%.1f/%.1f mapped=%.1f/%.1f private=%.1f/%.1f freeMiB=%.1f largestFreeMiB=%.1f regions=%llu allocations=%llu dropped=%llu walkMs=%.2f (type=commit/reserve MiB)",
        label,mib(commit),mib(reserve),mib(t.commit[Image]),mib(t.reserve[Image]),mib(t.commit[Mapped]),mib(t.reserve[Mapped]),mib(t.commit[Private]),mib(t.reserve[Private]),
        mib(t.freeBytes),mib(t.largestFree),(unsigned long long)t.regions,(unsigned long long)t.allocations,(unsigned long long)t.dropped,walkMs);sink(line);
    Alloc best[TopCount];const std::size_t n=cur.top(best,lim.top<TopCount?lim.top:TopCount);
    for(std::size_t i=0;i<n;++i){char one[256];formatAlloc(one,sizeof one,best[i],name);std::snprintf(line,sizeof line,"MEMMAP top %s #%u %s",label,unsigned(i),one);sink(line);}
    {Scratch local;Scratch& sc=scratch?*scratch:local;
     sizeHistogram(cur.allocs(),sc.hist);emitSizeHist(sink,label,sc.hist,lim.hist);
     moduleTotals(cur.allocs(),name,sc.modules);emitModules(sink,label,sc.modules,lim.modules);}
    {Alloc maps[MappedLines];const std::size_t m=cur.top(maps,lim.mapped<MappedLines?lim.mapped:MappedLines,Mapped);
     std::uint64_t mappedTotal=0;unsigned mappedCount=0;for(const Alloc& a:cur.allocs())if(a.type==Mapped){mappedTotal+=a.total;++mappedCount;}
     std::snprintf(line,sizeof line,"MEMMAP mapped %s count=%u totalMiB=%.1f shown=%zu",label,mappedCount,mib(mappedTotal),m);sink(line);
     for(std::size_t i=0;i<m;++i){char one[256];formatAlloc(one,sizeof one,maps[i],name);std::snprintf(line,sizeof line,"MEMMAP mapped %s #%u %s",label,unsigned(i),one);sink(line);}}
    if(!prev){std::snprintf(line,sizeof line,"MEMMAP diff %s previous=none",label);sink(line);return;}
    Diff d;diff(prev->allocs(),cur.allocs(),d);
    emitList(sink,"diff-new",label,d.added,d.nAdded,d.addedCount,d.addedBytes,name,false,unsigned(lim.diff));
    emitList(sink,"diff-gone",label,d.gone,d.nGone,d.goneCount,d.goneBytes,name,false,unsigned(lim.diff));
    emitList(sink,"diff-changed",label,d.changed,d.nChanged,d.changedCount,d.changedGrew,name,true,unsigned(lim.diff));
    emitDiffClasses(sink,label,d);
    std::snprintf(line,sizeof line,"MEMMAP diff %s netMiB=%.1f (new+grew %.1f, gone+shrank %.1f; allocations >= 1 MiB or changed by >= 1 MiB)",label,
        mib(d.addedBytes+d.changedGrew)-mib(d.goneBytes+d.changedDelta),mib(d.addedBytes+d.changedGrew),mib(d.goneBytes+d.changedDelta));sink(line);
}

#ifdef _WIN32
namespace Detail {
inline Collector& current(){static Collector c;return c;}
inline Collector& previous(){static Collector c;return c;}
inline Scratch& scratch(){static Scratch s;return s;}
inline bool& havePrevious(){static bool h=false;return h;}
inline std::atomic_flag& busy(){static std::atomic_flag f=ATOMIC_FLAG_INIT;return f;}
inline double ms(LARGE_INTEGER a,LARGE_INTEGER b,LARGE_INTEGER f){return f.QuadPart?1000.0*double(b.QuadPart-a.QuadPart)/double(f.QuadPart):0.0;}
inline void baseName(const char* full,char* out,std::size_t size){
    const char* slash=std::strrchr(full,'\\');const char* fwd=std::strrchr(full,'/');if(fwd>slash)slash=fwd;
    std::snprintf(out,size,"%s",slash?slash+1:full);
}
inline void moduleName(std::uint64_t base,char* out,std::size_t size){
    out[0]=0;char full[MAX_PATH]="";
    if(!GetModuleFileNameA(reinterpret_cast<HMODULE>(std::uintptr_t(base)),full,sizeof full))return;
    baseName(full,out,size);
}
template<class F> inline F procAddress(HMODULE m,const char* name){
    FARPROC p=m?GetProcAddress(m,name):nullptr;F f=nullptr;static_assert(sizeof f==sizeof p,"function pointer size");std::memcpy(&f,&p,sizeof f);return f;
}
// K32GetMappedFileNameA (kernel32 on Windows 7+ and Wine), else psapi's GetMappedFileNameA; null when neither exists.
using MappedNameFn=DWORD(WINAPI*)(HANDLE,LPVOID,LPSTR,DWORD);
inline MappedNameFn mappedNameFn(){
    static const MappedNameFn fn=[]{
        MappedNameFn f=procAddress<MappedNameFn>(GetModuleHandleA("kernel32.dll"),"K32GetMappedFileNameA");
        if(!f){HMODULE ps=GetModuleHandleA("psapi.dll");if(!ps)ps=LoadLibraryA("psapi.dll");f=procAddress<MappedNameFn>(ps,"GetMappedFileNameA");}
        return f;}();
    return fn;
}
inline void mappedName(std::uint64_t base,char* out,std::size_t size){
    out[0]=0;const MappedNameFn fn=mappedNameFn();if(!fn)return;
    char full[MAX_PATH*2]="";const DWORD n=fn(GetCurrentProcess(),reinterpret_cast<LPVOID>(std::uintptr_t(base)),full,sizeof full);
    if(!n||n>=sizeof full)return;
    baseName(full,out,size);
}
inline void nameOf(std::uint64_t base,unsigned type,char* out,std::size_t size){
    if(type==Image)moduleName(base,out,size);else if(type==Mapped)mappedName(base,out,size);else out[0]=0;
}
struct HeapSummaryT {DWORD cb;SIZE_T cbAllocated,cbCommitted,cbReserved,cbMaxReserve;};
struct ThreadBasicT {LONG exitStatus;PVOID teb;PVOID processId,threadId;ULONG_PTR affinity;LONG priority,basePriority;};
constexpr unsigned MaxThreads=512,MaxHeaps=256,HeapLines=64;
// Stack extents of our own threads: NtQueryInformationThread(ThreadBasicInformation) -> TEB, whose NT_TIB holds StackBase/StackLimit
// (read with ReadProcessMemory on our own process, which fails instead of faulting). Unreadable threads are counted, not fatal.
inline unsigned collectStacks(const DWORD* tids,unsigned n,StackSpan* spans,unsigned& unreadable){
    using QueryFn=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    const QueryFn query=procAddress<QueryFn>(GetModuleHandleA("ntdll.dll"),"NtQueryInformationThread");
    if(!query){unreadable=n;return 0;}
    unsigned got=0;
    for(unsigned i=0;i<n;++i){
        HANDLE h=OpenThread(THREAD_QUERY_INFORMATION,FALSE,tids[i]);if(!h)h=OpenThread(0x0800/*THREAD_QUERY_LIMITED_INFORMATION*/,FALSE,tids[i]);
        if(!h){++unreadable;continue;}
        ThreadBasicT tb={};const LONG status=query(h,0/*ThreadBasicInformation*/,&tb,sizeof tb,nullptr);CloseHandle(h);
        std::uintptr_t tib[3]={};SIZE_T got2=0;
        if(status<0||!tb.teb||!ReadProcessMemory(GetCurrentProcess(),tb.teb,tib,sizeof tib,&got2)||got2!=sizeof tib||!tib[1]||tib[2]>=tib[1]){++unreadable;continue;}
        StackSpan s;s.base=tib[1];s.limit=tib[2];spans[got++]=s;
    }
    return got;
}
}
// One snapshot. `northlight` is a preformatted tally string (may be empty). Skips (logging it) when another snapshot is
// running (sampler thread vs render thread); never throws.
inline void snapshot(Sink sink,const char* label,const char* northlight,bool async=false){
    using namespace Detail;
    char line[1024];
    if(busy().test_and_set(std::memory_order_acquire)){std::snprintf(line,sizeof line,"MEMMAP skipped %s reason=busy",label);sink(line);return;}
    try{
        LARGE_INTEGER freq={},t0={},t1={},t2={};QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&t0);
        Collector& cur=current();cur.begin();
        SYSTEM_INFO si={};GetSystemInfo(&si);
        std::uint64_t at=std::uint64_t(reinterpret_cast<std::uintptr_t>(si.lpMinimumApplicationAddress));if(at<0x10000)at=0x10000;
        const std::uint64_t end=std::uint64_t(reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress))+1;
        for(unsigned guard=0;at<end&&guard<400000;++guard){
            MEMORY_BASIC_INFORMATION m={};
            if(VirtualQuery(reinterpret_cast<const void*>(std::uintptr_t(at)),&m,sizeof m)!=sizeof m)break;
            Region r;r.base=std::uint64_t(reinterpret_cast<std::uintptr_t>(m.BaseAddress));r.size=m.RegionSize;if(!r.size)break;
            r.allocBase=std::uint64_t(reinterpret_cast<std::uintptr_t>(m.AllocationBase));r.protect=m.State==MEM_FREE?0:m.Protect;
            r.state=m.State==MEM_FREE?Free:m.State==MEM_COMMIT?Commit:Reserve;
            r.type=m.Type==MEM_IMAGE?Image:m.Type==MEM_MAPPED?Mapped:m.Type==MEM_PRIVATE?Private:NoType;
            cur.add(r);at=r.base+r.size;
        }
        QueryPerformanceCounter(&t1);
        // Owners: thread stacks (toolhelp thread ids -> TEB -> NT_TIB) and heap segment bases, tagged before the report so the top and diff lists show them.
        DWORD tids[MaxThreads];StackSpan spans[MaxThreads];unsigned threads=0,modules=0,enumerated=0,unreadable=0;bool toolhelp=false;StackStats stacks;
        {const DWORD pid=GetCurrentProcessId();
         HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD|TH32CS_SNAPMODULE,pid);
         if(s!=INVALID_HANDLE_VALUE){toolhelp=true;
             THREADENTRY32 te={};te.dwSize=sizeof te;if(Thread32First(s,&te))do{if(te.th32OwnerProcessID==pid){if(threads<MaxThreads)tids[threads]=te.th32ThreadID;++threads;}}while(Thread32Next(s,&te));
             MODULEENTRY32 me={};me.dwSize=sizeof me;if(Module32First(s,&me))do{++modules;}while(Module32Next(s,&me));
             CloseHandle(s);}
         enumerated=threads<MaxThreads?threads:MaxThreads;
         if(toolhelp){const unsigned got=collectStacks(tids,enumerated,spans,unreadable);stacks=tagStacks(cur,spans,got);}}
        HANDLE heaps[MaxHeaps];const DWORD heapCount=GetProcessHeaps(MaxHeaps,heaps);const DWORD heapHave=heapCount<MaxHeaps?heapCount:MaxHeaps;
        std::uint64_t heapBases[MaxHeaps]={};
        for(DWORD i=0;i<heapHave;++i){MEMORY_BASIC_INFORMATION m={};if(VirtualQuery(heaps[i],&m,sizeof m)==sizeof m)heapBases[i]=std::uint64_t(reinterpret_cast<std::uintptr_t>(m.AllocationBase));}
        const unsigned heapTagged=tagHeaps(cur,heapBases,heapHave);
        report(sink,label,cur,havePrevious()?&previous():nullptr,&nameOf,ms(t0,t1,freq),&scratch(),async?asyncLimits():Limits());
        // Process memory counters (psapi's K32 forwarders live in kernel32 on Windows 7+ and in Wine).
        {using Fn=BOOL(WINAPI*)(HANDLE,PROCESS_MEMORY_COUNTERS*,DWORD);
         Fn fn=procAddress<Fn>(GetModuleHandleA("kernel32.dll"),"K32GetProcessMemoryInfo");
         PROCESS_MEMORY_COUNTERS pmc={};pmc.cb=sizeof pmc;
         if(fn&&fn(GetCurrentProcess(),&pmc,sizeof pmc))std::snprintf(line,sizeof line,"MEMMAP process %s workingSetMiB=%.1f peakWorkingSetMiB=%.1f pagefileMiB=%.1f peakPagefileMiB=%.1f",label,mib(pmc.WorkingSetSize),mib(pmc.PeakWorkingSetSize),mib(pmc.PagefileUsage),mib(pmc.PeakPagefileUsage));
         else std::snprintf(line,sizeof line,"MEMMAP process %s unavailable",label);
         sink(line);}
        if(toolhelp)emitStacks(sink,label,stacks,enumerated,unreadable);else{std::snprintf(line,sizeof line,"MEMMAP stacks %s unavailable",label);sink(line);}
        // Heaps. Async (sampler thread) points never take a heap lock: no heap summary, lock or walk call (a below-normal thread holding a heap lock
        // would stall the game's malloc, and walking an unserialized heap from another thread can fault); only the handles (owner=heap tags above).
        // Game-thread points call HeapSummary (no HeapWalk) under an 8 ms budget checked between heaps; heaps not reached are logged as skipped.
        if(async){
            std::snprintf(line,sizeof line,"MEMMAP heaps %s async=1 summarized=0 count=%lu handles=%lu taggedAllocations=%u (no heap lock taken off the game thread)",label,(unsigned long)heapCount,(unsigned long)heapHave,heapTagged);sink(line);
        }else{using Fn=BOOL(WINAPI*)(HANDLE,DWORD,HeapSummaryT*);
         Fn fn=procAddress<Fn>(GetModuleHandleA("kernel32.dll"),"HeapSummary");
         std::uint64_t committed=0,allocated=0,reserved=0;unsigned summarized=0,failed=0,skipped=0,printed=0,unlisted=0;const HANDLE process=GetProcessHeap();double longest=0;
         std::snprintf(line,sizeof line,"MEMMAP heaps %s count=%lu heapSummary=%d async=0",label,(unsigned long)heapCount,fn?1:0);sink(line);
         LARGE_INTEGER th={};QueryPerformanceCounter(&th);bool over=false;
         for(DWORD i=0;i<heapHave;++i){
             char tail[160]="";HeapSummaryT hs={};hs.cb=sizeof hs;
             if(fn){QueryPerformanceCounter(&t2);if(ms(th,t2,freq)>8.0)over=true;}
             if(fn&&!over){LARGE_INTEGER c0={},c1={};QueryPerformanceCounter(&c0);const BOOL okSummary=fn(heaps[i],0,&hs);QueryPerformanceCounter(&c1);const double one=ms(c0,c1,freq);if(one>longest)longest=one;
                 if(okSummary){++summarized;committed+=hs.cbCommitted;allocated+=hs.cbAllocated;reserved+=hs.cbReserved;
                 std::snprintf(tail,sizeof tail," allocatedMiB=%.2f committedMiB=%.2f reservedMiB=%.2f",mib(hs.cbAllocated),mib(hs.cbCommitted),mib(hs.cbReserved));}
                 else{++failed;std::snprintf(tail,sizeof tail," summary=failed");}}
             else if(fn){++skipped;std::snprintf(tail,sizeof tail," skipped=budget");}
             if(printed>=HeapLines){++unlisted;continue;}
             ++printed;std::snprintf(line,sizeof line,"MEMMAP heap %s #%lu handle=0x%08llx base=0x%08llx%s%s",label,(unsigned long)i,(unsigned long long)reinterpret_cast<std::uintptr_t>(heaps[i]),(unsigned long long)heapBases[i],heaps[i]==process?" default=1":"",tail);sink(line);
         }
         if(fn){std::snprintf(line,sizeof line,"MEMMAP heaps %s summarized=%u failed=%u skippedByBudget=%u unlisted=%u beyondHandleArray=%lu allocatedMiB=%.1f committedMiB=%.1f reservedMiB=%.1f budgetMs=8 longestSummaryMs=%.3f",label,summarized,failed,skipped,unlisted,(unsigned long)(heapCount-heapHave),mib(allocated),mib(committed),mib(reserved),longest);sink(line);}}
        if(toolhelp)std::snprintf(line,sizeof line,"MEMMAP threads %s threads=%u modules=%u",label,threads,modules);else std::snprintf(line,sizeof line,"MEMMAP threads %s unavailable",label);
        sink(line);
        if(northlight&&northlight[0]){std::snprintf(line,sizeof line,"MEMMAP northlight %s %s",label,northlight);sink(line);}
        QueryPerformanceCounter(&t2);
        std::snprintf(line,sizeof line,"MEMMAP done %s totalMs=%.2f",label,ms(t0,t2,freq));sink(line);
        current().swap(previous());havePrevious()=true;
    }catch(...){}
    busy().clear(std::memory_order_release);
}
// The sampler thread's job entry (async_memory_diagnostics.h Sampler::setJob): a queued in-world point.
// Runs on the sampler thread: touches only the by-value Job, the static collectors and our own process's Win32 state (no Device/StreamCore pointers).
inline void runJob(Sink sink,const void* data,std::size_t size){
    if(size!=sizeof(Job)||!sink)return;
    Job job;std::memcpy(&job,data,sizeof job);job.label[sizeof job.label-1]=0;job.tally[sizeof job.tally-1]=0;
    LARGE_INTEGER f={},a={},b={};QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
    snapshot(sink,job.label,job.tally,true);
    QueryPerformanceCounter(&b);char line[256];
    std::snprintf(line,sizeof line,"MEMMAP job %s queuedMs=%lu runMs=%.2f (sampler thread; the guard sample ran first and is next again right after)",job.label,(unsigned long)(GetTickCount()-job.requestTick),Detail::ms(a,b,f));sink(line);
}
#endif
}
