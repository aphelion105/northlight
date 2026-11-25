#pragma once

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <utility>

namespace NorthlightHdr {

// Windows SDK d3d9on12.h is absent from the MinGW headers.
struct On12Override {
    BOOL enable;
    IUnknown* device;
    IUnknown* queues[2];
    UINT queueCount,nodeMask;
};
using Create9On12=IDirect3D9*(WINAPI*)(UINT,On12Override*,UINT);
inline constexpr GUID On12Iid={0xe7fda234,0xb589,0x4049,{0x94,0x0d,0x88,0x78,0x97,0x75,0x31,0xc8}};
struct On12Device : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetD3D12Device(REFIID,void**)=0;
    virtual HRESULT STDMETHODCALLTYPE UnwrapUnderlyingResource(IDirect3DResource9*,ID3D12CommandQueue*,REFIID,void**)=0;
    virtual HRESULT STDMETHODCALLTYPE ReturnUnderlyingResource(IDirect3DResource9*,UINT,UINT64*,ID3D12Fence**)=0;
};

class NativeOutputChain {
    Microsoft::WRL::ComPtr<IDirect3DDevice9> device;
    Microsoft::WRL::ComPtr<IDirect3DSurface9> renderTarget,readback;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> chain;
    static constexpr UINT BufferCount=2;
    struct CopyFrame {
        Microsoft::WRL::ComPtr<ID3D12Resource> back;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        UINT64 done=0;
    };
    Microsoft::WRL::ComPtr<On12Device> on12;
    Microsoft::WRL::ComPtr<IDirect3DTexture9> renderTexture;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    CopyFrame copies[BufferCount];
    HANDLE fenceEvent=nullptr;
    UINT64 sequence=0;
    HWND window=nullptr;
    UINT interval=1;

    HRESULT wait(UINT64 value,bool immediate=false){
        if(!value)return S_OK;
        const UINT64 complete=fence->GetCompletedValue();
        if(complete==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
        if(complete>=value)return S_OK;
        if(immediate)return DXGI_ERROR_WAS_STILL_DRAWING;
        const HRESULT hr=fence->SetEventOnCompletion(value,fenceEvent);
        if(FAILED(hr))return hr;
        return WaitForSingleObject(fenceEvent,INFINITE)==WAIT_OBJECT_0?S_OK:E_FAIL;
    }

    HRESULT createGpu(IDirect3DDevice9* source,const D3DSURFACE_DESC& desc){
        Microsoft::WRL::ComPtr<ID3D12Device> device12;
        HRESULT hr=on12->GetD3D12Device(IID_PPV_ARGS(&device12));
        if(FAILED(hr))return hr;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        if(FAILED(hr=device12->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)))||
           FAILED(hr=device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return hr;
        fenceEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!fenceEvent)return HRESULT_FROM_WIN32(GetLastError());
        for(auto& copy:copies){
            if(FAILED(hr=device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&copy.allocator))))return hr;
        }
        if(FAILED(hr=device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,copies[0].allocator.Get(),
            nullptr,IID_PPV_ARGS(&commands)))||FAILED(hr=commands->Close()))return hr;
        if(FAILED(hr=source->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,
            D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&renderTexture,nullptr)))return hr;
        return renderTexture->GetSurfaceLevel(0,&renderTarget);
    }

    HRESULT transferGpu(bool immediate){
        auto& copy=copies[chain->GetCurrentBackBufferIndex()];
        HRESULT hr=wait(copy.done,immediate);
        if(FAILED(hr)||FAILED(hr=copy.allocator->Reset())||
           FAILED(hr=commands->Reset(copy.allocator.Get(),nullptr)))return hr;

        Microsoft::WRL::ComPtr<ID3D12Resource> input;
        hr=on12->UnwrapUnderlyingResource(renderTexture.Get(),queue.Get(),IID_PPV_ARGS(&input));
        if(FAILED(hr)){commands->Close();return hr;}

        D3D12_RESOURCE_BARRIER barriers[2]{};
        auto transition=[](D3D12_RESOURCE_BARRIER& barrier,ID3D12Resource* resource,D3D12_RESOURCE_STATES copyState){
            barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,copyState};
        };
        transition(barriers[0],input.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(barriers[1],copy.back.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        commands->ResourceBarrier(2,barriers);
        commands->CopyResource(copy.back.Get(),input.Get());
        for(auto& barrier:barriers)std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        commands->ResourceBarrier(2,barriers);
        hr=commands->Close();
        if(SUCCEEDED(hr)){
            ID3D12CommandList* lists[]={commands.Get()};
            queue->ExecuteCommandLists(1,lists);
            hr=queue->Signal(fence.Get(),sequence+1);
            if(SUCCEEDED(hr))copy.done=++sequence;
        }
        ID3D12Fence* complete=fence.Get();
        const HRESULT returned=SUCCEEDED(hr)
            ?on12->ReturnUnderlyingResource(renderTexture.Get(),1,&copy.done,&complete)
            :on12->ReturnUnderlyingResource(renderTexture.Get(),0,nullptr,nullptr);
        return FAILED(hr)?hr:returned;
    }

public:
    ~NativeOutputChain(){
        if(fenceEvent){wait(sequence);CloseHandle(fenceEvent);}
    }
    bool gpuTransfer()const{return on12!=nullptr;}

    HRESULT create(IDirect3DDevice9* source, const D3DPRESENT_PARAMETERS& pp, IDirect3DSurface9** back){
        using Microsoft::WRL::ComPtr;
        ComPtr<IDXGIFactory4> factory;
        HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if(FAILED(hr))return hr;

        ComPtr<IDirect3DSurface9> logicalBack;
        D3DSURFACE_DESC desc{};
        if(FAILED(hr=source->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&logicalBack))||
           FAILED(hr=logicalBack->GetDesc(&desc)))return hr;
        source->QueryInterface(On12Iid,reinterpret_cast<void**>(on12.GetAddressOf()));
        ComPtr<ID3D11Device> device11;
        IUnknown* swapOwner=nullptr;
        if(on12){
            if(FAILED(hr=createGpu(source,desc)))return hr;
            swapOwner=queue.Get();
        }else{
            // Older systems can still present classic D3D9 through system memory.
            if(FAILED(hr=source->CreateRenderTarget(desc.Width,desc.Height,D3DFMT_A16B16G16R16F,
                D3DMULTISAMPLE_NONE,0,FALSE,&renderTarget,nullptr))||
               FAILED(hr=source->CreateOffscreenPlainSurface(desc.Width,desc.Height,D3DFMT_A16B16G16R16F,
                D3DPOOL_SYSTEMMEM,&readback,nullptr))||
               FAILED(hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
                nullptr,0,D3D11_SDK_VERSION,&device11,nullptr,&context)))return hr;
            device=source;
            swapOwner=device11.Get();
        }

        DXGI_SWAP_CHAIN_DESC1 swap{};
        swap.Width=desc.Width;swap.Height=desc.Height;
        swap.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        swap.SampleDesc.Count=1;swap.BufferCount=BufferCount;
        swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
        window=pp.hDeviceWindow;
        interval=pp.PresentationInterval==D3DPRESENT_INTERVAL_IMMEDIATE?0:1;
        ComPtr<IDXGISwapChain1> base;
        if(FAILED(hr=factory->CreateSwapChainForHwnd(swapOwner,window,&swap,nullptr,nullptr,&base))||
           FAILED(hr=base.As(&chain)))return hr;
        if(on12){
            for(UINT i=0;i<BufferCount;++i){
                if(FAILED(hr=chain->GetBuffer(i,IID_PPV_ARGS(&copies[i].back))))return hr;
            }
        }
        factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
        constexpr auto colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        UINT supported=0;
        if(FAILED(hr=chain->CheckColorSpaceSupport(colorSpace,&supported)))return hr;
        if(!(supported&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))return D3DERR_NOTAVAILABLE;
        if(FAILED(hr=chain->SetColorSpace1(colorSpace)))return hr;
        return renderTarget.CopyTo(back);
    }

    HRESULT transferCpu(){
        HRESULT hr=device->GetRenderTargetData(renderTarget.Get(),readback.Get());
        if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
        if(FAILED(hr=chain->GetBuffer(0,IID_PPV_ARGS(&back))))return hr;
        D3DLOCKED_RECT pixels{};
        if(FAILED(hr=readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY)))return hr;
        context->UpdateSubresource(back.Get(),0,nullptr,pixels.pBits,UINT(pixels.Pitch),0);
        return readback->UnlockRect();
    }

    HRESULT present(const RECT* source,const RECT* destination,HWND overrideWindow,const RGNDATA*,DWORD flags){
        if(source||destination||(overrideWindow&&overrideWindow!=window))return D3DERR_INVALIDCALL;
        const bool immediate=(flags&D3DPRESENT_DONOTWAIT)!=0;
        const HRESULT hr=on12?transferGpu(immediate):transferCpu();
        if(FAILED(hr))return hr;
        return chain->Present(interval,immediate?DXGI_PRESENT_DO_NOT_WAIT:0);
    }
};
}
