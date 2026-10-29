#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <cassert>
#include <cstdlib>
#include <new>
#include <vector>
#include "memory_footprint.h"
static int ioFailure=-1,allocationFailure=-1,ioCalls=0,openFiles=0;
static bool failIO(){++ioCalls;if(ioFailure<0)return false;if(ioFailure--==0){ioFailure=-1;return true;}return false;}
void* operator new(size_t size){if(allocationFailure>=0&&allocationFailure--==0){allocationFailure=-1;throw std::bad_alloc();}if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
namespace std {
static FILE* geometryTestTmpfile(){if(failIO())return nullptr;auto* file=::tmpfile();if(file)++openFiles;return file;}
static int geometryTestFclose(FILE* file){--openFiles;return ::fclose(file);}
static int geometryTestFseek(FILE* file,long offset,int origin){return failIO()? -1 : ::fseek(file,offset,origin);}
static size_t geometryTestFread(void* data,size_t size,size_t count,FILE* file){return failIO()? 0 : ::fread(data,size,count,file);}
static size_t geometryTestFwrite(const void* data,size_t size,size_t count,FILE* file){return failIO()? 0 : ::fwrite(data,size,count,file);}
static int geometryTestFflush(FILE* file){return failIO()? EOF : ::fflush(file);}
}
#define tmpfile geometryTestTmpfile
#define fclose geometryTestFclose
#define fseek geometryTestFseek
#define fread geometryTestFread
#define fwrite geometryTestFwrite
#define fflush geometryTestFflush
#include "temporary_geometry.h"
#undef tmpfile
#undef fclose
#undef fseek
#undef fread
#undef fwrite
#undef fflush
#include "world_mesh_pages.h"
using namespace NorthlightGI;
static bool candidate(std::vector<uint32_t>& published){
    try{
        TemporaryGeometryArray<uint32_t> values;
        constexpr unsigned Count=100000;
        for(unsigned i=0;i<Count;++i)values.push_back(i*17+3);
        for(unsigned i=0;i<Count;i+=97)values[i]^=0x12345678;
        for(unsigned i=0;i<Count;++i)assert(values[i]==((i*17+3)^(i%97?0:0x12345678)));
        values.flush();assert(values.bytes()<=300000);
        std::vector<uint32_t> next={values[0],values[50000],values[Count-1]};published.swap(next);return true;
    }catch(const std::exception&){return false;}
}
int main(){
    std::vector<uint32_t> previous={7,11,19},published=previous;
    assert(candidate(published)&&published!=previous&&!openFiles);const auto operations=ioCalls;
    unsigned refused=0;
    for(int failure=0;failure<operations;++failure){published=previous;ioFailure=failure;const bool ok=candidate(published);ioFailure=-1;if(!ok){++refused;assert(published==previous);}assert(!openFiles);}
    unsigned allocationRefused=0;
    for(int failure=0;failure<12;++failure){published=previous;allocationFailure=failure;const bool ok=candidate(published);allocationFailure=-1;if(!ok){++allocationRefused;assert(published==previous);}assert(!openFiles);}
    assert(refused&&allocationRefused);
    {TemporaryGeometryFile file;uint32_t value=123;const auto offset=file.append(&value,sizeof value);file.flush();uint32_t read=0;file.read(offset,&read,sizeof read);assert(read==value);
     for(unsigned mode=0;mode<2;++mode){bool failed=false;try{if(mode)file.write(UINT64_MAX,&value,4);else file.read(1,&read,4);}catch(const std::runtime_error&){failed=true;}assert(failed);}}
    {
        NorthlightWorldMeshPages::Plan plan;NorthlightWorldMeshPages::Page page;
        const uint32_t expected[]={17,29,41,53};page.key.vertexBytes=sizeof expected;page.key.indexBytes=0;page.spooled=true;
        plan.temporary=std::make_shared<TemporaryGeometryFile>();page.fileOffset=plan.temporary->append(expected,sizeof expected);plan.temporary->flush();
        uint32_t actual[4]={};assert(plan.copy(page,true,0,sizeof actual,actual));assert(!std::memcmp(expected,actual,sizeof actual));
        assert(!plan.copy(page,true,1,sizeof actual,actual));assert(!plan.copy(page,true,0,1,nullptr));
        for(int failure=0;failure<2;++failure){ioFailure=failure;assert(!plan.copy(page,true,0,sizeof actual,actual));ioFailure=-1;}
        page.fileOffset=UINT64_MAX;assert(!plan.copy(page,true,0,1,actual));
        page.spooled=false;assert(!plan.copy(page,true,0,1,actual));
    }
    assert(!openFiles);std::printf("TEMPORARY_GEOMETRY_PASS ioSites=%d ioRefusals=%u allocationRefusals=%u previousGenerationPreserved=1 boundedCacheKiB=256 cleanup=1\n",operations,refused,allocationRefused);
}
