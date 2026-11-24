#pragma once
#include "replay_bounds.h"
#include "northlight_mem.h"

// Converts world-axis model bounds back to the current view for range decisions.
class NorthlightMaterialViewBounds {
    using Bounds=NorthlightReplayBounds::Bounds;
    float inverse_[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    double view_[9]={1,0,0,0,1,0,0,0,1};
    bool known_=false;
public:
    bool known()const{return known_;}
    const auto& inverse()const{return inverse_;}
    bool operator==(const NorthlightMaterialViewBounds& other)const{
        return known_==other.known_&&
            !std::memcmp(inverse_,other.inverse_,sizeof inverse_)&&
            (!known_||!std::memcmp(view_,other.view_,sizeof view_));
    }
    void set(const float* rows){
        // Draws in the same view share the already validated rotation and inverse.
        if(rows&&known_&&!NorthlightMem::compare(rows,inverse_,12)&&
           !NorthlightMem::compare(rows+4,inverse_+4,12)&&!NorthlightMem::compare(rows+8,inverse_+8,12))return;
        known_=false;
        std::fill(std::begin(inverse_),std::end(inverse_),0.f);
        for(unsigned i=0;i<4;++i)inverse_[i*5]=1;
        if(!rows)return;
        for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k)
            if(!std::isfinite(rows[r*4+k]))return;
        for(unsigned r=0;r<3;++r)for(unsigned k=r;k<3;++k){
            double dot=0;
            for(unsigned j=0;j<3;++j)dot+=double(rows[r*4+j])*rows[k*4+j];
            if(std::fabs(dot-(r==k?1.:0.))>.0001)return;
        }
        // The supplied float basis is only approximately orthogonal.
        const double determinant=double(rows[0])*(double(rows[5])*rows[10]-double(rows[6])*rows[9])
            -double(rows[1])*(double(rows[4])*rows[10]-double(rows[6])*rows[8])
            +double(rows[2])*(double(rows[4])*rows[9]-double(rows[5])*rows[8]);
        if(!std::isfinite(determinant)||std::fabs(determinant)<.5)return;
        for(unsigned r=0;r<3;++r){
            const float* a=rows+4*((r+1)%3);
            const float* b=rows+4*((r+2)%3);
            for(unsigned k=0;k<3;++k){
                const unsigned i=(k+1)%3,j=(k+2)%3;
                view_[r*3+k]=(double(a[i])*b[j]-double(a[j])*b[i])/determinant;
            }
            std::copy_n(rows+r*4,3,inverse_+r*4);
        }
        known_=true;
    }
    Bounds inView(const Bounds& world)const{
        if(!known_||!world.valid)return world;
        Bounds view;
        view.valid=true;
        for(unsigned r=0;r<3;++r){
            double low=0,high=0,magnitude=0;
            for(unsigned k=0;k<3;++k){
                const double a=view_[r*3+k]*world.low[k],b=view_[r*3+k]*world.high[k];
                low+=std::min(a,b);
                high+=std::max(a,b);
                magnitude+=std::max(std::fabs(a),std::fabs(b));
            }
            const double error=(magnitude+1)*32*std::numeric_limits<float>::epsilon();
            view.low[r]=NorthlightReplayBounds::detail::outwardNormal<false>(float(low-error));
            view.high[r]=NorthlightReplayBounds::detail::outwardNormal<true>(float(high+error));
        }
        return view;
    }
    // Stock M2's clip.w is signed view Z. Projection of a convex box with
    // positive W attains its screen extrema at its corners. A guard band
    // covers shader round-off and the raster footprint at the threshold.
    static bool fitsScreen(const Bounds& view,const float* clip,double aspect,float pixels,float protectDepth){
        if(!view.valid||!clip||!(aspect>0&&aspect<8)||!(pixels>2)||
           clip[12]!=0||clip[13]!=0||clip[15]!=0||(clip[14]!=1&&clip[14]!=-1))return false;
        for(unsigned i=0;i<3;++i)
            if(!std::isfinite(view.low[i])||!std::isfinite(view.high[i])||view.low[i]>view.high[i])return false;
        if((clip[14]>0?view.low[2]:-view.high[2])<=std::max(1.f,protectDepth)+.01f)return false;
        for(unsigned i=0;i<12;++i)if(!std::isfinite(clip[i])||std::fabs(clip[i])>1000000)return false;
        double low[2]={INFINITY,INFINITY},high[2]={-INFINITY,-INFINITY};
        for(unsigned corner=0;corner<8;++corner){
            const double x=corner&1?view.high[0]:view.low[0],y=corner&2?view.high[1]:view.low[1],z=corner&4?view.high[2]:view.low[2];
            const double w=z*clip[14];
            const double nearPlane=clip[8]*x+clip[9]*y+clip[10]*z+clip[11];
            if(nearPlane<=.01)return false;
            for(unsigned axis=0;axis<2;++axis){
                const float* row=clip+4*axis;
                const double a=row[0]*x,b=row[1]*y,c=row[2]*z,d=row[3];
                const double ndc=(a+b+c+d)/w;
                const double margin=32.*std::numeric_limits<float>::epsilon()*(std::fabs(a)+std::fabs(b)+std::fabs(c)+std::fabs(d)+1.)/w;
                low[axis]=std::min(low[axis],ndc-margin);high[axis]=std::max(high[axis],ndc+margin);
            }
        }
        return std::max((high[0]-low[0])*aspect,high[1]-low[1])*540.+2.<pixels;
    }
    static float nearest(const Bounds& view){
        const float lo=view.low[2],hi=view.high[2];
        const float distance=lo>0?lo:hi<0?-hi:0;
        return std::max(0.f,NorthlightReplayBounds::detail::outwardNormal<false>(distance)-.001f);
    }
    static float farthest(const Bounds& view){
        const float distance=std::max(std::fabs(view.low[2]),std::fabs(view.high[2]));
        return NorthlightReplayBounds::detail::outwardNormal<true>(distance)+.001f;
    }
    static bool belowDisplacementSize(double x,double y,double z){
        // The box diagonal is an upper bound on the section's diameter.
        constexpr double minimumDiameter=.5;
        return x*x+y*y+z*z<minimumDiameter*minimumDiameter;
    }
    struct DisplacementLod {
        float range;
        bool small=false,outOfRange=false;

        template<class T> void classify(const T* low,const T* high){
            double extent[3],distanceSquared=0;
            for(unsigned axis=0;axis<3;++axis){
                const double lower=low[axis],upper=high[axis];
                if(!std::isfinite(lower)||!std::isfinite(upper)||lower>upper)return;
                extent[axis]=upper-lower;
                const double nearest=std::max({lower,-upper,0.});
                distanceSquared+=nearest*nearest;
            }
            small=belowDisplacementSize(extent[0],extent[1],extent[2]);
            // Leave borderline draws to the exact displacement path.
            const double limit=double(range)*1.0002+.001;
            outOfRange=distanceSquared>limit*limit;
        }
    };
};
