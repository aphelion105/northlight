#pragma once
// Optional local import of Forever DB2 data. No client memory, filesystem work
// or allocations on the frame path. Heights/positions are in game yards.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>

namespace NorthlightForever {
struct Layer {float color[4][3]={};float p[24]={};float flag[4]={};};
struct Key {uint32_t param=0,time=0,id=0,reserved=0;float direct[3]={},ambient[3]={};Layer layer[3];};
struct Light {uint32_t id=0,map=0;float position[3]={},start=0,end=0;uint32_t params[8]={};};
struct Zone {uint32_t map=0,light=0,first=0,count=0;float low=0,high=0;};
struct Point {float x=0,y=0;};
struct SkyKey {float color[8][3]={};}; // top, middle, band1, band2, smog, fog, sun, cloud sun
struct Frame {Layer layer[3];float direct[3]={},ambient[3]={};SkyKey sky;bool valid=false,skyValid=false;};
static_assert(sizeof(Layer)==160&&sizeof(Key)==520&&sizeof(Light)==60&&sizeof(Zone)==24,"NFA1 disk layout");
inline void add(Frame& out,const Frame& in,float w){
    if(!in.valid||w<=0)return;out.valid=true;
    if(in.skyValid){out.skyValid=true;for(unsigned i=0;i<8;++i)for(unsigned c=0;c<3;++c)out.sky.color[i][c]+=in.sky.color[i][c]*w;}
    for(unsigned c=0;c<3;++c){out.direct[c]+=in.direct[c]*w;out.ambient[c]+=in.ambient[c]*w;}
    for(unsigned i=0;i<3;++i){auto& a=out.layer[i];const auto& b=in.layer[i];
        const float before=a.flag[3],added=b.flag[3]*w,total=before+added;
        if(added<=0)continue;
        for(unsigned c=0;c<4;++c)for(unsigned j=0;j<3;++j)a.color[c][j]=(a.color[c][j]*before+b.color[c][j]*added)/total;
        for(unsigned j=0;j<24;++j)if(j!=1)a.p[j]=(a.p[j]*before+b.p[j]*added)/total;
        // Density carries presence once; an absent layer must not dilute its
        // colour/shape as well, or a weather transition fades quadratically.
        a.p[1]+=b.p[1]*w;
        for(unsigned j=0;j<3;++j)a.flag[j]=(a.flag[j]*before+b.flag[j]*added)/total;
        a.flag[3]=total;
    }
}
class Table {
public:
    std::vector<SkyKey> skies;
    std::vector<Light> lights;std::vector<Key> keys;std::vector<Zone> zones;std::vector<Point> points;
    bool load(const std::string& path){
        std::ifstream f(std::filesystem::u8path(path),std::ios::binary);uint32_t h[6]={};
        if(!f.read(reinterpret_cast<char*>(h),sizeof h)||h[0]!=0x3141464e||(h[1]!=1&&h[1]!=2)||h[2]>8192||h[3]>32768||h[4]>8192||h[5]>131072)return false;
        Table t;t.lights.resize(h[2]);t.keys.resize(h[3]);t.zones.resize(h[4]);t.points.resize(h[5]);
        auto read=[&](auto& v){return bool(f.read(reinterpret_cast<char*>(v.data()),std::streamsize(v.size()*sizeof(v[0]))));};
        if(!read(t.lights)||!read(t.keys)||!read(t.zones)||!read(t.points))return false;
        if(h[1]==2){t.skies.resize(h[3]);if(!read(t.skies))return false;}
        if(f.peek()!=EOF)return false;
        for(const auto& s:t.skies)for(const auto& color:s.color)for(float v:color)if(!std::isfinite(v)||v<0||v>1)return false;
        auto finite=[](float x){return std::isfinite(x)&&std::fabs(x)<=1000000;};
        for(const auto& l:t.lights){if(l.start<0||l.end<l.start||!finite(l.start)||!finite(l.end))return false;for(float v:l.position)if(!finite(v))return false;}
        for(size_t i=0;i<t.keys.size();++i){const auto& k=t.keys[i];if(k.time>=2880||!k.param||(i&&std::make_pair(t.keys[i-1].param,t.keys[i-1].time)>=std::make_pair(k.param,k.time)))return false;
            for(float v:k.direct)if(!finite(v)||v<0||v>1)return false;for(float v:k.ambient)if(!finite(v)||v<0||v>1)return false;
            for(const auto& l:k.layer){for(const auto& rgb:l.color)for(float v:rgb)if(!finite(v)||v<0||v>1)return false;for(float v:l.p)if(!finite(v))return false;for(float v:l.flag)if(v!=0&&v!=1)return false;if(l.p[1]<0)return false;}}
        for(const auto& z:t.zones)if(z.first>t.points.size()||z.count>t.points.size()-z.first||z.count<3||!finite(z.low)||!finite(z.high)||z.low>z.high)return false;
        for(const auto& p:t.points)if(!finite(p.x)||!finite(p.y))return false;
        *this=std::move(t);return true;
    }
    Frame at(uint32_t param,float time)const{
        Frame out;auto begin=std::lower_bound(keys.begin(),keys.end(),param,[](const Key& k,uint32_t p){return k.param<p;});
        auto end=std::upper_bound(begin,keys.end(),param,[](uint32_t p,const Key& k){return p<k.param;});if(begin==end)return out;
        auto next=std::upper_bound(begin,end,time,[](float t,const Key& k){return t<float(k.time);});auto prev=next==begin?end-1:next-1;if(next==end)next=begin;
        float a=float(prev->time),b=float(next->time);if(a>time)a-=2880;if(b<=a)b+=2880;if(b<time)b+=2880;
        float w=b>a?std::clamp((time-a)/(b-a),0.f,1.f):0;
        for(const auto pair:{std::make_pair(&*prev,1-w),std::make_pair(&*next,w)}){Frame v;v.valid=true;std::copy_n(pair.first->direct,3,v.direct);std::copy_n(pair.first->ambient,3,v.ambient);std::copy_n(pair.first->layer,3,v.layer);if(!skies.empty()){v.sky=skies[size_t(pair.first-keys.data())];v.skyValid=true;}add(out,v,pair.second);}return out;
    }
    Frame condition(const Light& l,float time,float storm)const{
        auto clear=at(l.params[0],time);auto wet=l.params[2]?at(l.params[2],time):clear;if(!wet.valid)wet=clear;
        Frame out;add(out,clear,1-storm);add(out,wet,storm);return out;
    }
    bool contains(const Zone& z,const float* p)const{
        if(p[2]<z.low||p[2]>z.high)return false;bool in=false;
        for(unsigned i=0,j=z.count-1;i<z.count;j=i++){const auto& a=points[z.first+i];const auto& b=points[z.first+j];
            if((a.y>p[1])!=(b.y>p[1])&&p[0]<(b.x-a.x)*(p[1]-a.y)/(b.y-a.y)+a.x)in=!in;}return in;
    }
    Frame sample(const std::string& map,const float* p,float day,float storm)const{
        Frame out;int id=map=="Azeroth"?0:map=="Kalimdor"?1:-1;
        if(id<0||!std::isfinite(day)||day<0||day>=1)return out;for(unsigned c=0;c<3;++c)if(!std::isfinite(p[c]))return out;
        storm=std::isfinite(storm)?std::clamp(storm,0.f,1.f):0;const Light* background=nullptr;
        for(const auto& l:lights)if(l.map==unsigned(id)&&l.end==0&&l.position[0]==0&&l.position[1]==0){background=&l;break;}
        for(const auto& z:zones)if(z.map==unsigned(id)&&contains(z,p))for(const auto& l:lights)if(l.id==z.light&&l.map==z.map){background=&l;break;}
        float sum=0;
        for(const auto& l:lights){if(l.map!=unsigned(id)||l.end<=0)continue;
            float ds=0;for(unsigned c=0;c<3;++c){float v=p[c]-l.position[c];ds+=v*v;}float d=std::sqrt(ds);
            float w=d<=l.start?1:l.end>l.start?std::clamp((l.end-d)/(l.end-l.start),0.f,1.f):0;
            if(w<=0)continue;auto v=condition(l,day*2880,storm);if(v.valid){add(out,v,w);sum+=w;}}
        if(sum>1){Frame normalized;add(normalized,out,1/sum);out=normalized;}
        else if(background)add(out,condition(*background,day*2880,storm),1-sum);
        return out;
    }
};
class Transition {
    Frame previous;std::string map;float position[3]={};double tick=0;
public:
    Frame update(const Frame& target,const std::string& nextMap,const float* p,double now){
        float distance=0;for(unsigned c=0;c<3;++c){float v=p[c]-position[c];distance+=v*v;}
        if(!target.valid||!previous.valid||nextMap!=map||distance>256*256||now<tick||now-tick>2)previous=target;
        else{Frame blended;float w=float(1-std::exp(-std::min(now-tick,.25)/.35));add(blended,previous,1-w);add(blended,target,w);previous=blended;}
        map=nextMap;std::copy_n(p,3,position);tick=now;return previous;
    }
};
// Units of authored density and height falloff are per 100 yards. Relative
// layers use the reference terrain elevation, not camera height.
inline float sigma(const Layer& l,float distance,float z,float ground){
    if(l.flag[3]<=0||distance<l.p[0])return 0;
    float height=z-ground*l.flag[1];
    float upper=std::min(0.f,(l.p[5]-height)*std::max(l.p[3],0.f)*.01f);
    float lower=std::min(0.f,(height-l.p[7])*std::max(l.p[6],0.f)*.01f);
    float curve=1+std::max(l.p[18],0.f)*std::pow(std::clamp((distance-l.p[0])/1000.f,0.f,1.f),std::max(l.p[19],.001f));
    return std::max(l.p[1],0.f)*.01f*std::exp(upper+lower)*curve;
}
inline float sigma(const Frame& f,float distance,float z,float ground){float result=0;for(const auto& l:f.layer)result+=sigma(l,distance,z,ground);return result;}
inline void constants(const Frame& frame,float out[38][4],float distance,float sourceWeight){
    std::fill_n(&out[0][0],38*4,0.f);out[1][3]=distance;out[11][3]=sourceWeight;
    float first=distance;
    for(unsigned i=0;i<3;++i){const auto& l=frame.layer[i];
        out[0][i]=l.flag[3]>0?std::max(l.p[1],0.f)*.01f:0;out[1][i]=std::max(l.p[0],0.f);
        if(out[0][i]>0)first=std::min(first,out[1][i]);
        out[2][i]=l.p[5];out[3][i]=std::max(l.p[3],0.f)*.01f;
        out[4][i]=l.p[7];out[5][i]=std::max(l.p[6],0.f)*.01f;
        out[6][i]=std::clamp(l.p[10],-.99f,.99f);out[7][i]=std::max(l.p[18],0.f);out[8][i]=std::max(l.p[19],.001f);
        out[9][i]=l.flag[0];out[10][i]=l.flag[1];out[11][i]=std::max(l.p[9],0.f);
        for(unsigned c=0;c<3;++c){
            // DB2 colours are display RGB; the HDR volume accumulates linear radiance.
            auto linear=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
            out[12+i][c]=linear(l.color[0][c]);out[15+i][c]=linear(l.color[1][c]);out[18+i][c]=linear(l.color[2][c]);out[35+i][c]=linear(l.color[3][c]);
            out[25+i][c]=l.p[11+c];out[28+i][c]=l.p[14+c];
        }
        out[24][i]=l.p[20]>0?1.f/(100*l.p[20]):0;out[31][i]=l.p[21]>0?1.f/(100*l.p[21]):0;
        float total=std::max(0.f,l.p[22])+std::max(0.f,l.p[23]);
        out[32][i]=total>0?std::max(0.f,l.p[22])/total:1;out[33][i]=total>0?std::max(0.f,l.p[23])/total:0;
        out[34][i]=l.flag[2];
    }
    out[34][3]=first; // Earliest active layer; shared by every ray and light source.
}
} // namespace NorthlightForever
