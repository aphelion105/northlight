#pragma once
// 0.3.212 (task 32): stencil mask for floating unit names. The game draws names (world text) in the world pass with the UI-hash vertex shader and a perspective projection, before the UI boundary; WorldComposite
// then relights, shadows, fogs and hazes them like the surface behind them. Marking happens inside the game's own name draw: stencil is enabled (ALWAYS, REPLACE 0x80 on pass) around it, so exactly the pixels the
// game draws (alpha test, depth test) get the bit. Consecutive names form a run that keeps the stencil states set (the run ends before any draw that is not a name, a Clear, EndScene, the effects, the frame end).
// A game alpha test that is off is turned on for the draw with GREATER 0, which drops only alpha-0 pixels (invisible under SRCALPHA blending; with the game's Z write on they no longer write depth either). Unmarking: after the frame's first mark, a later depth-final draw
// (Z-writing opaque, or water Z) replaces the bit with 0 where it lands, so solid geometry drawn over a name is composited normally. Composite: WorldComposite draws with the world depth-stencil bound and
// STENCILFUNC NOTEQUAL on the bit, so a marked pixel keeps the game's own colour. Marks must not outlive the frame: the first full world depth Clear of the frame also clears the stencil; a frame whose first mark has
// no such clear gets one stencil-only Clear. The mod never touches stencil when the game uses it (guard) or the world depth has none. Portable: no device calls; the state ids are the D3DRS_* values (renderer.cpp
// static_asserts them), applied by the callers with read-modify-restore.
#include "translucent_depth.h"
namespace NorthlightNameStencil {
constexpr unsigned long kBit=0x80;
// D3DRENDERSTATETYPE values
enum State : unsigned {ZEnable=7,ZWriteEnable=14,AlphaTestEnable=15,AlphaRef=24,AlphaFunc=25,StencilEnable=52,StencilFail=53,StencilZFail=54,
    StencilPass=55,StencilFunc=56,StencilRef=57,StencilMask=58,StencilWriteMask=59,TwoSidedStencilMode=185};
// D3DBLEND / D3DCMPFUNC / D3DSTENCILOP values
constexpr unsigned long kBlendSrcAlpha=5,kBlendSrcColor=3;
constexpr unsigned long kCmpGreater=5,kCmpNotEqual=6,kCmpAlways=8,kOpKeep=1,kOpReplace=3;
struct Set {unsigned state;unsigned long value;};
// The name draw itself: stencil REPLACE with ref 0x80 where the game's alpha and depth tests pass; the set changes no other state of the game's (kAlphaGuard below is the one exception). The states below have no effect while STENCILENABLE is off (the guard
// established that the game never turns it on), so they are set once and left; a name run only toggles STENCILENABLE. kMarkProbe are the two that tell the set is still in place.
inline constexpr Set kMark[]={{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,kBit},{StencilWriteMask,kBit},{TwoSidedStencilMode,0}};
inline constexpr Set kMarkProbe[]={{StencilPass,kOpReplace},{StencilRef,kBit}};
// Around a name draw whose game alpha test is off: GREATER 0 drops only alpha-0 pixels, which SRCALPHA blending leaves invisible. A deliberate change of the game's draw: when the name also writes Z,
// those invisible texels no longer write depth, so a later draw behind the transparent part of a glyph quad is no longer hidden by it (and depthTex, AO and fog see the surface behind). A fallback: the game's
// name draws keep their own alpha test on (traced in play: ALPHATESTENABLE 1, ALPHAREF 1, Z write on), and the guard does not run for them.
inline constexpr Set kAlphaGuard[]={{AlphaTestEnable,1},{AlphaFunc,kCmpGreater},{AlphaRef,0}};
// Around a depth-final draw: stencil REPLACE with ref 0 where it passes (the draw's own colour, depth and alpha test unchanged).
inline constexpr Set kUnmark[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,0},{StencilWriteMask,kBit},{TwoSidedStencilMode,0}};
// The WorldComposite and legacy composite quads: no depth test, stencil test only, nothing written.
inline constexpr Set kComposite[]={{ZEnable,0},{ZWriteEnable,0},{StencilEnable,1},{StencilFunc,kCmpNotEqual},{StencilPass,kOpKeep},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,kBit},{StencilMask,kBit},{StencilWriteMask,0},{TwoSidedStencilMode,0}};
// Depth formats with stencil bits (D3DFMT_D24S8, D24X4S4, D15S1, D24FS8, INTZ); D24X8, D16, D32 have none.
inline bool hasStencil(unsigned long format,unsigned long intz){return format==75||format==79||format==73||format==81||format==intz;}
// A name draw: the UI-hash vertex shader whose c3 is not the screen-space (0,0,0,1) the boundary waits for (a perspective projection), RGB written, normal alpha blending with a SRCALPHA source and a destination that is
// neither ZERO (effectively opaque) nor a SRCCOLOR modulate. Water never reaches it (a water vertex shader is not the UI hash).
template<class B,class S,class D,class C> inline bool nameBlend(B&& blend,S&& src,D&& dst,C&& colorWrite){
    if((colorWrite()&7)==0||!blend())return false;
    const unsigned long d=dst();
    return src()==kBlendSrcAlpha&&d!=NorthlightTranslucentDepth::kBlendZero&&d!=kBlendSrcColor;
}
// Per-frame bookkeeping and the session verdict. Frame counters feed NAMEMASK.
struct Mask {
    bool setting=false,disabled=false,debug=false;const char* reason="";
    // Frame
    unsigned marks=0,runs=0,unmarkDraws=0,stencilClears=0;bool cleared=false,composited=false,stencilChecked=false /* the game-uses-stencil guard has read STENCILENABLE this frame */;
    // Totals
    unsigned marksTotal=0;
    bool armed()const{return setting&&!disabled;}
    bool hasMarks()const{return marks!=0;}
    // The first mark (name draw) of a frame needs a stencil clear of the frame behind it: the game's Clear(Z) of the world depth (noteClear(false)), else one of ours.
    bool needsClear()const{return marks==0&&!cleared;}
    void noteClear(bool ours){cleared=true;if(ours)++stencilClears;}
    void noteMark(){++marks;++marksTotal;}
    void disable(const char* why){if(!disabled){disabled=true;reason=why;}}
    void endFrame(){marks=runs=unmarkDraws=stencilClears=0;cleared=composited=stencilChecked=false;}
};
}
