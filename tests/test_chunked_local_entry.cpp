#include "geometry_memory.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace NorthlightGeometryMemory;
static unsigned GetTickCount(){return 1000;}
int main(){
 for(unsigned i=0;i<7;++i){
    EntryLift chunkLift;auto entryBudget=buildBudget(64*MiB);Sample memory{600*MiB,120*MiB,true};
    if(i==1)memory.largest=40*MiB;
    if(i==2)memory.available=511*MiB;
    if(i==3)memory.largest=32*MiB;
    if(i==4)memory.valid=false;
    if(i==5){memory.largest=40*MiB;chunkLift.refused(600*MiB,384*MiB,364*MiB,999,0);}
    if(i==6){memory.largest=40*MiB;chunkLift.refused(600*MiB,384*MiB,384*MiB,999,50*MiB);}
    bool chunkedLocal=false;unsigned samples=0;auto workerMemory=[&](Budget budget){++samples;assert(budget.valid);return memory;};
#include "chunked_entry.inl"
    if(i==0){assert(!chunkedLocal&&!samples&&admits(memory,entryBudget));}
    else{assert(chunkedLocal&&samples==1);assert(admits(memory,entryBudget)==(i==1));}
    assert(entryBudget.available>=512*MiB&&entryBudget.largest>=33*MiB);
 }
 for(unsigned i=0;i<5;++i){
    bool localBuilt=i==4,localDeferred=false;Sample localSample;uint64_t localRequired=0,localLargest=0;std::string error;
    if(i==0)error="Incremental FGS3 model: Cannot allocate FGS2 geometry";
    if(i==1)error="Incremental FGS3 model: Geometry allocation deferred";
    if(i==2)error="Invalid model";
    if(i==3){error="Cannot allocate geometry BVH";localDeferred=true;}
    if(i==4)error="Cannot allocate geometry BVH";
    auto workerMemory=[&](Budget){return Sample{600*MiB,40*MiB,true};};
#include "chunked_failure.inl"
    noteLocalAllocationFailure();assert(localDeferred==(i<2||i==3));if(i<2)assert(localSample.valid&&localRequired&&localLargest);
 }
 std::puts("PASS chunked entry: 512 MiB aggregate and 32 MiB contiguous margin unchanged; lifted, invalid and insufficient samples refused");
}
