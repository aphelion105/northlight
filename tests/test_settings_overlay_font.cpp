// The settings overlay's embedded font (src/generated/overlay_font_roboto.h) loads through ImGui's own decompressor and
// rasterizer (built natively with the overlay's ImGui config; tests/test_settings_overlay.py).
#include <cstdio>
#include "imgui.h"
#include "overlay_font_roboto.h"

static int asserts=0;
void northlightImGuiAssert(const char* expression,const char* file,int line){++asserts;std::fprintf(stderr,"ImGui assertion %s (%s:%d)\n",expression,file,line);}

int main(){
    ImGui::CreateContext();
    ImFontAtlas* atlas=ImGui::GetIO().Fonts;
    static const ImWchar ranges[]={0x0020,0x00FF,0};
    ImFontConfig config;config.OversampleH=2;config.OversampleV=1;
    ImFont* font=atlas->AddFontFromMemoryCompressedTTF(NorthlightOverlayFont::RobotoMediumCompressed,int(NorthlightOverlayFont::RobotoMediumCompressedSize),32.f,&config,ranges);
    unsigned char* pixels=nullptr;int width=0,height=0;
    atlas->GetTexDataAsRGBA32(&pixels,&width,&height);
    const ImFontGlyph* a=font?font->FindGlyphNoFallback('A'):nullptr;
    const ImFontGlyph* e=font?font->FindGlyphNoFallback(0xE4):nullptr;   // a-umlaut: the Latin-1 range is in the font
    const bool ok=font&&atlas->IsBuilt()&&pixels&&width>0&&height>0&&a&&a->X1>a->X0&&e&&asserts==0;
    ImGui::DestroyContext();
    if(!ok){std::fprintf(stderr,"embedded font failed: font=%p glyphA=%p glyphAe=%p asserts=%d\n",static_cast<void*>(font),static_cast<const void*>(a),static_cast<const void*>(e),asserts);return 1;}
    std::printf("embedded Roboto: atlas %dx%d OK\n",width,height);
    return 0;
}
