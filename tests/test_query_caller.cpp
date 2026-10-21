// Uses the command-stream SDK stub and portable preinclude, like test_command_stream.cpp.
#define main unused_stream_main
#include "test_command_stream.cpp"
#undef main
int main(){
    static unsigned calls=0;static void* namedQuery=nullptr;static std::uint64_t namedFrame=0;
    StreamDevice::Options options;options.queryCaller=[](std::uint64_t frame,void* query,void* address){CHECK(address);++calls;namedQuery=query;namedFrame=frame;};
    Rig rig(true,options);rig.core().timing=false;
    IDirect3DQuery9* q=nullptr;CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)8,&q)==S_OK);rig.sync();
    CHECK(q->Issue(D3::kIssueEnd)==S_OK);rig.sync();
    (void)q->GetData(nullptr,0,D3::kGetDataFlush);CHECK(calls==0);
    rig.core().timing=true;DWORD value=0;HRESULT hr=S_FALSE;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(hr==S_FALSE){CHECK(std::chrono::steady_clock::now()<deadline);hr=q->GetData(&value,4,D3::kGetDataFlush);std::this_thread::yield();}
    CHECK(hr==S_OK&&value==0xABCD0000u&&calls==1&&namedQuery==q&&namedFrame==rig.core().frameNo);
    for(unsigned i=0;i<100;++i)CHECK(q->GetData(&value,4,D3::kGetDataFlush)==S_OK);
    CHECK(calls==1);
    CHECK(q->Issue(D3::kIssueEnd)==S_OK);rig.sync();(void)q->GetData(nullptr,0,D3::kGetDataFlush);CHECK(calls==1);
    IDirect3DQuery9* other=nullptr;CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)8,&other)==S_OK);rig.sync();
    (void)other->GetData(nullptr,0,D3::kGetDataFlush);CHECK(calls==2&&namedQuery==other);
    other->Release();q->Release();rig.finish();checkClean();
    std::puts("PASS EVENT caller diagnostics: off until enabled, correct query/frame, one callback across polls/reissues, original completion/data, no leaks");
}
