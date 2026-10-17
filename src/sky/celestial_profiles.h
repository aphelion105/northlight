#pragma once
// Client-local artistic palettes, selected from authored ADT zone IDs. Portable
// policy only: file loading at startup; region loading belongs to the worker.
#include "regional_fog.h"
#include "celestial_context.h"
#include <fstream>
#include <sstream>
#include <map>
#include <set>

namespace NorthlightCelestialProfiles {
struct Profile {
    float disc[2][3]={{1,.96f,.88f},{.94f,.97f,1}};
    float gain[2]={3.5f,1.1f};
    float light[2][3]={{1,1,1},{1,1,1}};
    float mix[2]={0,0},strength[2]={1,1};
    // Independent artistic volume controls. Identity defaults retain all
    // existing zones; neither control changes surface light or the sun disc.
    float fogGain[2]={1,1},fogHeightScale[2]={1,1};
    // Horizon haze optical-depth multiplier (horizon_haze.h), day/night. 1 = the global strength.
    float horizonHaze[2]={1,1};
};
inline Profile blend(const Profile& a,const Profile& b,float t){
    Profile p; t=std::clamp(t,0.f,1.f);
    for(unsigned s=0;s<2;++s){
        for(unsigned c=0;c<3;++c){p.disc[s][c]=a.disc[s][c]+(b.disc[s][c]-a.disc[s][c])*t;p.light[s][c]=a.light[s][c]+(b.light[s][c]-a.light[s][c])*t;}
        p.gain[s]=a.gain[s]+(b.gain[s]-a.gain[s])*t;
        p.mix[s]=a.mix[s]+(b.mix[s]-a.mix[s])*t;p.strength[s]=a.strength[s]+(b.strength[s]-a.strength[s])*t;
        p.fogGain[s]=a.fogGain[s]+(b.fogGain[s]-a.fogGain[s])*t;
        p.fogHeightScale[s]=a.fogHeightScale[s]+(b.fogHeightScale[s]-a.fogHeightScale[s])*t;
        p.horizonHaze[s]=a.horizonHaze[s]+(b.horizonHaze[s]-a.horizonHaze[s])*t;
    }return p;
}
inline float fogHeightScale(const Profile& p,float night){
    return p.fogHeightScale[0]+(p.fogHeightScale[1]-p.fogHeightScale[0])*std::clamp(night,0.f,1.f);
}
inline float horizonHaze(const Profile& p,float night){
    return p.horizonHaze[0]+(p.horizonHaze[1]-p.horizonHaze[0])*std::clamp(night,0.f,1.f);
}
inline std::string trim(const std::string& s){auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,b-a+1);}
struct Table {
    Profile fallback;
    std::map<std::pair<std::string,uint32_t>,Profile> zones;
    // Keys each zone section set itself (the rest were copied from [default]); lets an overlay move [default] under them.
    std::map<std::pair<std::string,uint32_t>,std::set<std::string>> explicitKeys;
    const Profile& at(const std::string& map,uint32_t zone)const {auto i=zones.find({map,zone});return i==zones.end()?fallback:i->second;}
};
// One "sun_/moon_<name> = value" assignment with its range checks; nullptr on success, else the reason.
// Shared by the ini parser and the dev tuning overlay (zone_tuning.h).
inline const char* assignKey(Profile& profile,const std::string& key,std::string value){
    Profile* p=&profile;
    unsigned s;if(key.rfind("sun_",0)==0)s=0;else if(key.rfind("moon_",0)==0)s=1;else return "unknown key";
    auto name=key.substr(s?5:4);float* target=nullptr;unsigned count=1;float maximum=1;
    if(name=="disc"){target=p->disc[s];count=3;}
    else if(name=="disc_gain"){target=&p->gain[s];maximum=4;}
    else if(name=="light"){target=p->light[s];count=3;}
    else if(name=="light_mix")target=&p->mix[s];
    else if(name=="light_strength"){target=&p->strength[s];maximum=4;}
    else if(name=="fog_gain"){target=&p->fogGain[s];maximum=8;}
    else if(name=="fog_height_scale"){target=&p->fogHeightScale[s];maximum=4;}
    else if(name=="horizon_haze"){target=&p->horizonHaze[s];maximum=4;}
    else return "unknown key";
    for(char& c:value)if(c==',')c=' ';std::istringstream values(value);
    for(unsigned i=0;i<count;++i)if(!(values>>target[i])||!std::isfinite(target[i])||target[i]<0||target[i]>maximum)return "invalid numeric value";
    std::string extra;if(values>>extra)return "too many values";
    if(name=="light"&&target[0]+target[1]+target[2]<.01f)return "light hue cannot be black; use light_strength=0";
    if(name=="fog_height_scale"&&target[0]<.25f)return "fog height scale must be at least .25";
    return nullptr;
}
// Atomic validation: a malformed file cannot install half a palette. A zone
// inherits [default], which must precede zone sections. Duplicate keys reject.
inline bool parse(std::istream& input,Table& out,std::string& error){
    Table t;Profile* p=nullptr;std::set<std::string>* explicitSet=nullptr;std::string line;unsigned number=0;bool defaultSeen=false;
    std::map<std::string,bool> keys;
    auto fail=[&](const char* why){error="line "+std::to_string(number)+": "+why;return false;};
    while(std::getline(input,line)){
        ++number;if(number>8192)return fail("file too large");line=trim(line.substr(0,line.find('#')));if(line.empty())continue;
        if(line.front()=='['&&line.back()==']'){
            std::string section=trim(line.substr(1,line.size()-2));keys.clear();
            if(section=="default") {if(defaultSeen||!t.zones.empty())return fail("default must appear once, before zones");defaultSeen=true;p=&t.fallback;explicitSet=nullptr;continue;}
            auto colon=section.find(':');if(colon==std::string::npos)return fail("expected [Map:ZoneID]");
            std::string map=trim(section.substr(0,colon)),id=trim(section.substr(colon+1));
            if(map!="Azeroth"&&map!="Kalimdor"&&map!="Expansion01"&&map!="Northrend")return fail("unsupported map");
            if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos||id.size()>6)return fail("invalid zone ID");
            uint32_t zone=uint32_t(std::stoul(id));if(!zone)return fail("zone ID must be positive");
            auto entry=t.zones.emplace(std::make_pair(map,zone),t.fallback);if(!entry.second)return fail("duplicate zone");p=&entry.first->second;explicitSet=&t.explicitKeys[entry.first->first];continue;
        }
        auto eq=line.find('=');if(!p||eq==std::string::npos)return fail("expected key=value in section");
        std::string key=trim(line.substr(0,eq)),value=trim(line.substr(eq+1));if(!keys.emplace(key,true).second)return fail("duplicate key");
        if(const char* why=assignKey(*p,key,value))return fail(why);
        if(explicitSet)explicitSet->insert(key);
    }
    if(input.bad()){error="read failed";return false;}out=std::move(t);error.clear();return true;
}
inline bool load(const std::string& path,Table& out,std::string& error){std::ifstream f(path);if(!f){error="file unavailable; neutral defaults retained";return false;}return parse(f,out,error);}

// Bilinear palettes at ADT chunk centers: continuous across authored chunk and
// tile boundaries, about 33 world units wide. No camera-anchored pattern or I/O.
inline Profile sample(const Table& table,const NorthlightRegionalFog::Region& region,const std::string& map,float x,float y){
    using namespace NorthlightRegionalFog;
    if(!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>100000||std::fabs(y)>100000)return table.fallback;
    double gx=(WorldZero-y)/ChunkSize-.5,gy=(WorldZero-x)/ChunkSize-.5;
    double ix=std::floor(gx),iy=std::floor(gy);Profile p[4];
    for(unsigned row=0;row<2;++row)for(unsigned col=0;col<2;++col){
        float px=float(WorldZero-(iy+row+.5)*ChunkSize),py=float(WorldZero-(ix+col+.5)*ChunkSize);
        p[row*2+col]=table.at(map,zoneAt(region,px,py));
    }
    return blend(blend(p[0],p[1],float(gx-ix)),blend(p[2],p[3],float(gx-ix)),float(gy-iy));
}
class Transition {
    bool valid=false;std::string map;float x=0,y=0;double time=0;Profile value;
public:
    Profile update(const Profile& target,const std::string& nextMap,float nx,float ny,double now){
        bool reset=!valid||map!=nextMap||(nx-x)*(nx-x)+(ny-y)*(ny-y)>256*256||now<time;
        value=reset?target:blend(value,target,float(1-std::exp(-std::min(now-time,.25)/1.25)));
        valid=true;map=nextMap;x=nx;y=ny;time=now;return value;
    }
};
inline void apply(const Profile& p,const float* direct,NorthlightCelestial::Context& sky){
    NorthlightCelestial::Body* bodies[]={&sky.sun,&sky.moon};float* colors[]={sky.sunColor,sky.moonColor};
    const float weight[]={sky.sunWeight,sky.moonWeight};
    const float energy=direct[0]*.2126f+direct[1]*.7152f+direct[2]*.0722f;
    for(unsigned s=0;s<2;++s){
        bodies[s]->emission=p.gain[s];
        float luma=p.light[s][0]*.2126f+p.light[s][1]*.7152f+p.light[s][2]*.0722f;
        for(unsigned c=0;c<3;++c){
            bodies[s]->tint[c]=p.disc[s][c];
            float tinted=p.light[s][c]*(energy/std::max(luma,.001f));
            colors[s][c]=(direct[c]+(tinted-direct[c])*p.mix[s])*p.strength[s]*weight[s];
        }
    }
}
} // namespace NorthlightCelestialProfiles
