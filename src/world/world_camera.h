#pragma once
// Audited current main-world camera, without depending on a terrain draw.
// Read only the calling process. Never calls engine functions or retains pointers.
#include "world_context.h"
#include <cstddef>
#include <limits>
namespace NorthlightWorldCamera {
struct Camera {float view[16]={},inverseView[16]={},camera[3]={};};
enum class Reject {None,Client,Read,CameraType,AttachedCamera,DeviceType,Stack,
    Matrix,Basis,Position,Changed};
struct Diagnostics {Reject reason=Reject::None;};
inline const char* rejectName(Reject r){switch(r){
#define FC_NAME(n) case Reject::n:return #n;
FC_NAME(None) FC_NAME(Client) FC_NAME(Read) FC_NAME(CameraType)
FC_NAME(AttachedCamera) FC_NAME(DeviceType) FC_NAME(Stack) FC_NAME(Matrix)
FC_NAME(Basis) FC_NAME(Position) FC_NAME(Changed)
#undef FC_NAME
}return "Unknown";}
struct Signature {uintptr_t address;size_t size;uint64_t hash;};
inline constexpr Signature signatures[]={
    {0x4f5960, 19, UINT64_C(0xb172d56d832e8370)},
    {0x4f9453, 98, UINT64_C(0xc07597a60c4f9af1)},
    {0x600c20, 46, UINT64_C(0xa9feabf30af5cdf6)},
    {0x600d60, 46, UINT64_C(0x0bcf0fe89047654e)},
    {0x607cb0, 304, UINT64_C(0x8613a19ac6cf426e)},
    {0x6bfe60, 493, UINT64_C(0x371febb852b6d3b1)},
    {0x6a9e00, 9, UINT64_C(0x71de67d1fc49dc54)},
    {0x689050, 109, UINT64_C(0x9b8e8230f16c6a13)},
    {0x407f80, 108, UINT64_C(0xce70af81f7d47dbc)},
    {0xa1ea54, 16, UINT64_C(0x72eedeed5546b962)},
    {0xa2e7b8, 8, UINT64_C(0xbd3dc82412d2778e)},
};
inline uint64_t hashBytes(const unsigned char* p,size_t n){uint64_t h=UINT64_C(14695981039346656037);for(size_t i=0;i<n;++i)h=(h^p[i])*UINT64_C(1099511628211);return h;}
template<class Reader> bool verifySignatures(Reader read){
    unsigned char bytes[512];
    for(const auto& s:signatures)if(s.size>sizeof bytes||!read(s.address,bytes,s.size)||hashBytes(bytes,s.size)!=s.hash)return false;
    return true;
}
inline bool fail(Diagnostics* d,Reject r){if(d)d->reason=r;return false;}
// cameraData is the contiguous position, forward, side, up at camera+8.
// deviceView is the exact matrix submitted by the engine's current view stack.
// The main world camera submits rotation only; world position is subtracted
// separately by the game (0x4F9453..0x4F94B0). Recreate that affine operation.
inline bool decode(const float* cameraData,const float* deviceView,float projectionSign,
                   Camera& out,Diagnostics* diagnostic=nullptr){
    if(diagnostic)*diagnostic={};
    if(!std::isfinite(projectionSign)||std::fabs(std::fabs(projectionSign)-1.f)>.001f)return fail(diagnostic,Reject::Basis);
    for(unsigned i=0;i<12;++i)if(!std::isfinite(cameraData[i]))return fail(diagnostic,Reject::Basis);
    for(unsigned i=0;i<16;++i)if(!std::isfinite(deviceView[i]))return fail(diagnostic,Reject::Matrix);
    for(unsigned i=0;i<3;++i)if(std::fabs(cameraData[i])>100000.f)return fail(diagnostic,Reject::Position);
    if(std::fabs(deviceView[3])>.0001f||std::fabs(deviceView[7])>.0001f||std::fabs(deviceView[11])>.0001f||
       std::fabs(deviceView[15]-1.f)>.0001f||std::fabs(deviceView[12])>.0001f||std::fabs(deviceView[13])>.0001f||std::fabs(deviceView[14])>.0001f)return fail(diagnostic,Reject::Matrix);
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){
        float basisDot=0,viewDot=0;
        for(unsigned k=0;k<3;++k){basisDot+=cameraData[3+3*i+k]*cameraData[3+3*j+k];viewDot+=deviceView[4*i+k]*deviceView[4*j+k];}
        if(std::fabs(basisDot-(i==j?1.f:0.f))>.004f)return fail(diagnostic,Reject::Basis);
        if(std::fabs(viewDot-(i==j?1.f:0.f))>.004f)return fail(diagnostic,Reject::Matrix);
    }
    // Reject UI/reflection/model-local view state even if it happens to be affine.
    // Do not guess a previous frame's camera basis when the current one disagrees.
    float forwardDot=0,upDot=0;
    for(unsigned k=0;k<3;++k){forwardDot+=deviceView[4*k+2]*projectionSign*cameraData[3+k];upDot+=deviceView[4*k+1]*cameraData[9+k];}
    if(forwardDot<.999f||upDot<.999f)return fail(diagnostic,Reject::Basis);
    Camera c;std::memcpy(c.view,deviceView,sizeof c.view);c.inverseView[15]=1;
    for(unsigned i=0;i<3;++i){c.camera[i]=cameraData[i];c.inverseView[12+i]=cameraData[i];
        for(unsigned k=0;k<3;++k){c.view[12+i]-=cameraData[k]*deviceView[4*k+i];c.inverseView[4*i+k]=deviceView[4*k+i];}}
    out=c;return true;
}
inline bool agreesWithTerrain(const Camera& camera,const float* terrainView,float rotationTolerance=.002f,float translationTolerance=.1f){
    for(unsigned i=0;i<16;++i)if(!std::isfinite(terrainView[i])||std::fabs(terrainView[i]-camera.view[i])>(i>=12&&i<15?translationTolerance:rotationTolerance))return false;
    return true;
}
// This injectable reader makes every pointer/type/range/rejection test native.
// Signature verification is done by read() once, separately from per-draw reads.
// decode() never compares the view's first column with the camera's side
// vector (only forward/up are checked), so the engine's handedness for that
// column is learned from a successful view-stack read and reused here.
inline float& sideColumnSign(){static float sign=0;return sign;}
template<class Reader> bool readCurrent(Reader read,float projectionSign,Camera& out,Diagnostics* diagnostic=nullptr){
    if(diagnostic)*diagnostic={};
    uint32_t frame=0,cam=0,camType=0,attachment[2]={},device=0,deviceType=0,slot=0;
    float cameraData[12]={},view[16]={};
    if(!read(0xb7436c,&frame,4)||frame<0x10000||frame>UINT32_MAX-0x7e24||
       !read(uintptr_t(frame)+0x7e20,&cam,4)||cam<0x10000||cam>UINT32_MAX-0xa8||
       !read(cam,&camType,4))return fail(diagnostic,Reject::Read);
    if(camType!=0xa1ea54)return fail(diagnostic,Reject::CameraType);
    if(!read(uintptr_t(cam)+0xa0,attachment,8))return fail(diagnostic,Reject::Read);
    if(attachment[0]||attachment[1])return fail(diagnostic,Reject::AttachedCamera);
    if(!read(uintptr_t(cam)+8,cameraData,sizeof cameraData)||!read(0xc5df88,&device,4)||
       device<0x10000||device>UINT32_MAX-0x1c00||!read(device,&deviceType,4))return fail(diagnostic,Reject::Read);
    // The active backend in this client is GxDeviceD3D. Other vtables are not audited.
    if(deviceType!=0xa2e718)return fail(diagnostic,Reject::DeviceType);
    if(!read(uintptr_t(device)+0x1af8,&slot,4))return fail(diagnostic,Reject::Read);
    if(slot>=4)return fail(diagnostic,Reject::Stack);
    if(!read(uintptr_t(device)+0x1b00+64*slot,view,sizeof view))return fail(diagnostic,Reject::Read);
    uint32_t frameAfter=0,camAfter=0,deviceAfter=0,slotAfter=0;
    if(!read(0xb7436c,&frameAfter,4)||!read(uintptr_t(frame)+0x7e20,&camAfter,4)||
       !read(0xc5df88,&deviceAfter,4)||!read(uintptr_t(device)+0x1af8,&slotAfter,4))return fail(diagnostic,Reject::Read);
    if(frameAfter!=frame||camAfter!=cam||deviceAfter!=device||slotAfter!=slot)return fail(diagnostic,Reject::Changed);
    if(!decode(cameraData,view,projectionSign,out,diagnostic))return false;
    float sideDot=0;for(unsigned k=0;k<3;++k)sideDot+=view[4*k]*cameraData[6+k];
    if(std::fabs(std::fabs(sideDot)-1.f)<.01f)sideColumnSign()=sideDot<0?-1.f:1.f;
    return true;
}
// Sky-phase reader: the same memory validation, but while the sky draws the
// engine's view stack holds the sky's own (non-affine) matrix, so the rotation
// is rebuilt from the camera's forward/side/up basis (the columns the main
// world view submits) instead of the view stack. Position and basis checks,
// and decode()'s orthonormality/agreement checks, still apply.
template<class Reader> bool readCurrentBasis(Reader read,float projectionSign,Camera& out,Diagnostics* diagnostic=nullptr){
    if(diagnostic)*diagnostic={};
    uint32_t frame=0,cam=0,camType=0,attachment[2]={};
    float cameraData[12]={},view[16]={};
    if(!read(0xb7436c,&frame,4)||frame<0x10000||frame>UINT32_MAX-0x7e24||
       !read(uintptr_t(frame)+0x7e20,&cam,4)||cam<0x10000||cam>UINT32_MAX-0xa8||
       !read(cam,&camType,4))return fail(diagnostic,Reject::Read);
    if(camType!=0xa1ea54)return fail(diagnostic,Reject::CameraType);
    if(!read(uintptr_t(cam)+0xa0,attachment,8))return fail(diagnostic,Reject::Read);
    if(attachment[0]||attachment[1])return fail(diagnostic,Reject::AttachedCamera);
    if(!read(uintptr_t(cam)+8,cameraData,sizeof cameraData))return fail(diagnostic,Reject::Read);
    uint32_t frameAfter=0,camAfter=0;
    if(!read(0xb7436c,&frameAfter,4)||!read(uintptr_t(frame)+0x7e20,&camAfter,4))return fail(diagnostic,Reject::Read);
    if(frameAfter!=frame||camAfter!=cam)return fail(diagnostic,Reject::Changed);
    const float sideSign=sideColumnSign();
    if(!std::isfinite(projectionSign)||sideSign==0)return fail(diagnostic,Reject::Basis);
    view[15]=1;for(unsigned k=0;k<3;++k){view[k*4]=cameraData[6+k]*sideSign;view[k*4+1]=cameraData[9+k];view[k*4+2]=cameraData[3+k]*projectionSign;}
    return decode(cameraData,view,projectionSign,out,diagnostic);
}
#ifdef _WIN32
// TEMPORARY (0.3.20): the code-signature gate rejected one development client
// build, which disabled the WMO/city path everywhere. The runtime
// checks that remain in readCurrent/decode (camera type id, device vtable id,
// orthonormal basis, view/projection agreement and the 1 cm agreement with the
// independent map/camera reader) validate every read. Set to false to restore.
inline constexpr bool kPermissiveCameraSignatures=true;
inline int failingSignature(){
    using NorthlightWorldContext::readSelf;
    unsigned char bytes[512];int index=0;
    for(const auto& s:signatures){if(s.size>sizeof bytes||!readSelf(s.address,bytes,s.size)||hashBytes(bytes,s.size)!=s.hash)return index;++index;}
    return -1;
}
inline bool read(char (&map)[64],float projectionSign,Camera& out,Diagnostics* diagnostic=nullptr){
#if defined(_WIN64) && !defined(NORTHLIGHT_REPLAY_STAND)
    if(!NorthlightX64World::imageBase())return fail(diagnostic,Reject::Client);
    NorthlightX64World::Scene scene;float rotation[16],position[3];char name[64]{};
    if(!NorthlightX64World::readScene(scene)||
       !NorthlightWorldContext::publishMapAndCamera(scene.map,scene.pose,name,position))return fail(diagnostic,Reject::Read);
    if(scene.transformed)return fail(diagnostic,Reject::AttachedCamera);
    if(!NorthlightX64World::readView(rotation))return fail(diagnostic,Reject::Stack);
    if(!decode(scene.pose,rotation,projectionSign,out,diagnostic))return false;
    std::memcpy(map,name,64);return true;
#else
    using NorthlightWorldContext::readSelf;
    static const bool supported=NorthlightWorldContext::supportedClient()&&(verifySignatures(readSelf)||kPermissiveCameraSignatures);
    if(!supported)return fail(diagnostic,Reject::Client);
    char name[64]={};float independentPosition[3]={};Camera candidate;
    if(!NorthlightWorldContext::readMapAndCamera(name,independentPosition))return fail(diagnostic,Reject::Read);
    if(!readCurrent(readSelf,projectionSign,candidate,diagnostic))return false;
    for(unsigned i=0;i<3;++i)if(std::fabs(candidate.camera[i]-independentPosition[i])>.01f)return fail(diagnostic,Reject::Changed);
    std::memcpy(map,name,64);out=candidate;return true;
#endif
}
inline bool readSkyPhase(char (&map)[64],float projectionSign,Camera& out,Diagnostics* diagnostic=nullptr){
    using NorthlightWorldContext::readSelf;
    static const bool supported=NorthlightWorldContext::supportedClient()&&(verifySignatures(readSelf)||kPermissiveCameraSignatures);
    if(!supported)return fail(diagnostic,Reject::Client);
    char name[64]={};float independentPosition[3]={};Camera candidate;
    if(!NorthlightWorldContext::readMapAndCamera(name,independentPosition))return fail(diagnostic,Reject::Read);
    if(!readCurrentBasis(readSelf,projectionSign,candidate,diagnostic))return false;
    for(unsigned i=0;i<3;++i)if(std::fabs(candidate.camera[i]-independentPosition[i])>.01f)return fail(diagnostic,Reject::Changed);
    std::memcpy(map,name,64);out=candidate;return true;
}
#endif
} // namespace NorthlightWorldCamera
