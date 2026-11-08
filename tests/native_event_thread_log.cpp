// Actual StreamDevice options/StreamQuery lifecycle with a real DXVK EVENT.
// A test-only query wrapper delays visibility to force a deterministic expired
// Untouched trip; this is a logging regression, never a GPU performance test.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <atomic>
#include <cstdio>
#include <string>
#include "forwarders.h"
#include "stream_device.h"
using namespace NorthlightStream;
struct GateQuery final:IDirect3DQuery9 {
    IDirect3DQuery9* real;std::atomic<bool>* gate;ULONG refs=1;
    GateQuery(IDirect3DQuery9* q,std::atomic<bool>* g):real(q),gate(g){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** p)override{*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n){real->Release();delete this;}return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** p)override{return real->GetDevice(p);}
    D3DQUERYTYPE STDMETHODCALLTYPE GetType()override{return real->GetType();}
    DWORD STDMETHODCALLTYPE GetDataSize()override{return real->GetDataSize();}
    HRESULT STDMETHODCALLTYPE Issue(DWORD f)override{return real->Issue(f);}
    HRESULT STDMETHODCALLTYPE GetData(void* p,DWORD n,DWORD f)override{
        BOOL value=FALSE;const auto hr=real->GetData(&value,sizeof value,f);
        if(FAILED(hr))return hr;
        if(gate->load())return S_FALSE; // Leave caller's marker untouched.
        if(p&&n==sizeof value)std::memcpy(p,&value,sizeof value);
        return hr;
    }
};
struct GateDevice final:ForwardIDirect3DDevice9 {
    std::atomic<bool> gate{true};
    std::atomic<unsigned> clears{0};
    explicit GateDevice(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE t,IDirect3DQuery9** p)override{
        if(t!=D3DQUERYTYPE_EVENT||!p)return real->CreateQuery(t,p);
        IDirect3DQuery9* q=nullptr;const auto hr=real->CreateQuery(t,&q);if(SUCCEEDED(hr))*p=new GateQuery(q,&gate);return hr;
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s)override{
        ++clears;return real->Clear(n,r,f,c,z,s);
    }
};
int main(int argc,char** argv){
    assert(argc==2);const auto dll=LoadLibraryExA(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);assert(dll);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(dll,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="NorthlightEventLogTest";RegisterClassA(&wc);
    const auto window=CreateWindowExA(0,wc.lpszClassName,"hidden-event-log-test",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9* real=nullptr;assert(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&real)));
    GateDevice target(real);unsigned reports=0;std::string report;
    StreamDevice::Options options;options.framesAhead=2;options.frameSkip=0;options.directReplay=false;
    options.diagnostics=[](){return true;};options.log=[&](const char* line){if(std::strstr(line,"CSTREAM EVENT_THREADS ")){++reports;report=line;puts(line);}};
    const char* reason=nullptr;auto* device=StreamDevice::make(&target,api,&pp,options,&reason);assert(device);
    auto& core=device->streamCore();core.timing=true;dxvk311EventPayload.store(true);
    // Legacy callback stays null, just as in the real constructor.
    assert(!core.logLine);
    IDirect3DQuery9* query=nullptr;assert(SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_EVENT,&query)));
    for(unsigned generation=1;generation<=2;++generation){
        target.gate.store(true);assert(SUCCEEDED(query->Issue(D3DISSUE_END)));
        BOOL value=FALSE;assert(query->GetData(&value,sizeof value,D3DGETDATA_FLUSH)==S_FALSE);
        assert(reports==generation-1);
        // Subsequent queued work must survive the express query path.
        assert(SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff123456,1,0)));
        target.gate.store(false);HRESULT hr=S_FALSE;const auto start=nowNs();
        while(hr==S_FALSE){hr=query->GetData(&value,sizeof value,D3DGETDATA_FLUSH);assert(nowNs()-start<2000000000ull);}
        assert(hr==S_OK&&value==TRUE);
        assert(reports==generation); // Old null-sink wiring fails here.
        value=FALSE;assert(query->GetData(&value,sizeof value,0)==S_OK&&value==TRUE);assert(reports==generation);
        assert(report.find("generation="+std::to_string(generation))!=std::string::npos);
        assert(runTask(core,[](StreamCore&){},Cmd::SyncInit));assert(target.clears.load()==generation);
    }
    query->Release();device->Release();api->Release();DestroyWindow(window);FreeLibrary(dll);
    puts("NATIVE_EVENT_THREAD_LOG_PASS constructor_options_capturing_sink two_generations expired_then_real_TRUE cached_completion subsequent_work");
}
