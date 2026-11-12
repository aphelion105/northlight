#pragma once
#include "shadow_bounds.h"
#include <cmath>
namespace StaticShadow {
// A replay may be omitted only if its ENTIRE proven geometry was inside the
// cached map's original clipping volume. Intersection alone is insufficient:
// cacheOffset can shift depth while an earlier caster was near-plane clipped.
inline bool containsBounds(const float* matrix,NorthlightGI::Vec3 low,NorthlightGI::Vec3 high){
    if(!matrix||low.x>high.x||low.y>high.y||low.z>high.z)return false;
    for(unsigned c=0;c<8;++c){
        const float x=c&1?high.x:low.x,y=c&2?high.y:low.y,z=c&4?high.z:low.z;
        float q[4];for(unsigned i=0;i<4;++i)q[i]=x*matrix[i]+y*matrix[4+i]+z*matrix[8+i]+matrix[12+i];
        for(float v:q)if(!std::isfinite(v))return false;
        if(q[3]<=0||q[0]<-q[3]||q[0]>q[3]||q[1]<-q[3]||q[1]>q[3]||q[2]<0||q[2]>q[3])return false;
        // Most outside boxes already failed the first corner. Inside affine
        // boxes need no seven remaining projections unless a plane is close.
        if(!c&&NorthlightShadowBounds::affineLightMatrix(matrix)){
            const float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};bool boundary=false;
            for(float v:hi)if(!std::isfinite(v))return false;
            for(unsigned axis=0;axis<3;++axis){
                double minimum=matrix[12+axis],maximum=minimum,magnitude=std::fabs(minimum);
                for(unsigned row=0;row<3;++row){const double a=double(lo[row])*matrix[row*4+axis],b=double(hi[row])*matrix[row*4+axis];
                    minimum+=std::min(a,b);maximum+=std::max(a,b);magnitude+=std::max(std::fabs(a),std::fabs(b));}
                if(magnitude>std::numeric_limits<float>::max()/32.){boundary=true;break;}
                const double margin=32.*std::numeric_limits<float>::epsilon()*(magnitude+1.),plane=axis<2?-1.:0.;
                if(minimum+margin<plane||maximum-margin>1)return false;
                boundary|=minimum-margin<plane||maximum+margin>1;
            }
            if(!boundary)return true;
        }
    }
    return true;
}
}
