#pragma once

#include "com_ptr.hpp"
#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace NorthlightDrawProbe {

struct Selection {
    unsigned min=0, vertices=0, start=0, count=0;
    wchar_t root[1024]{};
    bool enabled=false, used=false;

    Selection() {
        char value[64];
        const DWORD length=GetEnvironmentVariableA("NORTHLIGHT_APITRACE_DRAW_PROBE",value,sizeof value);
        if(!length||length>=sizeof value)return;
        char extra;
        if(std::sscanf(value,"%u,%u,%u,%u%c",&min,&vertices,&start,&count,&extra)!=4||!vertices||!count)return;
        const DWORD size=GetEnvironmentVariableW(L"NORTHLIGHT_FRAME_PARITY_ROOT",root,sizeof root/sizeof *root);
        enabled=size>0&&size<sizeof root/sizeof *root;
    }

    std::FILE* file(const wchar_t* name)const {
        wchar_t path[1100];
        if(std::swprintf(path,sizeof path/sizeof *path,L"%ls/%ls",root,name)<0)return nullptr;
        return _wfopen(path,L"wb");
    }

    bool armed()const {
        wchar_t path[1100];
        if(std::swprintf(path,sizeof path/sizeof *path,L"%ls/arm",root)<0)return false;
        const DWORD attrs=GetFileAttributesW(path);
        return attrs!=INVALID_FILE_ATTRIBUTES&&!(attrs&FILE_ATTRIBUTE_DIRECTORY);
    }
};

struct Draw {
    com_ptr<IDirect3DSurface9> target;
    D3DSURFACE_DESC desc{};
    RECT rect{};
    Selection* selection=nullptr;
    HRESULT before=E_FAIL;

    HRESULT read(IDirect3DDevice9* device,const wchar_t* name)const {
        com_ptr<IDirect3DSurface9> active,resolve,crop,stage;
        HRESULT hr=device->GetRenderTarget(0,&active);
        if(FAILED(hr)||!active||static_cast<IDirect3DSurface9*>(active)!=static_cast<IDirect3DSurface9*>(target))return E_FAIL;
        const UINT width=rect.right-rect.left,height=rect.bottom-rect.top;
        IDirect3DSurface9* input=target;
        if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
            hr=device->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolve,nullptr);
            if(FAILED(hr))return hr;
            hr=device->StretchRect(target,nullptr,resolve,nullptr,D3DTEXF_NONE);
            if(FAILED(hr))return hr;
            input=resolve;
        }
        hr=device->CreateRenderTarget(width,height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&crop,nullptr);
        if(FAILED(hr))return hr;
        hr=device->StretchRect(input,&rect,crop,nullptr,D3DTEXF_NONE);
        if(FAILED(hr))return hr;
        hr=device->CreateOffscreenPlainSurface(width,height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr);
        if(FAILED(hr))return hr;
        hr=device->GetRenderTargetData(crop,stage);
        if(FAILED(hr))return hr;
        D3DLOCKED_RECT lock{};
        hr=stage->LockRect(&lock,nullptr,D3DLOCK_READONLY);
        if(FAILED(hr))return hr;
        bool saved=false;
        if(lock.pBits&&lock.Pitch>=int(width*4)){
            if(auto* file=selection->file(name)){
                saved=true;
                const auto* bytes=static_cast<const unsigned char*>(lock.pBits);
                for(UINT y=0;y<height&&saved;++y)saved=std::fwrite(bytes+y*lock.Pitch,4,width,file)==width;
                saved=std::fclose(file)==0&&saved;
            }
        }
        hr=stage->UnlockRect();
        return saved?hr:E_FAIL;
    }

    Draw(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT count) {
        static Selection probe;
        if(!probe.enabled||probe.used||type!=D3DPT_TRIANGLELIST||base||min!=probe.min||vertices!=probe.vertices||
           start!=probe.start||count!=probe.count||!probe.armed())return;
        com_ptr<IDirect3DSurface9> back;
        if(FAILED(device->GetRenderTarget(0,&target))||!target||
           FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))||
           static_cast<IDirect3DSurface9*>(target)!=static_cast<IDirect3DSurface9*>(back)||FAILED(target->GetDesc(&desc)))return;
        if((desc.Format!=D3DFMT_X8R8G8B8&&desc.Format!=D3DFMT_A8R8G8B8)||!desc.Width||!desc.Height||
           static_cast<unsigned long long>(desc.Width)*desc.Height*4>64u*1024u*1024u)return;
        const LONG width=std::min(768u,desc.Width),height=std::min(1024u,desc.Height);
        const LONG left=(desc.Width-width)/2,top=std::min(desc.Height-height,desc.Height*3/5-height/2);
        rect={left,top,left+width,top+height};
        selection=&probe;
        probe.used=true;
        before=read(device,L"draw-probe-before.bgra");
    }

    void finish(IDirect3DDevice9* device,unsigned call,HRESULT draw)const {
        if(!selection)return;
        const HRESULT after=SUCCEEDED(before)&&SUCCEEDED(draw)?read(device,L"draw-probe-after.bgra"):E_FAIL;
        if(auto* file=selection->file(L"draw-probe.json")){
            std::fprintf(file,"{\"call\":%u,\"rect\":[%ld,%ld,%ld,%ld],\"size\":[%u,%u],\"format\":%u,"
                "\"samples\":%u,\"sample_quality\":%lu,\"before\":\"%08lx\",\"draw\":\"%08lx\",\"after\":\"%08lx\"}\n",
                call,rect.left,rect.top,rect.right,rect.bottom,desc.Width,desc.Height,unsigned(desc.Format),
                unsigned(desc.MultiSampleType),(unsigned long)desc.MultiSampleQuality,
                (unsigned long)before,(unsigned long)draw,(unsigned long)after);
            std::fclose(file);
        }
    }
};

} // namespace NorthlightDrawProbe
