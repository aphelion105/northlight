#pragma once
#include "utf8_path.h"
#include "diagnostic_files.h"
#include "diagnostic_readback.h"
#include <d3d9.h>
#include <algorithm>
#include <cstdio>
#include <string>
// Explicit F12 shadow view only. Read actual GPU products without changing the
// lighting state; errors are diagnostic failures, never renderer-disable events.
namespace NorthlightWorldDiagnostics {
inline std::string directory(const std::string& root){
    const std::string parent=root+"northlight-diagnostics";CreateDirectoryW(std::filesystem::u8path(parent).c_str(),nullptr);
    const std::string prefix=parent+"/session-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount());
    for(unsigned i=0;i<64;++i){const auto path=prefix+"-"+std::to_string(i);if(CreateDirectoryW(std::filesystem::u8path(path).c_str(),nullptr))return NorthlightDiagnosticFiles::enable(path)?path:std::string{};if(GetLastError()!=ERROR_ALREADY_EXISTS)break;}
    return {};
}
// An absent pass has a real zero-filled image, without reading or clearing an
// old GPU target. Capture metadata distinguishes skipped work from GPU failure.
inline bool empty(const std::string& directory,const char* name,unsigned capture,UINT width,UINT height,D3DFORMAT format){
    const unsigned stride=format==D3DFMT_A16B16G16R16F?8:4;
    if(!width||!height||uint64_t(width)*height*stride>64*1024*1024)return false;
    const auto path=directory+"/capture-"+std::to_string(capture)+"-"+name+".fgr";
    return NorthlightDiagnosticFiles::image(path,width,height,unsigned(format),stride,nullptr,0);
}
// Paired camera captures need the original cached projection too: its depth
// origin and XY margin can differ from the current working shadow map.
inline bool shadowFrame(const std::string& directory,unsigned capture,int source,int cascade,
                        const float* cached,const float* working,long x,long y,float dz,
                        uint64_t geometry,uint64_t signature,const char* reason){
    char suffix[128];std::snprintf(suffix,sizeof suffix,"/capture-%u-s%d-c%d-frame.json",capture,source,cascade);
    FILE* f=NorthlightFiles::open((directory+suffix).c_str(),"wb");if(!f)return false;
    std::fprintf(f,"{\"source\":%d,\"cascade\":%d,\"geometry\":%llu,\"signature\":\"%llu\",\"reason\":\"%s\",\"offset\":[%ld,%ld,%.9g],\"cached\":[",source,cascade,(unsigned long long)geometry,(unsigned long long)signature,reason?reason:"reuse",x,y,dz);
    for(unsigned i=0;i<16;++i)std::fprintf(f,"%s%.9g",i?",":"",cached[i]);
    std::fprintf(f,"],\"upstream_retained\":true,\"working\":[");for(unsigned i=0;i<16;++i)std::fprintf(f,"%s%.9g",i?",":"",working[i]);
    std::fprintf(f,"]}\n");const bool ok=std::ferror(f)==0;return std::fclose(f)==0&&ok;
}
inline bool dump(IDirect3DDevice9* device,IDirect3DSurface9* source,
                 const std::string& directory,const char* name,unsigned capture){
    if(!source)return false;
    D3DSURFACE_DESC desc={};HRESULT hr=source->GetDesc(&desc);
    unsigned bytes=desc.Format==D3DFMT_A16B16G16R16F||desc.Format==D3DFMT_G32R32F?8:
        (desc.Format==D3DFMT_R32F||desc.Format==D3DFMT_A8R8G8B8||desc.Format==D3DFMT_X8R8G8B8?4:0);
    if(FAILED(hr)||!bytes||!desc.Width||!desc.Height||uint64_t(desc.Width)*desc.Height*bytes>64*1024*1024)return false;
    if(uint64_t(desc.Width)*desc.Height*bytes>NorthlightDiagnosticReadback::DirectLimit&&desc.MultiSampleType==D3DMULTISAMPLE_NONE){
        hr=NorthlightDiagnosticReadback::strips(device,source,desc,bytes,directory+"/capture-"+std::to_string(capture)+"-"+name+".fgr");
        logf("WORLD GPU diagnostic capture=%u buffer=%s size=%ux%u format=%u saved=%d HRESULT=%08lx",capture,name,desc.Width,desc.Height,unsigned(desc.Format),SUCCEEDED(hr),(unsigned long)hr);
        return SUCCEEDED(hr);
    }
    IDirect3DSurface9* staging=nullptr;
    hr=device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging,nullptr);
    if(SUCCEEDED(hr))hr=device->GetRenderTargetData(source,staging);
    D3DLOCKED_RECT lock={};bool locked=false;
    if(SUCCEEDED(hr)){hr=staging->LockRect(&lock,nullptr,D3DLOCK_READONLY);locked=SUCCEEDED(hr);}
    bool ok=false;
    if(locked&&lock.pBits&&lock.Pitch>=INT(desc.Width*bytes)){
        char suffix[128];std::snprintf(suffix,sizeof suffix,"/capture-%u-%s.fgr",capture,name);
        ok=NorthlightDiagnosticFiles::image(directory+suffix,desc.Width,desc.Height,unsigned(desc.Format),bytes,lock.pBits,size_t(lock.Pitch));
    }
    if(locked)staging->UnlockRect();if(staging)staging->Release();
    logf("WORLD GPU diagnostic capture=%u buffer=%s size=%ux%u format=%u saved=%d HRESULT=%08lx",capture,name,desc.Width,desc.Height,unsigned(desc.Format),ok,(unsigned long)hr);
    return ok;
}
inline bool product(IDirect3DDevice9* device,IDirect3DSurface9* surface,const std::string& directory,const char* name,unsigned capture,
                    bool current,UINT width,UINT height,D3DFORMAT format,unsigned frame){
    const bool ok=current?dump(device,surface,directory,name,capture):empty(directory,name,capture,width,height,format);
    const auto path=directory+"/capture-"+std::to_string(capture)+"-products.jsonl";
    FILE* f=NorthlightFiles::open(path.c_str(),"ab");if(!f)return false;
    std::fprintf(f,"{\"product\":\"%s\",\"renderer_frame\":%u,\"state\":\"%s\",\"saved\":%s,\"zero_filled\":%s,\"fps_eligible\":false}\n",name,frame,current?(ok?"current":"unavailable"):"skipped",ok?"true":"false",current?"false":"true");
    return std::fclose(f)==0&&ok;
}
inline bool textureProduct(IDirect3DDevice9* device,IDirect3DTexture9* texture,const std::string& directory,const char* name,unsigned capture,
                           bool current,UINT width,UINT height,D3DFORMAT format,unsigned frame){
    IDirect3DSurface9* level=nullptr;if(current&&texture)texture->GetSurfaceLevel(0,&level);
    const bool ok=product(device,level,directory,name,capture,current,width,height,format,frame);if(level)level->Release();return ok;
}
}
