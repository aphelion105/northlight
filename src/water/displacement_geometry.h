#pragma once
#include "actor_deformation.h"
#include <unordered_map>

namespace NorthlightDisplacementGeometry {
using Mesh=NorthlightDrawSnapshot::Mesh;
using Four=NorthlightActorDeformation::Four;
struct Height {
    unsigned width=0,height=0;std::vector<float> values;
    float sample(float u,float v,bool clampU=false,bool clampV=false)const{
        if(!width||!height||values.size()!=size_t(width)*height||!std::isfinite(u)||!std::isfinite(v))return .5f;
        const float x=(clampU?std::clamp(u,0.f,1.f):u-std::floor(u))*width-.5f;
        const float y=(clampV?std::clamp(v,0.f,1.f):v-std::floor(v))*height-.5f;
        const int ix=int(std::floor(x)),iy=int(std::floor(y));
        auto at=[&](int a,int b){a=clampU?std::clamp(a,0,int(width)-1):(a%int(width)+int(width))%int(width);b=clampV?std::clamp(b,0,int(height)-1):(b%int(height)+int(height))%int(height);return values[size_t(b)*width+a];};
        return (at(ix,iy)*(1-(x-ix))+at(ix+1,iy)*(x-ix))*(1-(y-iy))+(at(ix,iy+1)*(1-(x-ix))+at(ix+1,iy+1)*(x-ix))*(y-iy);
    }
};
inline bool encode(unsigned char* p,unsigned type,const Four& v){
    if(type<=3){std::memcpy(p,v.data(),(type+1)*4);return true;}
    auto rounded=[](float x,float low,float high){return std::lround(std::clamp(x,low,high));};
    if(type==4){for(unsigned i=0;i<4;++i)p[i]=uint8_t(rounded(v[i==0?2:i==2?0:i]*255,0,255));return true;}
    if(type==5||type==8){for(unsigned i=0;i<4;++i)p[i]=uint8_t(rounded(v[i]*(type==8?255:1),0,255));return true;}
    if(type==6||type==7||type==9||type==10){for(unsigned i=0;i<((type==6||type==9)?2u:4u);++i){int16_t x=int16_t(rounded(v[i]*(type>=9?32767:1),-32767,32767));std::memcpy(p+i*2,&x,2);}return true;}
    if(type==11||type==12){for(unsigned i=0;i<(type==11?2u:4u);++i){uint16_t x=uint16_t(rounded(v[i]*65535,0,65535));std::memcpy(p+i*2,&x,2);}return true;}
    return false;
}
struct Key {std::array<uint32_t,6> v;bool operator==(const Key& b)const{return v==b.v;}};
struct Hash {size_t operator()(const Key& k)const{size_t h=2166136261u;for(auto x:k.v)h=(h^x)*16777619u;return h;}};
inline uint64_t edge(unsigned a,unsigned b){return (uint64_t(std::min(a,b))<<32)|std::max(a,b);}
// One subdivision factor per draw makes shared edges conform. Boundary vertices
// stay on the original surface, including authored UV/material/hard-normal seams.
inline bool build(const Mesh& in,const D3DVERTEXELEMENT9* elements,UINT elementCount,
    const std::vector<std::array<float,2>>& uv,const Height& first,unsigned factor,bool terrain,
    bool clampU,bool clampV,Mesh& out,const Height* second=nullptr,
    const std::vector<std::array<float,2>>* uv1=nullptr,const std::vector<float>* blend=nullptr,unsigned influences=4){
    out={};if(influences>4)return false;const auto stride=in.streams[0].stride;
    if(!in.vertexCount||in.vertexCount>32768||stride>240||uv.size()!=in.vertexCount||factor<2||factor>32||!elements||elementCount>MAXD3DDECLLENGTH)return false;
    if(in.streams[0].bytes.size()<size_t(in.vertexCount)*stride)return false;
    if((second&&(!uv1||!blend||uv1->size()!=in.vertexCount||blend->size()!=in.vertexCount)))return false;
    for(auto value:uv)if(!std::isfinite(value[0])||!std::isfinite(value[1]))return false;
    const D3DVERTEXELEMENT9 *pos=nullptr,*normal=nullptr,*weights=nullptr,*bones=nullptr;
    for(UINT i=0;i<elementCount;++i){const auto& e=elements[i];if(e.Stream==0xff)break;
        if(e.Stream||e.Method!=D3DDECLMETHOD_DEFAULT||e.Type>12||e.Offset+NorthlightDrawSnapshot::declarationBytes(e.Type)>stride)return false;
        if(e.Usage==D3DDECLUSAGE_POSITION&&e.UsageIndex==0)pos=&e;
        if(e.Usage==D3DDECLUSAGE_NORMAL&&e.UsageIndex==0)normal=&e;
        if(e.Usage==D3DDECLUSAGE_BLENDWEIGHT)weights=&e;
        if(e.Usage==D3DDECLUSAGE_BLENDINDICES)bones=&e;
        if(e.Usage==D3DDECLUSAGE_TEXCOORD&&e.UsageIndex==7)return false;
    }
    if(!pos||(pos->Type!=D3DDECLTYPE_FLOAT3&&pos->Type!=D3DDECLTYPE_FLOAT4))return false;
    if(weights&&!bones)return false;
    std::vector<std::array<unsigned,3>> triangles;
    auto index=[&](unsigned i){return in.indexed?(i<in.indices.size()?in.indices[i]:UINT_MAX):i;};
    if(in.topology!=D3DPT_TRIANGLELIST&&in.topology!=D3DPT_TRIANGLESTRIP)return false;
    for(unsigned i=0;i<in.primitiveCount;++i){unsigned at=in.topology==D3DPT_TRIANGLELIST?i*3:i;std::array<unsigned,3> t={index(at),index(at+1),index(at+2)};if(in.topology==D3DPT_TRIANGLESTRIP&&(i&1))std::swap(t[0],t[1]);for(auto a:t)if(a>=in.vertexCount)return false;if(t[0]!=t[1]&&t[1]!=t[2]&&t[2]!=t[0])triangles.push_back(t);}
    if(triangles.empty())return false;
    std::vector<Four> faceNormals;if(!normal&&!terrain){faceNormals.resize(in.vertexCount);
        for(auto t:triangles){Four p[3];for(unsigned k=0;k<3;++k)if(!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+pos->Offset,pos->Type,p[k]))return false;
            float a[3],b[3];for(unsigned k=0;k<3;++k){a[k]=p[1][k]-p[0][k];b[k]=p[2][k]-p[0][k];}Four n={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0],0};for(auto i:t)for(unsigned k=0;k<3;++k)faceNormals[i][k]+=n[k];}}

    while(factor>2&&triangles.size()*factor*factor>32768)factor/=2;
    if(triangles.size()*factor*factor>32768)return false;
    std::unordered_map<uint64_t,unsigned> edges;for(auto t:triangles)for(unsigned i=0;i<3;++i)++edges[edge(t[i],t[(i+1)%3])];
    std::vector<bool> boundary(in.vertexCount,false);for(auto e:edges)if(e.second!=2){boundary[unsigned(e.first>>32)]=true;boundary[unsigned(e.first)]=true;}
    out.streams[0].stride=stride+12;out.topology=D3DPT_TRIANGLELIST;out.indexed=true;
    std::unordered_map<Key,unsigned,Hash> vertices;
    auto vertex=[&](const std::array<unsigned,3>& t,unsigned i,unsigned j)->unsigned{
        const unsigned w[3]={factor-i-j,i,j};std::array<std::pair<unsigned,unsigned>,3> sorted;
        for(unsigned k=0;k<3;++k)sorted[k]={w[k]?t[k]:UINT_MAX,w[k]};std::sort(sorted.begin(),sorted.end());
        Key key;for(unsigned k=0;k<3;++k){key.v[k*2]=sorted[k].first;key.v[k*2+1]=sorted[k].second;}
        auto found=vertices.find(key);if(found!=vertices.end())return found->second;
        if(out.vertexCount>=32768)return UINT_MAX;
        const unsigned n=out.vertexCount++;vertices.emplace(key,n);auto& bytes=out.streams[0].bytes;bytes.resize(size_t(out.vertexCount)*(stride+12));auto* dst=bytes.data()+size_t(n)*(stride+12);
        Four direction={0,0,1,0};
        for(UINT e=0;e<elementCount&&elements[e].Stream!=0xff;++e){const auto& element=elements[e];if(&element==weights||&element==bones)continue;Four value={};
            for(unsigned k=0;k<3;++k){Four a;NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+element.Offset,element.Type,a);for(unsigned c=0;c<4;++c)value[c]+=a[c]*(float(w[k])/factor);}
            for(float v:value)if(!std::isfinite(v))return UINT_MAX;
            if(!encode(dst+element.Offset,element.Type,value))return UINT_MAX;
            if(&element==normal&&!terrain)direction=value;
        }
        if(!normal&&!terrain){direction={};for(unsigned k=0;k<3;++k)for(unsigned c=0;c<3;++c)direction[c]+=faceNormals[t[k]][c]*(float(w[k])/factor);}
        if(bones&&!weights){unsigned k=w[1]>w[0]?1:0;if(w[2]>w[k])k=2;Four ids;if(!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+bones->Offset,bones->Type,ids)||!encode(dst+bones->Offset,bones->Type,ids))return UINT_MAX;}
        if(!influences){unsigned k=w[1]>w[0]?1:0;if(w[2]>w[k])k=2;for(auto* e:{weights,bones})if(e)std::memcpy(dst+e->Offset,in.streams[0].bytes.data()+size_t(t[k])*stride+e->Offset,NorthlightDrawSnapshot::declarationBytes(e->Type));}
        if(influences&&weights&&bones){unsigned ids[12]={},used=0;float merged[12]={};
            for(unsigned k=0;k<3;++k){Four ws,bs;if(!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+weights->Offset,weights->Type,ws)||!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+bones->Offset,bones->Type,bs))return UINT_MAX;
                for(unsigned c=0;c<influences;++c){if(!std::isfinite(ws[c])||!std::isfinite(bs[c])||bs[c]<0||bs[c]>255||std::fabs(bs[c]-std::round(bs[c]))>.001f||ws[c]<0)return UINT_MAX;const unsigned id=unsigned(std::lround(bs[c]));unsigned j=0;while(j<used&&ids[j]!=id)++j;if(j==used)ids[used++]=id;merged[j]+=ws[c]*(float(w[k])/factor);}}
            for(unsigned a=0;a<std::min(used,influences);++a)for(unsigned b=a+1;b<used;++b)if(merged[b]>merged[a]){std::swap(merged[a],merged[b]);std::swap(ids[a],ids[b]);}
            Four ws={},bs={};float total=0;for(unsigned a=0;a<std::min(used,influences);++a)total+=merged[a];if(total<=1e-8f)return UINT_MAX;
            for(unsigned a=0;a<std::min(used,influences);++a){ws[a]=merged[a]/total;bs[a]=float(ids[a]);}if(!encode(dst+weights->Offset,weights->Type,ws)||!encode(dst+bones->Offset,bones->Type,bs))return UINT_MAX;
            if(weights->Type==D3DDECLTYPE_UBYTE4N){unsigned sum=0;float fraction[4];for(unsigned a=0;a<4;++a){unsigned n=unsigned(std::floor(ws[a]*255));dst[weights->Offset+a]=uint8_t(n);sum+=n;fraction[a]=ws[a]*255-n;}
                while(sum<255){unsigned a=unsigned(std::max_element(fraction,fraction+4)-fraction);++dst[weights->Offset+a];fraction[a]=-1;++sum;}}
        }
        bool pinned=false;for(unsigned k=0;k<3;++k){if(w[k]==factor&&boundary[t[k]])pinned=true;if(!w[k]&&edges[edge(t[(k+1)%3],t[(k+2)%3])]!=2)pinned=true;}
        float u=0,v=0,u1=0,v1=0,b=0;for(unsigned k=0;k<3;++k){float weight=float(w[k])/factor;u+=uv[t[k]][0]*weight;v+=uv[t[k]][1]*weight;if(uv1){u1+=(*uv1)[t[k]][0]*weight;v1+=(*uv1)[t[k]][1]*weight;}if(blend)b+=(*blend)[t[k]]*weight;}
        float h=first.sample(u,v,clampU,clampV);if(second&&uv1&&blend)h=second->sample(u1,v1,clampU,clampV)*(1-std::clamp(b,0.f,1.f))+h*std::clamp(b,0.f,1.f);
        const float length=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);if(!std::isfinite(length)||length<1e-5f)return UINT_MAX;
        float delta[3];for(unsigned k=0;k<3;++k)delta[k]=pinned?0:(h-.5f)*direction[k]/length;std::memcpy(dst+stride,delta,12);return n;
    };
    for(auto t:triangles){std::vector<std::vector<unsigned>> rows(factor+1);for(unsigned i=0;i<=factor;++i)for(unsigned j=0;j<=factor-i;++j){unsigned v=vertex(t,i,j);if(v==UINT_MAX){out={};return false;}rows[i].push_back(v);}
        for(unsigned i=0;i<factor;++i)for(unsigned j=0;j<factor-i;++j){out.indices.insert(out.indices.end(),{rows[i][j],rows[i+1][j],rows[i][j+1]});if(j+1<factor-i)out.indices.insert(out.indices.end(),{rows[i+1][j],rows[i+1][j+1],rows[i][j+1]});}}
    out.primitiveCount=unsigned(out.indices.size()/3);return true;
}
} // namespace NorthlightDisplacementGeometry
