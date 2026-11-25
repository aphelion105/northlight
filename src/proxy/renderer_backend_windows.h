#pragma once

static void configureDxvkCompatibility(const NorthlightBackendLoader::Inspection& info,bool disableFse=false){
    // Do not edit dxvk.conf or global launcher/Wine settings. Only this client
    // process receives the required D3D9 compatibility options. Explicit
    // DXVK_CONFIG assignments remain last and can override them for diagnosis.
    static bool applied=false;if(applied)return;applied=true;
    try {
        const DWORD needed=GetEnvironmentVariableA("DXVK_CONFIG",nullptr,0);
        std::string existing;
        if(needed){
            std::vector<char> buffer(size_t(needed)+1);
            const DWORD copied=GetEnvironmentVariableA("DXVK_CONFIG",buffer.data(),DWORD(buffer.size()));
            if(copied>=buffer.size()){logf("DXVK compatibility configuration changed concurrently; defaults not applied");return;}
            existing.assign(buffer.data(),copied);
        }
        const auto combined=NorthlightDxvkCompatibility::config(existing,disableFse);
        const BOOL set=SetEnvironmentVariableA("DXVK_CONFIG",combined.c_str());
        // DXVK 1.10.x reads only dxvk.conf/DXVK_CONFIG_FILE: the variable is then inert.
        // 0.3.189: DXVK 2.x reads cachedDynamicBuffers, >=3.0 cachedWriteOnlyBuffers; each ignores the other's key.
        logf("DXVK compatibility process defaults applied=%u vendor=1002 cachedDynamicBuffers(2.x)=True cachedWriteOnlyBuffers(>=3.0)=True descriptorHeap=False explicitInlineOverrides=%u dxvk=%s DXVK_CONFIG supported=%u",
             unsigned(set!=FALSE),unsigned(!existing.empty()),info.dxvkVersion.empty()?"unknown":info.dxvkVersion.c_str(),unsigned(info.dxvkConfigEnv));
        if(disableFse)logf("DXVK3 engine-name FSE forced=False process-config-applied=%u",unsigned(set!=FALSE));
    }catch(...){logf("DXVK compatibility defaults unavailable: allocation failure");}
}
// Win32 adapter for NorthlightBackendLoader; optional DXVK-only import redirect.
struct Win32BackendSys {
    using Module=HMODULE;
    NorthlightBackend::Kind kind;std::wstring selfPath;BY_HANDLE_FILE_INFORMATION selfId={};bool haveSelfId=false;
    bool engineNameWorkaround=false,presentWaitWorkaround=false;
    static bool fileId(const std::wstring& path,BY_HANDLE_FILE_INFORMATION& info,unsigned long& error){
        HANDLE h=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
        const bool ok=GetFileInformationByHandle(h,&info)!=FALSE;if(!ok)error=GetLastError();CloseHandle(h);
        return ok&&!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    }
    explicit Win32BackendSys(NorthlightBackend::Kind k):kind(k){
        wchar_t path[MAX_PATH*2]={};const DWORD n=GetModuleFileNameW(selfModule,path,MAX_PATH*2);
        if(n&&n<MAX_PATH*2)selfPath=path;
        unsigned long error=0;haveSelfId=!selfPath.empty()&&fileId(selfPath,selfId,error);
    }
    const std::wstring& self(){return selfPath;}
    std::wstring full(const std::wstring& path){
        wchar_t out[MAX_PATH*2]={};const DWORD n=GetFullPathNameW(path.c_str(),MAX_PATH*2,out,nullptr);
        return n&&n<MAX_PATH*2?std::wstring(out):std::wstring();
    }
    int identity(const std::wstring& path,unsigned long& error){
        BY_HANDLE_FILE_INFORMATION info={};if(!fileId(path,info,error))return -1;
        return haveSelfId&&info.dwVolumeSerialNumber==selfId.dwVolumeSerialNumber&&info.nFileIndexHigh==selfId.nFileIndexHigh&&info.nFileIndexLow==selfId.nFileIndexLow;
    }
    bool read(const std::wstring& path,std::vector<unsigned char>& bytes){
        HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size={};DWORD got=0;
        bool ok=GetFileSizeEx(h,&size)&&size.QuadPart>0&&size.QuadPart<=(64ll<<20);
        if(ok){try{bytes.resize(size_t(size.QuadPart));}catch(...){ok=false;}}
        ok=ok&&ReadFile(h,bytes.data(),DWORD(bytes.size()),&got,nullptr)&&got==bytes.size();
        CloseHandle(h);return ok;
    }
    void beforeLoad(const std::wstring&,const NorthlightBackendLoader::Inspection& info){
        // 0.3.175: DXVK_ASYNC is left to the runtime (WoWSilicon enables it). Async forks may skip a
        // draw while a pipeline compiles; the user test showed far fewer hitches, so it is not forced off.
        // Only Backend=dxvk/dxvk2 (the Windows package's DXVK: 3.1.1 default, 2.7.1 as dxvk2) gets the
        // vendor/buffer defaults; legacy (the macOS WoWSilicon DXVK) keeps today's runtime unchanged.
        const bool requested=GetPrivateProfileIntW(L"Renderer",L"DxvkEngineNameWorkaround",0,(std::wstring(rootPath)+L"northlight-renderer.ini").c_str())==1;
        const HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
        const bool wine=ntdll&&GetProcAddress(ntdll,"wine_get_version");
        engineNameWorkaround=requested&&NorthlightBackend::isPackagedDxvk(kind)&&info.dxvk&&NorthlightBackend::dxvkMajor(info.dxvkVersion)==3&&!wine;
        const bool presentWaitRequested=GetPrivateProfileIntW(L"Renderer",L"DxvkPresentWaitWorkaround",1,(std::wstring(rootPath)+L"northlight-renderer.ini").c_str())!=0;
        presentWaitWorkaround=presentWaitRequested&&NorthlightBackend::isPackagedDxvk(kind)&&info.dxvk&&info.dxvkVersion=="v3.1.1"&&!wine;
        if(requested)logf("DXVK3 engine-name requested=1 eligible=%u wine=%u backend=%s version=%s",unsigned(engineNameWorkaround),unsigned(wine),NorthlightBackend::name(kind),info.dxvkVersion.c_str());
        if(NorthlightBackend::isPackagedDxvk(kind))configureDxvkCompatibility(info,engineNameWorkaround);
    }
    HMODULE load(const std::wstring& path,unsigned long& error){
        HMODULE m=LoadLibraryW(path.c_str());error=m?0:GetLastError();
        if(m&&(engineNameWorkaround||presentWaitWorkaround))logf("DXVK3 compatibility import status=%s engine-name=%u present-wait=%u",NorthlightDxvkEngineName::install(m,&logf,engineNameWorkaround,presentWaitWorkaround),unsigned(engineNameWorkaround),unsigned(presentWaitWorkaround));
        return m;
    }
    bool isSelf(HMODULE m){return m==selfModule;}
    void* proc(HMODULE m,const char* name){FARPROC f=GetProcAddress(m,name);void* p=nullptr;static_assert(sizeof(p)==sizeof(f));std::memcpy(&p,&f,sizeof p);return p;}
    bool ownedBySelf(void* address){
        HMODULE owner=nullptr;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCWSTR>(address),&owner)&&owner==selfModule;
    }
    void release(HMODULE m){FreeLibrary(m);}
};
// 0.3.204 (task 21): "module+0xoffset" of a TestCooperativeLevel caller's return address (a diagnostic line, a few calls in a session; game thread only).
static const char* tclCallerModule(void* address){
    static char text[300];
    HMODULE m=nullptr;
    if(!address||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCWSTR>(address),&m)){
        std::snprintf(text,sizeof text,"(no module)+0x%llx",(unsigned long long)reinterpret_cast<std::uintptr_t>(address));return text;}
    wchar_t path[MAX_PATH]={};const DWORD len=GetModuleFileNameW(m,path,MAX_PATH);const wchar_t* base=path;
    for(const wchar_t* c=path;*c;++c)if(*c==L'\\'||*c==L'/')base=c+1;
    std::snprintf(text,sizeof text,"%ls+0x%llx",len?base:L"(unknown)",(unsigned long long)(reinterpret_cast<std::uintptr_t>(address)-reinterpret_cast<std::uintptr_t>(m)));
    return text;
}
static std::wstring systemDirectory(){
    wchar_t dir[MAX_PATH]={};const UINT n=GetSystemDirectoryW(dir,MAX_PATH);
    return n&&n<MAX_PATH-10?std::wstring(dir):std::wstring();
}
static std::wstring systemD3d9(){return NorthlightBackend::defaultPath(NorthlightBackend::Kind::Native,L"",systemDirectory());}
static void logAttempts(const std::vector<NorthlightBackendLoader::Attempt>& attempts){
    for(const auto& a:attempts)
        logf("BACKEND candidate=%ls result=%s error=%lu ours=%u dxvk=%s DXVK_CONFIG=%u scanned=%u",a.path.c_str(),NorthlightBackendLoader::name(a.outcome),
             a.error,unsigned(a.info.ours),a.info.dxvk?(a.info.dxvkVersion.empty()?"yes":a.info.dxvkVersion.c_str()):"no",unsigned(a.info.dxvkConfigEnv),unsigned(a.info.read));
}
// Host exe path only: nothing depends on which wow.exe runs (not read, not hashed).
static void logHostExecutable(const std::wstring& selfPath){
    wchar_t exe[MAX_PATH*2]={};const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH*2);
    logf("HOST exe=%ls module=%ls; executable is never modified",n&&n<MAX_PATH*2?exe:L"(unknown)",selfPath.c_str());
}
static std::string fileSha(const std::wstring& path){
    std::vector<unsigned char> bytes;Win32BackendSys sys(NorthlightBackend::Kind::Invalid);
    if(!sys.read(path,bytes))return "unreadable";
    NorthlightBackendLoader::Sha256 h;h.update(bytes.data(),bytes.size());return h.hex();
}
// Where this proxy sits and how the game reached it; the game-folder d3d9.dll
// (WoWSilicon's DXVK on macOS) against the backend copy that is actually used.
static void logProxyPlacement(Win32BackendSys& sys,const std::wstring& root,const NorthlightBackendLoader::Attempt& backendAttempt,bool loaded){
#ifdef NORTHLIGHT_REPLAY_STAND
    (void)backendAttempt;(void)loaded;
    logf("PROXY module=%ls root=%ls (stand module directory)",sys.selfPath.c_str(),root.c_str());
    return;
#endif
    const auto where=NorthlightBackend::location(sys.selfPath,root);
    std::vector<unsigned char> dlls;const bool listed=sys.read(root+L"dlls.txt",dlls)&&NorthlightBackend::dllsListed(std::string(dlls.begin(),dlls.end()));
    logf("PROXY module=%ls root=%ls (host exe directory) location=%s dlls.txt entry mods/d3d9.dll=%u",sys.selfPath.c_str(),root.c_str(),NorthlightBackend::name(where),unsigned(listed));
    if(where==NorthlightBackend::Location::Other)logf("PROXY WARNING: this module is neither <game>\\d3d9.dll nor <game>\\mods\\d3d9.dll; config, logs and world-cache are read from the host exe directory");
    const std::wstring game=root+L"d3d9.dll";unsigned long error=0;const int id=sys.identity(game,error);
    if(id<0){logf("GAME d3d9.dll absent (error=%lu)",error);return;}
    if(id>0){logf("GAME d3d9.dll = this proxy");return;}
    const std::string gameSha=fileSha(game);
    logf("GAME d3d9.dll sha256=%s backend sha256=%s match=%u",gameSha.c_str(),loaded?backendAttempt.info.sha256.c_str():"(none)",unsigned(loaded&&gameSha==backendAttempt.info.sha256));
    if(loaded&&gameSha!=backendAttempt.info.sha256)
        logf("GAME WARNING: the game-folder d3d9.dll differs from the backend copy (launcher update or backend switch?). The renderer keeps using %ls.",backendAttempt.path.c_str());
}
static HMODULE backend() {
    static HMODULE cached=[]() -> HMODULE {
    ensureRootPath();
#ifdef NORTHLIGHT_REPLAY_STAND
    if(!rootPath[0])return nullptr; // Never consult the original game folder or current directory.
#endif
    wchar_t config[MAX_PATH]={},value[64]={},custom[MAX_PATH]={};
    swprintf(config,MAX_PATH,L"%lsnorthlight-renderer.ini",rootPath);
    DWORD attrs=GetFileAttributesW(config),configError=GetLastError();
    bool exists=attrs!=INVALID_FILE_ATTRIBUTES;
    GetPrivateProfileStringW(L"Renderer",L"Backend",L"",value,64,config);
    GetPrivateProfileStringW(L"Renderer",L"BackendPath",L"",custom,MAX_PATH,config);
#ifdef _WIN64
    selectedBackend=value[0]?NorthlightBackend::parse(value,exists):NorthlightBackend::Kind::Native;
#else
    selectedBackend=NorthlightBackend::parse(value,exists);
#endif
    if(!exists&&configError!=ERROR_FILE_NOT_FOUND&&configError!=ERROR_PATH_NOT_FOUND)
        selectedBackend=NorthlightBackend::Kind::Invalid;
    const auto configured=selectedBackend;
#if defined(_WIN64)
    logf("NORTHLIGHT X64 D3D9: live 12340 memory and hooks disabled");
    // The client can release its adapter-probe reference before reopening
    // this DLL for rendering. Keep the proxy resident with its backend.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(selfModule),&pinned)){
        logf("X64 D3D9 proxy lifetime initialization failed error=%lu",GetLastError());
        return nullptr;
    }
#elif !defined(NORTHLIGHT_REPLAY_STAND)
    const auto artInputs=NorthlightArtInputs::check(rootPath);
    logf("ART INPUTS status=%s path=%ls",artInputs.status,artInputs.path.c_str());
    if(artInputs.blocked){
        const auto text=NorthlightArtInputs::message(artInputs);
        MessageBoxW(nullptr,text.c_str(),L"Northlight - rebuild required",MB_OK|MB_ICONWARNING|MB_SETFOREGROUND);
        ExitProcess(ERROR_BAD_ENVIRONMENT);
    }
    // The Win32 producer can read M2 batch arguments only for this Gx ABI.
#if defined(__i386__)
    if(GetModuleHandleW(nullptr)==reinterpret_cast<HMODULE>(0x400000)){
        unsigned char code[0x860];
        const auto matches=[&](uintptr_t at,size_t n,const char* hash){
            return NorthlightBlpMipGuard::read(at,code,n)&&NorthlightBlpMipGuard::digest(code,n,hash);
        };
        NorthlightM2Draw::verifiedClient=
            matches(0x6a3620,0xb8,"c293ff9e4d0288696e1f8fe025cc4277d48b9d1ad72dd6a216567a8210d4c953")&&
            matches(0x8203b0,0x266,"490ca58b8f6fd5811d957dea517e69477308067e06b77c84e63d8dee23299658")&&
            matches(0x823a5a,0x30,"24ff37d906e2f5cdef5f90fa35c123ce1ac42f7c2e6a8120e160fb0712eba277");
        NorthlightM2Draw::verifiedGroupedClient=NorthlightM2Draw::verifiedClient&&
            matches(0x820ae0,0x453,"6cd94ee64da063a67271a38a55dca3b40003886cbff9bbad0cbc20ff1ebf4ccb")&&
            matches(0x823a93,0x18,"63ad4bd96a78114038587b5d367cf3de8cce3560c9352225008fda45987ffc68")&&
            matches(0x823c90,0x18,"142c614851d9543e81679783a692b24ece86fe0087e1ebf2799e707bf8b5f000");
        NorthlightGx12340::verifiedParticleUpload=
            matches(0x68fa60,0xa7,"4d06ccc7032a4c3a06099268bee04a9facde901d9f54d695d58d4b9021ea700c")&&
            matches(0x6c48d0,0x3fa,"4e8c11f413a3f8ae9686be95cb9dd485af1092c6f8fc7bd9689e74a9233173e2")&&
            matches(0x683180,0x20,"17bbde89166811035bec07784e6183587d1d33e7ae98523d269ac1fb1ecee3bb");
        NorthlightWorldBatch12340::verifiedWmo=NorthlightM2Draw::verifiedClient&&
            matches(0x7a9380,0x860,"ce2f2f0a4cade956d5ec203d16d5505ad7bd8f1963b7f052da6d4b4ab5a58f68")&&
            matches(0x7ab760,0x356,"5df5639265efc1c65d359dec4beeaa3c6223d652fa5f87a56549b9e652cd8567")&&
            matches(0x7f9430,0x42,"6be6cd9b74dc0e1e30fec43b365ec72c1bba601fb62354a17492a62c4b9bb6d9");
        NorthlightWorldBatch12340::verifiedAdt=NorthlightM2Draw::verifiedClient&&
            matches(0x7d2d70,0x214,"63b90045d33544e023b73333e868d9a386544a9e4d904cf1fad5d984cc678612")&&
            matches(0x798a94,0x12,"80e48e395535cbbfefee97db1be64b000c07bd3e91df87b47c9d7eb7aee595d0");
        NorthlightWorldBatch12340::verifiedAdtBounds=NorthlightWorldBatch12340::verifiedAdt&&
            matches(0x7c3a10,0x128,"b4600216770897b5d141c928c10dc38dfb3fb5f481b6475654d12a34c581600b")&&
            matches(0x7c5220,0x21b,"c45dca08d19bf27998fef1a7e0055b06b29a8bf713ca44ea8b987903dd795455")&&
            matches(0x7c64b0,0x81,"c81b7c8fc4e341ab5a3ff78fc0bc4fe3eb9a39ec31493686aab7793fbb12a843")&&
            matches(0x7d02c0,0x1db,"ed31466e82168befb76c3e2a49b1bb4a42ce764fd3fdb95be57b6aba52a4a3ee")&&
            matches(0x7d3f70,0x50,"c30d85780e954ce4e33fec7974630000321523b6f7252fe1f5ea8bdc74b71c78");
    }
    logf("NATIVE_M2 client12340=%u",unsigned(NorthlightM2Draw::verifiedClient));
    logf("NATIVE_M2 groupedClient12340=%u",unsigned(NorthlightM2Draw::verifiedGroupedClient));
    logf("NATIVE_PARTICLE_UPLOAD client12340=%u",unsigned(NorthlightGx12340::verifiedParticleUpload));
    logf("NATIVE_WORLD_BATCH wmo12340=%u adt12340=%u",unsigned(NorthlightWorldBatch12340::verifiedWmo),unsigned(NorthlightWorldBatch12340::verifiedAdt));
    logf("NATIVE_ADT_BOUNDS client12340=%u",unsigned(NorthlightWorldBatch12340::verifiedAdtBounds));
#endif
    // Install before loading the backend (and its worker threads), outside
    // DllMain. This one-shot guard has no callbacks into the proxy after unload.
    const bool blpGuard=GetPrivateProfileIntW(L"Renderer",L"BlpMipGuard",1,config)!=0;
    const char* blpStatus=blpGuard?NorthlightBlpMipGuard::install():"disabled";
    logf("BLP mip guard status=%s runtime-only=1 limit=allocated-dimensions/16 files-written=0",blpStatus);
    const bool lightStay=GetPrivateProfileIntW(L"Renderer",L"NativeLightStay",1,config)!=0;
    logf("NATIVE light stay status=%s requested=%u runtime-only=1 files-written=0 restart-required=1",NorthlightNativeLightStay::install(lightStay),unsigned(lightStay));
    const bool moreLights=GetPrivateProfileIntW(L"Renderer",L"NativeMoreLights",1,config)!=0;
    logf("NATIVE more lights status=%s requested=%u max=8 runtime-only=1 files-written=0 shader-path=1",NorthlightNativeMoreLights::install(moreLights),unsigned(moreLights));
    logf("GLUE lighting selection status=%s scene-source-priority=1 receivers=background+character+pet slots=4+4 deduplicated=1 runtime-only=1",NorthlightGlueLighting::install());
#else
    logf("BLP mip guard status=disabled-imported-replay runtime-only=1 files-written=0");
#endif
    Win32BackendSys sys(configured);
    const std::wstring root(rootPath),system=systemD3d9(),overridden=NorthlightBackend::overridePath(custom,root);
    const auto candidates=NorthlightBackend::candidates(configured,overridden,root,systemDirectory());
    NorthlightBackendLoader::Result<HMODULE> result=NorthlightBackendLoader::load(sys,candidates,system);
    HMODULE module=result.module;
    memmapProcess("backend-loaded",0); /* task 42 (MEMMAP): right after the backend DLL load, before Direct3DCreate9 and any backend CreateDevice; diffs against pre-create/create */
    const auto& last=result.attempts.empty()?NorthlightBackendLoader::Attempt{}:result.attempts.back();
    // Only DXVK keeps the legacy (unchecked, no RESZ dummy draw) rules; every
    // other runtime, including the system fallback, gets the native rules.
    if(module&&(result.fallback||(configured==NorthlightBackend::Kind::Legacy&&!last.info.dxvk)))selectedBackend=NorthlightBackend::Kind::Native;
    logf("Northlight renderer 0.3.218; reference sun look (sun glow hue from native/sunHalo band, soft-shoulder glare, veil, sun-tinted haze), native sun/moon suppressed (F1b), lamps dimmed to 30 pct in direct sun, native moon02 skipped by texture identity, no game bytes in the DLL, MEMREAD self-read profile (RenderProfile), soft sun removal in shadow, jump-stable shadow anchor, geometry coverage hold with travel lead, steadier animated shadow edges (near 5x5 tent, still-camera shadow history), native blob shadows kept at BlobShadowStrength (faint texture under modulate blend), bilinear lighting history, near capture reserve for the player and companions, remembered rigid prop shadows (drawn-by-game states, windowed held), AO and bloom folded into the world composite, ground normals reject object tops, both wide samples, batched celestial terrain mask, DXVK async left to the runtime, render-thread terrain upload and rigid bookkeeping trims, moon without the horizon stall, art layer bands retimed to the sun and moon, actor prepare on a worker, trimmed prepare handoff, in-place capture constants, gate thread census, predicted snapshot lookups, word-wise memcmp, owner-thread gate elision; abandoned-frame prepare quarantine; removal smoothing on matching normals in its own pass (35/50 degree gate); per-frame draw gates; translucent depth census; early depth for translucent actors; DXVK 3.1.1 default, dxvk2 (2.7.1) by choice only, no automatic fallback; AO depth texel snap; shadow cascades follow camera zoom and collision; reduced terrain shadow reach under address-space pressure; command-stream replay thread; draw-hook lookup caches; lighter replay retire; fresh texture shadows evict only stale keeps; soft local-light cap with fades; blended GI re-publication; Forever-style rain (storm light bands, weather draw detection); moving fog clouds; GPU budget control; frames ahead and frame skipping in the command stream; replay jobs; AO denoise pass; rain mask (MRT, depth scrub), effect boundary at the UI in rain; UI boundary fallback for font mods; TestCooperativeLevel answered on the game thread; two large buffer shadows (36 MiB allowance); lamp fog glow capped once over all lamps; texture readback diagnostics, texture readbacks without a queue drain; sky light held across the weather rescale; colour-guided lighting upsample and colour-picked contact normals at wall and actor feet; actor shadow map with soft contact-hardening filter; uncached maps skip geometry builds; nearest-first actor capture (radius gate, distance cut), stride-aligned replay bulk; floating names keep the game's colour (stencil mask); instance map names with an apostrophe or a space reach the world context; lamps in view first, screen-coherent lamp batches (16-lamp passes); backend=%s path=%ls loaded=%d error=%lu",
         NorthlightBackend::name(configured),last.path.c_str(),module!=nullptr,module?0ul:(last.error?last.error:(unsigned long)ERROR_INVALID_PARAMETER));
    logAttempts(result.attempts);
    logHostExecutable(sys.selfPath);
    logf("BACKEND selected=%ls runtime=%s rules=%s BackendPath=%ls fallback=%u",module?last.path.c_str():L"(none)",
         last.info.dxvk?(last.info.dxvkVersion.empty()?"DXVK (unknown version)":last.info.dxvkVersion.c_str()):"non-DXVK",NorthlightBackend::name(selectedBackend),overridden.empty()?L"(default)":overridden.c_str(),unsigned(result.fallback));
    // 0.3.192 (DXVK3): the NOOVERWRITE read-back flag only for the backend that actually loaded and is DXVK >= 3 (see upload_lock.h).
    materialCacheBackendIdentity=last.info.sha256;
    const bool loadedDxvk=module&&!result.fallback&&last.info.dxvk;
    const bool verifiedDxvk1=loadedDxvk&&last.info.dxvkVersion=="v1.10.3";
    const bool verifiedDxvk2=loadedDxvk&&last.info.dxvkVersion=="v2.7.1";
    const bool dxvk3=loadedDxvk&&NorthlightBackend::dxvkMajor(last.info.dxvkVersion)>=3;
    // DXVK 1.10.3 has a one-FP16-step compositor difference; retain its ordinary shader.
    backendHdrBaseComposite=verifiedDxvk2||dxvk3;
    backendHdrExpandedMaterial=loadedDxvk&&last.info.dxvkVersion=="v3.1.1";
    backendTemporalZeroWeight=loadedDxvk&&last.info.dxvkVersion=="v3.1.1";
    backendPointPipelinePrewarm=verifiedDxvk1||verifiedDxvk2||dxvk3;
    materialCacheAsyncReadback=verifiedDxvk1||verifiedDxvk2||(loadedDxvk&&last.info.dxvkVersion=="v3.1.1");
    logf("BACKEND_WORK hdrBaseComposite=%u pointPipelinePrewarm=%u asyncMaterialReadback=%u expandedHdrMaterial=%u temporalZeroWeight=%u",unsigned(backendHdrBaseComposite),unsigned(backendPointPipelinePrewarm),unsigned(materialCacheAsyncReadback),unsigned(backendHdrExpandedMaterial),unsigned(backendTemporalZeroWeight));
    NorthlightStream::dxvk311EventPayload.store(module&&!result.fallback&&last.info.dxvk&&last.info.dxvkVersion=="v3.1.1",std::memory_order_relaxed);
    logf("CSTREAM EVENT wait=bounded-timer shortSpinUs=2000 queueFence=0 completion=real-GetData csPendingSpinUs=200 backendObservation=trip-final");
    NorthlightUpload::ReadBackNoOverwrite.store(module&&!result.fallback&&last.info.dxvk&&NorthlightBackend::dxvkMajor(last.info.dxvkVersion)>=3,std::memory_order_relaxed);
    logf("LOCK METER read-back lock flags readBackLock=0x%x (NOOVERWRITE only on a loaded DXVK >= 3 backend)",unsigned(NorthlightUpload::readBackLock()));
#ifndef NORTHLIGHT_SOURCE_ID
#define NORTHLIGHT_SOURCE_ID "unrecorded"
#endif
#ifndef NORTHLIGHT_BUILD_UTC
#define NORTHLIGHT_BUILD_UTC "unrecorded"
#endif
    logf("BUILD identity source=%s built=%s",NORTHLIGHT_SOURCE_ID,NORTHLIGHT_BUILD_UTC);
#ifdef NORTHLIGHT_PROFILE_BUILD
    logf("NORTHLIGHT PROFILING BUILD optimized=O2 symbols=1");
#endif
    logf("MEMORY policy terrain=demand-full/address-space-half normalMiB=32 lowMiB=16");
    if(result.fallback&&module)logf("BACKEND SELF-LOAD REFUSED: the configured backend resolves to this proxy (or another Northlight build); fell back to the system d3d9 runtime. Fix northlight-renderer.ini Backend/BackendPath.");
    else if(result.fallback)logf("BACKEND SELF-LOAD REFUSED: the configured backend resolves to this proxy (or another Northlight build) and the system d3d9 fallback FAILED too (expected under Wine with d3d9=n: the system d3d9 is builtin; the fallback is Windows-only). Fix northlight-renderer.ini Backend/BackendPath.");
    if(module&&last.info.dxvk&&!NorthlightBackend::isPackagedDxvk(configured))
        logf("DXVK compatibility defaults not applied: Backend=%s keeps the current runtime rules (no vendor 1002, no cachedDynamicBuffers/cachedWriteOnlyBuffers override)",NorthlightBackend::name(configured));
    logProxyPlacement(sys,root,last,module!=nullptr);
    logf("LOG previous session log northlight-renderer.prev.log rotation=%s error=%lu",NorthlightLogRotation::name(logRotation),(unsigned long)logRotationError);
#ifndef NORTHLIGHT_REPLAY_STAND
    if(!module)MessageBoxW(nullptr,
        L"Renderer backend could not be loaded. Check northlight-renderer.ini and northlight-renderer.log. Use a backend DLL that matches the client architecture.",
        L"Northlight renderer",MB_OK|MB_ICONERROR);
#endif
    return module;
    }();
    return cached;
}
// A backend proxy that resolved "d3d9.dll" by name calls back into this
// module. Never re-enter backend(): forward the inner call to the system runtime.
static HMODULE recursionBackend() {
    static HMODULE cached=[]() -> HMODULE {
        Win32BackendSys sys(NorthlightBackend::Kind::Native);NorthlightBackendLoader::Attempt a;
        HMODULE module=NorthlightBackendLoader::attempt(sys,systemD3d9(),a);
        logf("BACKEND RECURSION: the backend called back into this d3d9.dll; inner calls are forwarded unwrapped to %ls result=%s error=%lu",a.path.c_str(),NorthlightBackendLoader::name(a.outcome),a.error);
        return module;
    }();
    return cached;
}
template<class T> static T procedure(const char* name) {
    HMODULE module=NorthlightBackendLoader::ExportScope::reentered()?recursionBackend():backend();
    FARPROC raw=module?GetProcAddress(module,name):nullptr;
    T fn=nullptr;static_assert(sizeof(fn)==sizeof(raw));std::memcpy(&fn,&raw,sizeof(fn));return fn;
}

#ifdef NORTHLIGHT_EXTERNAL_ASSETS
static bool rendererAssetsReady(){
    static const bool resources=[](){
        std::string error;
        const bool loaded=NorthlightAssets::initialize(std::filesystem::path(rootPath)/L"northlight-assets.bin",error);
        logf("RESOURCES %s loaded=%u %s",NorthlightAssets::identity(),unsigned(loaded),error.c_str());
        if(!loaded)logf("Northlight effects disabled: reinstall the matching northlight-assets.bin; native rendering retained");
        return loaded;
    }();
    return resources;
}
#endif
