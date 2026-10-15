#pragma once
#include "world_context.h"
#include "wmo_shader_signatures.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace NorthlightWmoContext {
struct Lighting {
    float lightDirection[3]={},ambient[3]={},direct[3]={};
    float dayFraction=0;
    // the game's 18 current light colours (sky +0xd4, see sun_hue.h for the slot order).
    // slotsConsistent: this read's slot 1 equals its raw copy at +0x17c (0x7ee892), a free
    // per-frame check of the slot layout. Not +0x1a8/+0x1ac: the sun-glare darkening, the underwater darkening and the spell flash
    // rewrite those after the copy, which made overcast Tirisfal fail (0.3.163). slotsProven: that, and the sampler/copy code
    // matched lightSlotProofs() (Windows reads only).
    std::uint32_t slots[18]={};bool slotsConsistent=false,slotsProven=false;
    // The light1 colours as read (+0x1a8 direct, +0x1ac ambient) and what predicts their rewrite: the spell flash blend (+0x50 of the
    // block), the sun glare screen glow (0xd38f4c) and the camera's liquid type (0xcd8794). predictors: glow and liquid were read twice
    // alike and predictionProofs() matched (Windows reads only); without it the prediction is never used.
    std::uint32_t packedDirect=0,packedAmbient=0,liquidType=0;float glow=0;std::uint8_t flash=0;bool predictors=false;
};
inline const WmoShaderSignature* signature(std::uint64_t hash) {
    size_t low=0,high=sizeof(kWmoShaderSignatures)/sizeof(kWmoShaderSignatures[0]);
    while(low<high){const size_t middle=(low+high)/2;if(kWmoShaderSignatures[middle].hash<hash)low=middle+1;else high=middle;}
    if(low==sizeof(kWmoShaderSignatures)/sizeof(kWmoShaderSignatures[0])||kWmoShaderSignatures[low].hash!=hash)return nullptr;
    return &kWmoShaderSignatures[low];
}
inline constexpr size_t SkyBytes=0x1b8,LightSlotBytes=0xd4;
// 0.3.200: how far the sky block's own camera copy may be from the camera before the block counts as stale. The engine
// updates that copy at its own point of the frame and not every frame, so in motion (mounted, measured 4..6.5 units) it
// trails the camera by several frames of travel (0.25 used to reject it: the light then switched to the native fallback
// frame to frame, a visible flicker). A teleport or map change moves it by far more than this.
inline constexpr float SkyCameraTolerance=40.f;
inline float scalar(const unsigned char* bytes,size_t at){float f;std::memcpy(&f,bytes+at,4);return f;}
inline std::uint32_t packed(const unsigned char* bytes,size_t at){std::uint32_t n;std::memcpy(&n,bytes+at,4);return n;}
inline void rgb(std::uint32_t color,float* output){output[0]=float((color>>16)&255)/255.f;output[1]=float((color>>8)&255)/255.f;output[2]=float(color&255)/255.f;}

// Fresh engine sky/environment source, independent of WMO VS constant usage.
// Prelit WMO variants do NOT read c10..12 and cannot supply those values.
// The light source is the outdoor world environment; WMO vertex baking and
// indoor group overrides remain distinct material/local lighting contributions.
inline bool decodeGlobalLighting(const unsigned char* bytes,size_t size,const float* expectedCamera,Lighting& out) {
    if(!bytes||size<SkyBytes||!expectedCamera)return false;
    Lighting value;value.dayFraction=scalar(bytes,4);
    if(!std::isfinite(value.dayFraction)||value.dayFraction<0||value.dayFraction>=1)return false;
    float cameraError=0,lengthSquared=0;
    for(unsigned i=0;i<3;++i){
        float camera=scalar(bytes,0x18+i*4),direction=scalar(bytes,0x19c+i*4);
        if(!std::isfinite(camera)||!std::isfinite(expectedCamera[i])||!std::isfinite(direction)||
           std::fabs(camera)>100000||std::fabs(expectedCamera[i])>100000)return false;
        float delta=camera-expectedCamera[i];cameraError+=delta*delta;
        value.lightDirection[i]=-direction;lengthSquared+=direction*direction;
    }
    if(cameraError>SkyCameraTolerance*SkyCameraTolerance||lengthSquared<.25f||lengthSquared>2.25f)return false;
    const float inverseLength=1/std::sqrt(lengthSquared);
    for(auto& component:value.lightDirection)component*=inverseLength;
    value.packedDirect=packed(bytes,0x1a8);value.packedAmbient=packed(bytes,0x1ac);value.flash=bytes[0x50];
    rgb(value.packedDirect,value.direct);rgb(value.packedAmbient,value.ambient);
    std::memcpy(value.slots,bytes+LightSlotBytes,sizeof value.slots);
    value.slotsConsistent=value.slots[1]==packed(bytes,0x17c);
    out=value;return true;
}
inline bool decodeCoherentGlobalLighting(const unsigned char* first,const unsigned char* second,size_t size,
                                         const float* expectedCamera,Lighting& out) {
    return first&&second&&size>=SkyBytes&&!std::memcmp(first,second,SkyBytes)&&decodeGlobalLighting(first,size,expectedCamera,out);
}
// The game rewrites light1 (direct +0x1a8, ambient +0x1ac) after the band copy (SetColors, 0x7f356d), in this order:
//  (a) underwater (0xcd8794 non-zero): DarkenColor of the ambient (store 0x7f36c6) and then the direct (0x7f36da);
//  (b) a spell-visual flash (+0x50 non-zero): InterpColor of both towards +0x51;
//  (c) DayNight::Update (0x7816f0): the sun glare darkening, scale = int((1 - 0.35 glow) 255), c = (scale c + 255) >> 8 per channel
//      (ambient 0x78184e, then direct 0x7818b0). glow is g_sunGlare.m_screenGlow (0xd38f4c) from the previous frame's RenderGlares.
// The weather (storm blend) is already inside the band colours when they are copied. So without (a) and (b) the colours are
// a pure function of the bands (slot 1 direct, slot 0 ambient) and the glow: predictScale/predictColour below, accepted within
// one LSB per channel (the x87 float steps). Since the command stream the block is read on the replay thread, where the game thread
// may be between the copy and (c): such a read holds the unscaled band colours (Duskwood, scale ~.8-.93: the sun light and its fog
// glow flashed brighter for that single frame, 0.3.207).
inline std::uint8_t predictScale(float glow){
    const float glare=glow*0.35f,scaled=(1.0f-glare)*255.0f; /* the game's float steps; fint = round to nearest */
    if(!std::isfinite(scaled)||scaled<-1e6f||scaled>1e6f)return 255;
    return std::uint8_t(std::lrintf(scaled)&255);
}
inline std::uint32_t predictColour(std::uint32_t band,std::uint8_t scale){
    std::uint32_t out=band&0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8)out|=((std::uint32_t(scale)*((band>>shift)&255)+255)>>8)<<shift;
    return out;
}
inline bool withinLsb(std::uint32_t a,std::uint32_t b,int tolerance=1){
    for(unsigned shift=0;shift<24;shift+=8){const int d=int((a>>shift)&255)-int((b>>shift)&255);if(d>tolerance||d<-tolerance)return false;}
    return true;
}
// Glow and liquid type, each read twice (the glow changes once a frame in RenderGlares, the liquid on a camera move); a pair that differs
// leaves `out.predictors` false. Read order identical at capture and replay (game_snapshot.h captureReaders).
template<class Read> inline void readPredictors(Read read,Lighting& out){
    float glow[2];std::uint32_t liquid[2];out.predictors=false;
    if(!read(0xd38f4c,&glow[0],4)||!read(0xd38f4c,&glow[1],4)||!read(0xcd8794,&liquid[0],4)||!read(0xcd8794,&liquid[1],4))return;
    if(std::memcmp(glow,glow+1,4)||liquid[0]!=liquid[1]||!std::isfinite(glow[0]))return;
    out.glow=glow[0];out.liquidType=liquid[0];out.predictors=true;
}
// A read the prediction applies to: layout confirmed, no liquid, no flash. Returns true and the predicted packed colours when so.
inline bool predicted(const Lighting& l,std::uint32_t& direct,std::uint32_t& ambient){
    if(!l.predictors||!l.slotsConsistent||l.liquidType||l.flash)return false;
    const std::uint8_t scale=predictScale(l.glow);direct=predictColour(l.slots[1],scale);ambient=predictColour(l.slots[0],scale);return true;
}
// A coherent read already equal to the prediction is accepted as read; one that is not (the unscaled colours of a read between the copy and
// the rewrite, a stale glow, a wrapped flash) falls back to the hold: a read whose colour equals its slot keeps the last accepted colour when
// that one was rescaled DARKER than the band (a brighter rescale is never stretched).
// The race lasts one read: MaxHeld consecutive unscaled reads, or HoldMs since the last accepted one
// (a loading screen, alt-tab), accept them again (the weather ended). The direction and the slots of
// the read are used as read. Filter a copy and keep it only when the context accepts the read, so a
// read that is retried within the frame is counted and remembered once.
struct LightingHold {
    static constexpr std::uint32_t HoldMs=500;
    static constexpr unsigned MaxHeld=2;
    struct Colour {float value[3]={};bool darker=false;std::uint32_t at=0;unsigned run=0;bool valid=false;};
    Colour direct,ambient;std::string map;unsigned held=0,predictedAccepts=0,predictedMisses=0; /* per colour: matched / not matched when predictable */
    // darker: no channel above the band colour and at least one below it.
    static bool darker(const float* colour,const float* band){
        bool below=false;
        for(unsigned i=0;i<3;++i){if(colour[i]>band[i])return false;below|=colour[i]<band[i];}
        return below;
    }
    static bool keep(Colour& c,float* colour,std::uint32_t slot,bool force,std::uint32_t now,bool accepted=false){
        float band[3];rgb(slot,band);
        const bool unscaled=colour[0]==band[0]&&colour[1]==band[1]&&colour[2]==band[2];
        const bool hold=!accepted&&c.valid&&c.darker&&(unscaled||force)&&c.run<MaxHeld&&now-c.at<=HoldMs;
        if(hold){std::memcpy(colour,c.value,sizeof c.value);++c.run;return true;}
        std::memcpy(c.value,colour,sizeof c.value);c.darker=darker(colour,band);c.at=now;c.run=0;c.valid=true;return false;
    }
    // Returns true when a colour of this read was replaced by the last accepted one.
    bool filter(Lighting& light,const char* nextMap,std::uint32_t now){
        if(!nextMap||map!=nextMap){direct=Colour{};ambient=Colour{};map=nextMap?nextMap:"";}
        if(!light.slotsConsistent)return false; /* the slot layout is unconfirmed: nothing to compare against */
        std::uint32_t wantDirect=0,wantAmbient=0;bool matchDirect=false,matchAmbient=false;
        if(predicted(light,wantDirect,wantAmbient)){
            matchDirect=withinLsb(light.packedDirect,wantDirect);matchAmbient=withinLsb(light.packedAmbient,wantAmbient);
            predictedAccepts+=matchDirect+matchAmbient;predictedMisses+=!matchDirect+!matchAmbient;
        }
        // The game rewrites the ambient first and the direct after it (0x78184e, 0x7818b0): an unscaled ambient means an unscaled direct.
        const bool heldAmbient=keep(ambient,light.ambient,light.slots[0],false,now,matchAmbient);
        const bool heldDirect=keep(direct,light.direct,light.slots[1],heldAmbient,now,matchDirect);
        held+=heldDirect||heldAmbient;
        return heldDirect||heldAmbient;
    }
};
// Reuse rigid-view validation and coordinate conversion from TerrainContext.
// view MUST come from the independently verified current camera, never WMO's
// c31..33 model-view transform.
inline bool context(const float* view,const Lighting& lighting,NorthlightWorldContext::TerrainContext& out) {
    if(!view)return false;float constants[12]={};
    for(unsigned i=0;i<3;++i){
        for(unsigned j=0;j<3;++j)constants[i]+=lighting.lightDirection[j]*view[j*4+i];
        constants[4+i]=lighting.ambient[i];constants[8+i]=lighting.direct[i];
    }
    return NorthlightWorldContext::decodeTerrain(view,constants,out);
}
// Terrain variants need not populate the same native light constants on every
// draw. Prefer the independently validated environment, but still require this
// draw's rigid world view to agree with the current game camera.
inline bool terrainContext(const float* view,const float* nativeLight,const float* camera,
                           const Lighting* global,NorthlightWorldContext::TerrainContext& out,bool* usedGlobal=nullptr) {
    NorthlightWorldContext::TerrainContext candidate;if(usedGlobal)*usedGlobal=false;
    if(global&&context(view,*global,candidate)&&NorthlightWorldContext::cameraAgrees(candidate,camera)){
        out=candidate;if(usedGlobal)*usedGlobal=true;return true;
    }
    if(nativeLight&&NorthlightWorldContext::decodeTerrain(view,nativeLight,candidate)&&
       NorthlightWorldContext::cameraAgrees(candidate,camera)){out=candidate;return true;}
    return false;
}
// Optional exact-hash SM3 lit-WMO proof. c10 ambient, c11 direct, -c12 view
// source direction. Baked vertex-color and local-point-light terms are not
// represented by this global directional/ambient pair.
inline bool decodeLitShader(std::uint64_t hash,const float* view,const float* c10to12,
                            NorthlightWorldContext::TerrainContext& out) {
    const auto* known=signature(hash);if(!known||!known->lighting||!view||!c10to12)return false;
    float constants[12]={};
    for(unsigned i=0;i<3;++i){constants[i]=-c10to12[8+i];constants[4+i]=c10to12[i];constants[8+i]=c10to12[4+i];}
    return NorthlightWorldContext::decodeTerrain(view,constants,out);
}

struct CodeProof {uintptr_t address;const unsigned char* bytes;size_t size;};
inline const CodeProof* proofs(size_t& count) {
    static const unsigned char sky[]={0xb8,0x00,0x8b,0xd3,0x00,0xc3};
    static const unsigned char directionCopy[]={0x8b,0x3d,0xa8,0x04,0xce,0x00,0x83,0xc7,0x58,0x8d,0x86,0x9c,0x01,0x00,0x00,0x50,0x8b,0xcf,0xe8,0x13,0x32,0x0b,0x00};
    static const unsigned char directionSetter[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x10,0x89,0x51,0x24,0x8b,0x50,0x04,0x89,0x51,0x28,0x8b,0x40,0x08,0x89,0x41,0x2c};
    static const unsigned char directSource[]={0x0f,0xb6,0x88,0xaa,0x01,0x00,0x00,0x0f,0xb6,0x90,0xa9,0x01,0x00,0x00,0x89,0x4d,0x0c,0x0f,0xb6,0x88,0xa8,0x01,0x00,0x00};
    static const unsigned char ambientSource[]={0x0f,0xb6,0x88,0xae,0x01,0x00,0x00,0x0f,0xb6,0x90,0xad,0x01,0x00,0x00,0x0f,0xb6,0x80,0xac,0x01,0x00,0x00};
    static const unsigned char ambientUpload[]={0x53,0x8d,0x55,0xf4,0x52,0x6a,0x0a,0x6a,0x00,0xff,0xd0};
    static const unsigned char directUpload[]={0x53,0x8d,0x45,0xe8,0x50,0x6a,0x0b,0x6a,0x00,0xff,0xd2};
    static const unsigned char directionUpload[]={0x6a,0x0c,0x6a,0x00,0xff,0xd0};
    static const CodeProof values[]={{0x7ecef0,sky,sizeof sky},{0x7818b6,directionCopy,sizeof directionCopy},
        {0x834ae0,directionSetter,sizeof directionSetter},{0x7a8c44,directSource,sizeof directSource},
        {0x7a8c94,ambientSource,sizeof ambientSource},{0x7a8db8,ambientUpload,sizeof ambientUpload},
        {0x7a8dd1,directUpload,sizeof directUpload},{0x7a8def,directionUpload,sizeof directionUpload}};
    count=sizeof(values)/sizeof(values[0]);return values;
}
// slot order and destination of the light colours. The sampler stores band 9 and
// band 10 in slots 9/10 (+0x24/+0x28); the frame copy writes the 18 colours to 0xd38bd4;
// band 9 feeds the native sun/moon billboards and glare records; slot 1 is copied unscaled to
// +0x17c (the per-frame layout check).
inline const CodeProof* lightSlotProofs(size_t& count) {
    static const unsigned char bandStores[]={0x6a,0x0a,0x57,0x8d,0x55,0x10,0x53,0x52,0x89,0x4e,0x24,0xe8,0x68,0xfe,0xff,0xff,
        0x8b,0x00,0x6a,0x0b,0x57,0x8d,0x4d,0x10,0x53,0x51,0x89,0x46,0x28};
    static const unsigned char frameCopy[]={0x8d,0x85,0x50,0xff,0xff,0xff,0x50,0xb9,0xd4,0x8b,0xd3,0x00,0xe8,0x92,0xa3,0xff,0xff};
    static const unsigned char sunToGlare[]={0xa1,0xf8,0x8b,0xd3,0x00,0x89,0x4d,0xfc,0xa3,0x34,0x8e,0xd3,0x00,0xdb,0x45,0xfc,0xa3,0xc0,0x8e,0xd3,0x00};
    static const unsigned char rawDirectCopy[]={0x8b,0x15,0xd8,0x8b,0xd3,0x00,0xa3,0x94,0x8c,0xd3,0x00,0x89,0x15,0x7c,0x8c,0xd3,0x00};
    static const CodeProof values[]={{0x7ec0b8,bandStores,sizeof bandStores},{0x7f356d,frameCopy,sizeof frameCopy},{0x7f36f6,sunToGlare,sizeof sunToGlare},
        {0x7ee887,rawDirectCopy,sizeof rawDirectCopy}};
    count=sizeof(values)/sizeof(values[0]);return values;
}
// The code behind predictScale/predictColour and the two conditions that void it (bytes of the stock 3.3.5a client, checked by validate_wmo_context.py):
// SetLightColors copies slot 1 to light1 direct (0x7ee756) and slot 0 to light1 ambient (0x7ee774 load, 0x7ee84a store); SetColors darkens the
// ambient (0x7f36c6) and the direct (0x7f36da) underwater from the camera's liquid type (SceneCamLiquidStatus 0x780620 returns 0xcd8794) and
// flashes the ambient from +0x50 (0x7f3748); DayNight::Update (0x7816f0) reads glow 0xd38f4c, scales by 0.35 (constant 0xa3e860), makes
// scale = fistp((1 - glare) 255) (255.0 at 0x9e30c0) and applies (scale c + 255) >> 8 to the ambient (store 0x78184e) and the direct (0x7818b0).
inline const CodeProof* predictionProofs(size_t& count) {
    static const unsigned char directCopy[]={0x8b,0x15,0xd8,0x8b,0xd3,0x00,0x0f,0xb6,0x0d,0xd6,0x8b,0xd3,0x00,0x89,0x15,0xa8,0x8c,0xd3,0x00};
    static const unsigned char ambientLoad[]={0x8b,0x35,0xd4,0x8b,0xd3,0x00};
    static const unsigned char ambientStore[]={0x89,0x35,0xac,0x8c,0xd3,0x00};
    static const unsigned char liquidGetter[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x85,0xc0,0x74,0x08,0xd9,0x05,0x90,0x87,0xcd,0x00,0xd9,0x18,0xa1,0x94,0x87,0xcd,0x00,0x5d};
    static const unsigned char darkenAmbient[]={0x89,0x0d,0xac,0x8c,0xd3,0x00};
    static const unsigned char darkenDirect[]={0xa3,0xa8,0x8c,0xd3,0x00};
    static const unsigned char flashAmbient[]={0x0f,0xb6,0x05,0x50,0x8b,0xd3,0x00,0x85,0xc0,0x89,0x75,0xfc,0x74,0x15,0x8d,0x4d,0xfc,0x51,0x50,0xb9,0xac,0x8c,0xd3,0x00};
    static const unsigned char glowRead[]={0xd9,0x05,0x4c,0x8f,0xd3,0x00,0xd8,0x0d,0x60,0xe8,0xa3,0x00,0xd9,0x55,0xfc};
    static const unsigned char scaleFormula[]={0xd9,0xe8,0xd8,0x65,0xfc,0x83,0xc4,0x04,0xd8,0x0d,0xc0,0x30,0x9e,0x00,0xd9,0x55};
    static const unsigned char scaleRound[]={0xd9,0x5d,0x08,0xd9,0x45,0x08,0xdb,0x5d,0x0c,0x0f,0xb6,0x45,0x0c};
    static const unsigned char scaleApply[]={0x0f,0xaf,0xc8,0x81,0xc1,0xff,0x00,0x00,0x00,0xc1,0xe9,0x08};
    static const unsigned char scaledAmbient[]={0x89,0x96,0xac,0x01,0x00,0x00};
    static const unsigned char scaledDirect[]={0x89,0x8e,0xa8,0x01,0x00,0x00};
    static const unsigned char glareScaleConstant[]={0x33,0x33,0xb3,0x3e}; /* 0.35f */
    static const unsigned char byteMaxConstant[]={0x00,0x00,0x7f,0x43};    /* 255.0f */
    static const CodeProof values[]={{0x7ee756,directCopy,sizeof directCopy},{0x7ee774,ambientLoad,sizeof ambientLoad},{0x7ee84a,ambientStore,sizeof ambientStore},
        {0x780620,liquidGetter,sizeof liquidGetter},{0x7f36c6,darkenAmbient,sizeof darkenAmbient},{0x7f36da,darkenDirect,sizeof darkenDirect},
        {0x7f3748,flashAmbient,sizeof flashAmbient},{0x781783,glowRead,sizeof glowRead},{0x7817d8,scaleFormula,sizeof scaleFormula},
        {0x7817e9,scaleRound,sizeof scaleRound},{0x781803,scaleApply,sizeof scaleApply},{0x78184e,scaledAmbient,sizeof scaledAmbient},
        {0x7818b0,scaledDirect,sizeof scaledDirect},{0xa3e860,glareScaleConstant,sizeof glareScaleConstant},{0x9e30c0,byteMaxConstant,sizeof byteMaxConstant}};
    count=sizeof(values)/sizeof(values[0]);return values;
}
#ifdef _WIN32
inline bool proven(const CodeProof* proof,size_t count) {
    for(size_t i=0;i<count;++i){unsigned char actual[64];if(proof[i].size>sizeof actual||
        !NorthlightWorldContext::readSelf(proof[i].address,actual,proof[i].size)||std::memcmp(actual,proof[i].bytes,proof[i].size))return false;}
    return true;
}
inline bool supportedClient() {
    if(!NorthlightWorldContext::supportedClient())return false;
    size_t count;const auto* proof=proofs(count);
    for(size_t i=0;i<count;++i){unsigned char actual[64];if(proof[i].size>sizeof actual||
        !NorthlightWorldContext::readSelf(proof[i].address,actual,proof[i].size)||std::memcmp(actual,proof[i].bytes,proof[i].size))return false;}
    return true;
}
inline bool readGlobalLighting(const float* expectedCamera,Lighting& out) {
    static const bool supported=supportedClient();if(!supported)return false;
    unsigned char first[SkyBytes],second[SkyBytes];
    static const bool slots=[]{size_t count;const auto* proof=lightSlotProofs(count);return proven(proof,count);}();
    if(!(NorthlightWorldContext::readSelf(0xd38b00,first,sizeof first)&&
         NorthlightWorldContext::readSelf(0xd38b00,second,sizeof second)&&
         decodeCoherentGlobalLighting(first,second,SkyBytes,expectedCamera,out)))return false;
    static const bool predictionCode=[]{size_t count;const auto* proof=predictionProofs(count);return proven(proof,count);}();
    readPredictors(NorthlightWorldContext::readSelf,out);out.predictors=out.predictors&&predictionCode&&slots&&out.slotsConsistent;
    out.slotsProven=slots&&out.slotsConsistent;return true;
}
#endif
} // namespace NorthlightWmoContext
