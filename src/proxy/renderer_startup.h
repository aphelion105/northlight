#pragma once

static HMODULE selfModule;
static wchar_t rootPath[MAX_PATH];
static std::string settingsRecovery; // failure text of the startup journal recovery ("" = fine), logged by the first Device
// Game folder = directory of the host exe, resolved on first use (never in
// DllMain: the macOS proxy is preloaded under the loader lock from dlls.txt).
// backend() calls it first; every export reaches backend() before it logs.
// The offline stand uses its own module directory: a retracer can virtualize
// the executable name to the ORIGINAL client, including its installed folder.
static void ensureRootPath(){
    static const bool resolved=[]{
        wchar_t self[MAX_PATH]={};const DWORD m=GetModuleFileNameW(selfModule,self,MAX_PATH);
#ifdef NORTHLIGHT_REPLAY_STAND
        const std::wstring root=m&&m<MAX_PATH?NorthlightBackend::directory(self):L"";
#else
        wchar_t exe[MAX_PATH]={};const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH);
        const std::wstring root=NorthlightBackend::gameRoot(n&&n<MAX_PATH?exe:L"",m&&m<MAX_PATH?self:L"");
#endif
        if(root.size()<MAX_PATH)wcscpy(rootPath,root.c_str());
        // Before backend selection, MemorySwitches or any loader reads an INI: finish a settings commit a crash interrupted (one stat when none).
        if(rootPath[0])settingsRecovery=NorthlightSettings::Store::recoverAtStartup(std::filesystem::path(rootPath));
        if(rootPath[0]&&settingsRecovery.empty())settingsRecovery=NorthlightSettings::migrateLightLimits(std::filesystem::path(rootPath));
        if(rootPath[0]&&settingsRecovery.empty())settingsRecovery=NorthlightSettings::migrateHdr(std::filesystem::path(rootPath));
        return true;
    }();
    (void)resolved;
}
