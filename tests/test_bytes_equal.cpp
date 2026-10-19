#include "bytes_equal.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
__attribute__((noinline)) static bool baseline(const void* a,const void* b,size_t n){return !std::memcmp(a,b,n);}
__attribute__((noinline)) static bool optimized(const void* a,const void* b,size_t n){return NorthlightBytes::equal(a,b,n);}
int main(){
    assert(NorthlightBytes::equal(nullptr,nullptr,0));
    std::vector<unsigned char> a(4160),b(4160);
    for(size_t i=0;i<a.size();++i)a[i]=b[i]=static_cast<unsigned char>(i*37);
    unsigned cases=0;
    for(size_t n=0;n<=4096;n+=n<130?1:17)for(unsigned offset=0;offset<32;++offset){
        auto* x=a.data()+offset;auto* y=b.data()+31-offset;
        std::memcpy(y,x,n);assert(optimized(x,y,n));++cases;
        for(size_t i:{size_t(0),n/2,n?n-1:0})if(i<n){
            y[i]^=1;assert(!optimized(x,y,n));y[i]^=1;++cases;
        }
        assert(optimized(x,x,n));
    }
#ifdef _WIN32
    SYSTEM_INFO info{};GetSystemInfo(&info);const size_t page=info.dwPageSize;
    auto* x=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* y=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(x&&y);DWORD old;
    assert(VirtualProtect(x+page,page,PAGE_NOACCESS,&old)&&VirtualProtect(y+page,page,PAGE_NOACCESS,&old));
    std::memset(x,0x81,page);std::memset(y,0x81,page);
    for(size_t n=1;n<=page;++n){assert(optimized(x+page-n,y+page-n,n));y[page-1]^=1;assert(!optimized(x+page-n,y+page-n,n));y[page-1]^=1;++cases;}
    assert(VirtualFree(x,0,MEM_RELEASE)&&VirtualFree(y,0,MEM_RELEASE));
#endif
    double ms[2][7];volatile unsigned sink=0;
    for(unsigned r=0;r<7;++r)for(unsigned mode=0;mode<2;++mode){
        auto fn=mode?optimized:baseline;const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<80000;++i){
            const size_t n=size_t(64)<<((i/8)%7);const size_t edit=i%n;
            a[edit]^=1;std::memcpy(b.data(),a.data(),n);
            if(i%8==0)b[n-1]^=1;
            sink+=fn(a.data(),b.data(),n);
        }
        ms[mode][r]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    std::sort(ms[0],ms[0]+7);std::sort(ms[1],ms[1]+7);(void)sink;
    std::printf("{\"pass\":true,\"cases\":%u,\"old_ms\":%.6f,\"new_ms\":%.6f,\"ratio\":%.4f}\n",cases,ms[0][3],ms[1][3],ms[1][3]/ms[0][3]);
}
