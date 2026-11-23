#pragma once
#include "world_local_lights.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NorthlightPointShadow {
// True only when an entire world AABB is outside one homogeneous D3D clip
// plane. Perspective w is retained; no corner division or positive-w guess.
// Unknown/invalid inputs fail open. Double CPU arithmetic plus a conservative
// float dot-product cancellation bound covers the GPU's float clip transform.
namespace detail {
inline __attribute__((always_inline)) bool clipRejectBounds(const float* lo,const float* hi,const double* extent,const float* matrix) {
    if(!matrix)return false;
    for(int i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    // Check every clip component before an early rejection, so a different
    // plane cannot hide an overflowing GPU transform component.
    for(int column=0;column<4;++column){
        double bound=std::fabs(double(matrix[12+column]));
        for(int row=0;row<3;++row)bound+=std::fabs(double(matrix[row*4+column]))*
            extent[row];
        if(bound>std::numeric_limits<float>::max()/32.)return false;
    }
    for(int plane=0;plane<6;++plane){
        // x+w,w-x,y+w,w-y,z,w-z all have inside >= 0.
        const int column=plane<4?plane/2:2;
        const double sign=(plane==1||plane==3||plane==5)?-1.:1.;
        const bool addW=plane!=4;
        double maximum=0,magnitude=0;
        for(int row=0;row<4;++row){
            const double a=matrix[row*4+column],w=addW?matrix[row*4+3]:0.;
            const double coefficient=sign*a+w;
            const double coord=row==3?1.:coefficient>=0?hi[row]:lo[row];
            const double maxCoord=row==3?1.:extent[row];
            maximum+=coefficient*coord;magnitude+=(std::fabs(a)+std::fabs(w))*maxCoord;
        }
        // Extreme finite inputs that could overflow GPU intermediates remain
        // uncullable, even if their ideal double expression would cancel.
        if(magnitude>std::numeric_limits<float>::max()/16.)return false;
        const double margin=32.*std::numeric_limits<float>::epsilon()*(magnitude+1.);
        if(maximum < -margin)return true;
    }
    return false;
}
inline bool bounds(const float* lo,const float* hi,double (&extent)[3]){
    if(!lo||!hi)return false;
    for(unsigned i=0;i<3;++i){if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])return false;
        extent[i]=std::max(std::fabs(double(lo[i])),std::fabs(double(hi[i])));}
    return true;
}
}
inline bool clipReject(const float* lo,const float* hi,const float* matrix){
    double extent[3];return detail::bounds(lo,hi,extent)&&detail::clipRejectBounds(lo,hi,extent,matrix);
}
inline bool faceMatrix(const float* p,float nearZ,float farZ,unsigned face,float* out,unsigned resolution=256) {
    if(!p||!out||face>=6||resolution<2||!std::isfinite(nearZ)||!std::isfinite(farZ)||nearZ<=0||farZ<=nearZ)return false;
    for(int i=0;i<3;++i)if(!std::isfinite(p[i]))return false;
    // D3DCUBEMAP_FACE_POSITIVE_X,...,NEGATIVE_Z. World can be Z up;
    // the cube convention is independent of the world's up direction.
    static const float forward[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    static const float right[6][3]={{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}};
    static const float up[6][3]={{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
    const float a=farZ/(farZ-nearZ),b=-nearZ*a,h=1.f/resolution;
    for(int i=0;i<16;++i)out[i]=0;
    for(int i=0;i<3;++i){
        out[i*4]=right[face][i]-h*forward[face][i];
        out[i*4+1]=up[face][i]+h*forward[face][i];
        out[i*4+2]=a*forward[face][i];out[i*4+3]=forward[face][i];
        for(int j=0;j<4;++j)out[12+j]-=p[i]*out[i*4+j];
    }
    out[14]+=b;return true;
}
// The sphere broad phase is evaluated once by the caller. Invalid/unknown
// bounds retain all six faces; projection uses the same conservative predicate
// as the previous per-face draw loop.
inline unsigned faceMask(const float* lo,const float* hi,bool valid,const float (&matrices)[6][16]){
    double extent[3];if(!valid||!detail::bounds(lo,hi,extent))return 63;
    unsigned mask=0;
    for(unsigned face=0;face<6;++face)if(!detail::clipRejectBounds(lo,hi,extent,matrices[face]))mask|=1u<<face;
    return mask;
}
// The cube matrices do not change between AABBs in one update. Prepare their
// double planes and conservative float-cancellation bounds once, without a heap allocation.
class FaceClip {
    struct Face {
        // Cube side planes depend on two world axes, near/far on one.
        // Store the support corners in row order to keep the scalar sum exact.
        struct SparsePlane {
            unsigned corner[2]={};
            double weight[2]={},offset=0;
        } sparse[6];
        bool sparseReady=false;
        double marginSlope=0,marginOffset=0;
        bool finite=false;
        double absolute[4][4]={},coefficient[6][4]={},magnitude[6][4]={};
        void prepare(const float* matrix,double& maximum){
            for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return;
            for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row)
                absolute[column][row]=std::fabs(double(matrix[row*4+column]));
            // Independent reductions keep matrix preparation vectorizable.
            float peaks[4]={};
            for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)
                peaks[column]=std::max(peaks[column],std::fabs(matrix[row*4+column]));
            maximum=std::max(maximum,double(std::max({peaks[0],peaks[1],peaks[2],peaks[3]})));
            for(unsigned plane=0;plane<6;++plane){
                const unsigned column=plane<4?plane/2:2;
                const double sign=(plane==1||plane==3||plane==5)?-1.:1.;
                for(unsigned row=0;row<4;++row){
                    const double a=matrix[row*4+column],w=plane!=4?matrix[row*4+3]:0.;
                    coefficient[plane][row]=sign*a+w;magnitude[plane][row]=std::fabs(a)+std::fabs(w);
                }
            }
            finite=true;
            sparseReady=true;
            for(unsigned plane=0;plane<6;++plane){
                auto& out=sparse[plane];
                out.offset=coefficient[plane][3];
                unsigned used=0;
                double slope=0;
                for(unsigned axis=0;axis<3;++axis){
                    slope+=magnitude[plane][axis];
                    const double c=coefficient[plane][axis];
                    if(c==0)continue;
                    if(used==2){sparseReady=false;break;}
                    out.corner[used]=2*axis+unsigned(c>0);
                    out.weight[used++]=c;
                }
                marginSlope=std::max(marginSlope,slope);
                marginOffset=std::max(marginOffset,magnitude[plane][3]);
            }
        }
        bool rejectSparse(const float* corners,double maxExtent,const float* lo,const float* hi,const double* extent)const{
            // Twice the largest possible per-plane rounding margin. Only the
            // narrow band around a boundary needs the original plane-specific test.
            const double margin=64.*std::numeric_limits<float>::epsilon()*(marginSlope*maxExtent+marginOffset+1.);
            for(const auto& p:sparse){
                const double maximum=p.weight[0]*corners[p.corner[0]]+p.weight[1]*corners[p.corner[1]]+p.offset;
                if(maximum < -margin)return true;
                if(maximum < 0)return reject(lo,hi,extent,true);
            }
            return false;
        }
        bool reject(const float* lo,const float* hi,const double* extent,bool bounded)const{
            if(!finite)return false;
            if(!bounded)for(unsigned column=0;column<4;++column){
                double bound=absolute[column][3];
                for(unsigned row=0;row<3;++row)if(absolute[column][row]!=0)bound+=absolute[column][row]*extent[row];
                if(bound>std::numeric_limits<float>::max()/32.)return false;
            }
            for(unsigned plane=0;plane<6;++plane){
                double maximum=0,total=0;
                for(unsigned row=0;row<4;++row){
                    if(magnitude[plane][row]==0)continue;
                    const double c=coefficient[plane][row],coord=row==3?1.:c>=0?hi[row]:lo[row];
                    maximum+=c*coord;total+=magnitude[plane][row]*(row==3?1.:extent[row]);
                }
                if(!bounded&&total>std::numeric_limits<float>::max()/16.)return false;
                if(maximum < -32.*std::numeric_limits<float>::epsilon()*(total+1.))return true;
            }
            return false;
        }
    };
    Face faces[6];
    double safetyScale=0;
    double sphere[4]={};bool sphereValid=false;
    bool outsideSphere(const float* lo,const float* hi)const{
        double distance=0;
        for(unsigned axis=0;axis<3;++axis){
            const double delta=std::max({double(lo[axis])-sphere[axis],sphere[axis]-hi[axis],0.});
            distance+=delta*delta;
        }
        return distance>sphere[3];
    }
    unsigned planeMask(const float* lo,const float* hi,const double* extent,double maxExtent,unsigned requested)const{
        // One upper bound proves every component/plane is below its overflow
        // guard. The factor-two slack covers rounding; extreme inputs keep the
        // original individual guards and their fail-open masks.
        const bool bounded=safetyScale*maxExtent<=std::numeric_limits<float>::max()/64.;
        const float corners[]={lo[0],hi[0],lo[1],hi[1],lo[2],hi[2]};
        unsigned mask=requested;
        for(unsigned remaining=requested;remaining;remaining&=remaining-1){
            const unsigned index=unsigned(__builtin_ctz(remaining));
            const auto& face=faces[index];
            const bool outside=bounded&&face.sparseReady
                ?face.rejectSparse(corners,maxExtent,lo,hi,extent)
                :face.reject(lo,hi,extent,bounded);
            if(outside)mask&=~(1u<<index);
        }
        return mask;
    }
public:
    // A world-space box stays fixed across all light projections in a frame.
    class PreparedBounds {
        friend class FaceClip;
        float low_[3]={},high_[3]={};
        double extent_[3]={},maxExtent_=1.;
        bool valid_=false;
    public:
        PreparedBounds(const float* lo,const float* hi,bool known){
            valid_=known&&detail::bounds(lo,hi,extent_);
            if(!valid_)return;
            std::copy_n(lo,3,low_);
            std::copy_n(hi,3,high_);
            maxExtent_=std::max({1.,extent_[0],extent_[1],extent_[2]});
        }
    };
    explicit FaceClip(const float (&matrices)[6][16],unsigned requested=63){
        for(unsigned face=0;face<6;++face)if(requested&(1u<<face))faces[face].prepare(matrices[face],safetyScale);
        safetyScale*=8; // Four coordinates; each clip-plane term combines at most two matrix coefficients.
    }
    FaceClip(const float (&matrices)[6][16],const NorthlightLocalLights::Light& light,unsigned requested=63):FaceClip(matrices,requested){
        sphereValid=std::isfinite(light.attenuationEnd)&&light.attenuationEnd>=0;
        sphere[3]=double(light.attenuationEnd)*light.attenuationEnd;
        for(unsigned axis=0;axis<3;++axis){sphere[axis]=light.position[axis];sphereValid=sphereValid&&std::isfinite(sphere[axis]);}
    }
    unsigned mask(const PreparedBounds& bounds,unsigned requested=63)const{
        requested&=63;
        if(!requested||!bounds.valid_)return requested;
        if(sphereValid&&outsideSphere(bounds.low_,bounds.high_))return 0;
        return planeMask(bounds.low_,bounds.high_,bounds.extent_,bounds.maxExtent_,requested);
    }
    unsigned mask(const float* lo,const float* hi,bool valid,unsigned requested=63)const{
        requested&=63;if(!requested)return 0;
        double extent[3];if(!valid)return requested;
        if(sphereValid){if(!lo||!hi)return requested;double distance=0;
            for(unsigned axis=0;axis<3;++axis){
                if(!std::isfinite(lo[axis])||!std::isfinite(hi[axis])||lo[axis]>hi[axis])return requested;
                const double delta=std::max({double(lo[axis])-sphere[axis],sphere[axis]-hi[axis],0.});distance+=delta*delta;}
            if(distance>sphere[3])return 0;
            for(unsigned axis=0;axis<3;++axis)extent[axis]=std::max(std::fabs(double(lo[axis])),std::fabs(double(hi[axis])));
        }else if(!detail::bounds(lo,hi,extent))return requested;
        const double maxExtent=std::max({1.,extent[0],extent[1],extent[2]});
        return planeMask(lo,hi,extent,maxExtent,requested);
    }
};
}
