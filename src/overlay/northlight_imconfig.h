#pragma once
// Dear ImGui build configuration for the settings overlay (IMGUI_USER_CONFIG, set by scripts/build_renderer.py for
// every TU). The vendored sources in src/imgui/ stay unmodified. No Win32 helpers (clipboard, IME, shell: no extra
// import libraries), no file I/O (no imgui.ini or log in the game folder, fonts are loaded from memory), no demo or
// debug windows. A failed ImGui assertion throws (settings_overlay.cpp): the overlay turns itself off for the session.
#define IMGUI_DISABLE_WIN32_FUNCTIONS
#define IMGUI_DISABLE_FILE_FUNCTIONS
#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_DISABLE_DEBUG_TOOLS
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
void northlightImGuiAssert(const char* expression,const char* file,int line);
#define IM_ASSERT(_EXPR) ((_EXPR)?(void)0:northlightImGuiAssert(#_EXPR,__FILE__,__LINE__))
