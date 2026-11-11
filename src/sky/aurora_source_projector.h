#pragma once
#include "aurora_source.h"
#include <algorithm>
#include <vector>

namespace NorthlightAuroraSource {
struct Direction {float x=0,y=0,z=0;};
inline Direction subtract(Direction a,Direction b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Direction cross(Direction a,Direction b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float dot(Direction a,Direction b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct NativeVertex {
    // Live VS output rotated from native view space to world axes, relative to
    // the sky origin. Native UV/color interpolation is retained for the PS proof.
    Direction position;
    std::array<float,4> color{};
    std::array<float,2> uv[2]{};
};
struct NativeTriangle {NativeVertex vertices[3];unsigned material=0;int cull=0;};
inline bool intersect(Direction ray,const NativeTriangle& triangle,float& u,float& v){
    const auto a=triangle.vertices[0].position,e1=subtract(triangle.vertices[1].position,a),e2=subtract(triangle.vertices[2].position,a);
    // Native positive-x/y D3D projection: CW culling retains positive
    // origin-relative orientation. This tests the source face, not a viewport.
    if(triangle.cull&&dot(a,cross(e1,e2))*triangle.cull<=0)return false;
    const auto p=cross(ray,e2);const double determinant=dot(e1,p);
    if(std::fabs(determinant)<1e-12)return false;
    const auto from=Direction{-a.x,-a.y,-a.z};
    const double b=dot(from,p)/determinant;if(b<0||b>1)return false;
    const auto q=cross(from,e1);const double c=dot(ray,q)/determinant;
    if(c<0||b+c>1||dot(e2,q)/determinant<=0)return false;
    // Half-open shared edges: a ray exactly on a tessellation edge belongs
    // to one triangle, rather than doubling additive emission on that edge.
    const auto ordered=[](Direction first,Direction second){return first.x!=second.x?first.x<second.x:first.y!=second.y?first.y<second.y:first.z<second.z;};
    if(b==0&&!ordered(triangle.vertices[2].position,a))return false;
    if(c==0&&!ordered(a,triangle.vertices[1].position))return false;
    if(b+c==1&&!ordered(triangle.vertices[1].position,triangle.vertices[2].position))return false;
    u=float(b);v=float(c);return true;
}
inline NativeVertex interpolate(const NativeTriangle& triangle,float u,float v){
    NativeVertex out;const float weights[]={1-u-v,u,v};
    for(unsigned i=0;i<3;++i){const auto& vertex=triangle.vertices[i];
        for(unsigned c=0;c<4;++c)out.color[c]+=vertex.color[c]*weights[i];
        for(unsigned t=0;t<2;++t)for(unsigned c=0;c<2;++c)out.uv[t][c]+=vertex.uv[t][c]*weights[i];
    }
    return out;
}
// Project a complete, proven ADDITIVE native emitter onto the diffuse basis.
// shade returns linear emitted radiance including native alpha/blend factors.
// This routine belongs on the worker: no D3D resources or borrowed views escape.
// A caller must verify the full native model batch multiset before invoking it.
template<class Shade> bool project(const std::vector<NativeTriangle>& triangles,std::uint64_t selection,std::uint64_t revision,
                                  Shade shade,Snapshot& output,unsigned samplesPerAxis=8){
    output={};
    if(!selection||!revision||triangles.empty()||triangles.size()>8192||!samplesPerAxis||samplesPerAxis>16)return false;
    for(const auto& triangle:triangles)for(const auto& vertex:triangle.vertices){
        if(!std::isfinite(vertex.position.x)||!std::isfinite(vertex.position.y)||!std::isfinite(vertex.position.z))return false;
        for(float value:vertex.color)if(!std::isfinite(value))return false;
        for(const auto& uv:vertex.uv)for(float value:uv)if(!std::isfinite(value))return false;
    }
    Snapshot next;next.selection=selection;next.revision=revision;
    for(unsigned cell=0;cell<SectorCount;++cell){
        const unsigned azimuth=cell%AzimuthCount,height=cell/AzimuthCount;
        double total[3]{};
        for(unsigned y=0;y<samplesPerAxis;++y)for(unsigned x=0;x<samplesPerAxis;++x){
            const double z=(height+(y+.5)/samplesPerAxis)/HeightCount;
            const double angle=((azimuth+(x+.5)/samplesPerAxis)/AzimuthCount-.5)*2*double(Pi);
            const double radius=std::sqrt(1-z*z);
            const Direction ray{float(radius*std::sin(angle)),float(radius*std::cos(angle)),float(z)};
            for(const auto& triangle:triangles){float u=0,v=0;if(!intersect(ray,triangle,u,v))continue;
                RGB radiance{};if(!shade(triangle.material,interpolate(triangle,u,v),radiance))return false;
                for(unsigned c=0;c<3;++c){if(!std::isfinite(radiance[c])||radiance[c]<0)return false;total[c]+=radiance[c];}
            }
        }
        for(unsigned c=0;c<3;++c)next.radiance[cell][c]=float(total[c]/(samplesPerAxis*samplesPerAxis));
    }
    next.complete=true;if(!valid(next))return false;output=next;return true;
}
}
