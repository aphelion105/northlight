#define NORTHLIGHT_REPLAY_STAND 1
#include "world_context.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
using namespace NorthlightStream;
static void fill(GameSnapshot& s,std::uint64_t draw,float x){
    s.clear();s.triggerKind=2;s.triggerDraw=draw;
    const std::uint32_t frame=0x18000000,camera=0x18010000;
    const float xyz[3]={x,25,30};char map[64]="Azeroth";
    assert(s.record(0xb7436c,&frame,4,true));assert(s.record(frame+0x7e20,&camera,4,true));
    assert(s.record(camera+8,xyz,sizeof xyz,true));assert(s.record(0xce06d0,map,sizeof map,true));
    assert(s.record(0x18020000,nullptr,4,false));
}
int main(int argc,char** argv){
    assert(argc==2);std::ifstream input(argv[1],std::ios::binary);assert(input);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),{}};
    ClientImage::View image;ClientImage::Error error;assert(image.open(bytes.data(),bytes.size(),error));
    GameSnapshot snapshot;fill(snapshot,100,10);
    StandInputs::Session session(image);assert(session.bind(snapshot,100,2));
    float camera[3]={};char map[64]={};std::uint32_t untouched=0x12345678,host=0;
    assert(!NorthlightWorldContext::readSelfLive(reinterpret_cast<std::uintptr_t>(&untouched),&host,4)&&host==0);
    assert(!NorthlightWorldContext::readSelf(0xb7436c,&host,4));
    {
        StandInputs::Scope scope(session);assert(scope.entered());
        assert(NorthlightWorldContext::supportedClient());
        for(unsigned repeat=0;repeat<8;++repeat){
            assert(NorthlightWorldContext::readMapAndCamera(map,camera));
            assert(!std::strcmp(map,"Azeroth")&&camera[0]==10&&camera[1]==25&&camera[2]==30);
        }
        assert(session.dynamicReads==32&&session.staticReads>=2&&!session.missingReads);
        assert(!NorthlightWorldContext::readSelf(0x18020000,&host,4));
        assert(!NorthlightWorldContext::readSelf(0x18020000,&host,4));
        assert(session.recordedFailures==2&&session.valid());
        StandInputs::Scope nested(session);assert(!nested.entered()&&StandInputs::active==&session);
        fill(snapshot,101,11);assert(session.bind(snapshot,101,2));
        assert(NorthlightWorldContext::readMapAndCamera(map,camera)&&camera[0]==11);
    }
    assert(!StandInputs::active&&!NorthlightWorldContext::readSelf(0xb7436c,&host,4));
    {StandInputs::Scope again(session);assert(again.entered());}
    StandInputs::Session replacement(image);StandInputs::Scope refused(replacement);assert(!refused.entered());
    StandInputs::Session wrong(image);assert(!wrong.bind(snapshot,102,2)&&!wrong.valid());
    StandInputs::Session missing(image);assert(missing.bind(snapshot,101,2));
    assert(!missing.read(0x18030000,&host,4)&&!missing.valid()&&missing.missingReads==1);
    assert(!missing.read(0xb7436c,&host,4));
    StandInputs::Session badOrder(image);assert(badOrder.bind(snapshot,101,2));
    assert(!badOrder.bind(snapshot,101,2)&&!badOrder.valid());
    std::puts("PASS actual client signature bytes + authored runtime snapshot; FIFO/last-copy, scope, missing-read refusal; no host reads/client execution; not a game-scene/FPS result");
}
