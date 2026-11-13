#pragma once
#include "forever_atmosphere.h"
#include "regional_fog.h"

// Authored approximations, not imported Forever content. Density/falloff units
// match its volume format. Zone IDs are AreaTable parents from the FRF cache.
namespace NorthlightExpansionAtmosphere {
struct Profile {
    const char* map;uint32_t zone;const char* name;
    float color[3],density,groundDensity,height,falloff,anisotropy,night,storm,noise;
};
inline constexpr Profile Profiles[]={
    {"Expansion01",3483,"Hellfire Peninsula",{.76f,.43f,.25f},.075f,.12f,16,2.0f,.65f,1.10f,1.8f,.40f},
    {"Expansion01",3518,"Nagrand",{.62f,.76f,.62f},.028f,.055f,8,3.0f,.72f,1.45f,2.8f,.15f},
    {"Expansion01",3519,"Terokkar Forest",{.49f,.63f,.61f},.055f,.13f,18,2.0f,.70f,1.50f,2.4f,.35f},
    {"Expansion01",3520,"Shadowmoon Valley",{.39f,.57f,.25f},.09f,.15f,22,1.6f,.62f,1.12f,1.7f,.55f},
    {"Expansion01",3521,"Zangarmarsh",{.33f,.57f,.67f},.11f,.26f,28,1.2f,.72f,1.55f,2.0f,.65f},
    {"Expansion01",3522,"Blade's Edge Mountains",{.66f,.57f,.44f},.035f,.065f,10,2.7f,.65f,1.20f,2.0f,.25f},
    {"Expansion01",3523,"Netherstorm",{.54f,.38f,.69f},.06f,.08f,16,2.5f,.60f,1.10f,1.8f,.45f},
    {"Expansion01",3703,"Shattrath City",{.65f,.70f,.65f},.035f,.065f,12,2.5f,.68f,1.30f,2.2f,.20f},
    {"Northrend",65,"Dragonblight",{.66f,.75f,.83f},.065f,.10f,14,2.0f,.66f,1.30f,3.0f,.30f},
    {"Northrend",66,"Zul'Drak",{.48f,.57f,.63f},.08f,.16f,22,1.8f,.65f,1.45f,2.5f,.45f},
    {"Northrend",67,"The Storm Peaks",{.72f,.80f,.88f},.025f,.045f,9,3.3f,.67f,1.20f,4.0f,.25f},
    {"Northrend",210,"Icecrown",{.43f,.55f,.66f},.085f,.15f,20,2.0f,.62f,1.35f,3.5f,.50f},
    {"Northrend",394,"Grizzly Hills",{.65f,.66f,.49f},.04f,.11f,14,2.2f,.74f,1.60f,2.5f,.30f},
    {"Northrend",495,"Howling Fjord",{.66f,.73f,.73f},.045f,.12f,20,1.8f,.72f,1.45f,2.7f,.35f},
    {"Northrend",2817,"Crystalsong Forest",{.57f,.53f,.73f},.04f,.10f,16,2.2f,.70f,1.40f,2.0f,.35f},
    {"Northrend",3537,"Borean Tundra",{.63f,.71f,.74f},.05f,.08f,12,2.3f,.66f,1.30f,3.0f,.30f},
    {"Northrend",3711,"Sholazar Basin",{.52f,.70f,.55f},.07f,.22f,24,1.4f,.74f,1.65f,2.4f,.60f},
    {"Northrend",4197,"Wintergrasp",{.61f,.72f,.82f},.045f,.09f,12,2.5f,.67f,1.35f,3.3f,.35f},
    {"Northrend",4395,"Dalaran",{.69f,.70f,.80f},.018f,.025f,6,4.0f,.65f,1.20f,2.5f,.10f},
    {"Northrend",4742,"Hrothgar's Landing",{.62f,.73f,.81f},.07f,.15f,22,1.5f,.65f,1.40f,3.0f,.45f},
};
inline NorthlightForever::Frame at(const std::string& map,uint32_t zone,float day,float storm){
    NorthlightForever::Frame f;if(!std::isfinite(day)||day<0||day>=1)return f;
    const Profile* p=nullptr;for(const auto& candidate:Profiles)if(candidate.zone==zone&&map==candidate.map){p=&candidate;break;}
    // Map 530 also contains blood elf/draenei starting areas. Never apply an
    // Outland fallback to an unknown zone, missing tile or another continent.
    if(!p)return f;
    f.valid=true;storm=std::isfinite(storm)?std::clamp(storm,0.f,1.f):0;
    const float daylight=std::clamp(float(std::sin((day-.25f)*6.28318530718f))*4,0.f,1.f);
    const float dusk=std::max(0.f,1-std::fabs(day-.76f)/.075f)*(1-storm);
    const float density=(p->night+(1-p->night)*daylight)*(1+(p->storm-1)*storm);
    const float nightColor[3]={.48f,.59f,.78f},sunset[3]={1.f,.67f,.37f};
    for(unsigned i=0;i<3;++i){auto& l=f.layer[i];
        l.flag[0]=1;l.flag[1]=1;l.flag[2]=i==1?1:0;l.flag[3]=1;
        l.p[0]=i==0?8.f:0.f;l.p[1]=(i==0?p->density:i==1?p->groundDensity:p->density*.7f*storm)*density;
        l.p[2]=1;l.p[3]=i==0?.18f:p->falloff;l.p[5]=i==0?100.f:p->height;
        l.p[6]=6;l.p[7]=-4;l.p[9]=i==2?.65f:1.6f;l.p[10]=p->anisotropy;
        l.p[19]=1;l.p[11]=.015f;l.p[12]=.008f;l.p[14]=-.006f;l.p[15]=.012f;
        l.p[20]=.8f;l.p[21]=.23f;l.p[22]=1-p->noise;l.p[23]=p->noise;
        for(unsigned c=0;c<3;++c){
            const float hue=p->color[c]*(nightColor[c]+(1-nightColor[c])*daylight);
            l.color[0][c]=(hue*(1-dusk*.3f)+sunset[c]*dusk*.3f)*(1-storm*.25f);
            l.color[1][c]=l.color[0][c]*.16f;l.color[2][c]=l.color[0][c]*.07f;l.color[3][c]=l.color[0][c];
        }
    }
    return f;
}
inline NorthlightForever::Frame sample(const NorthlightRegionalFog::Region& region,const std::string& map,const float* position,float day,float storm,const float* direct,const float* ambient){
    using namespace NorthlightRegionalFog;
    NorthlightForever::Frame result;if(map!="Expansion01"&&map!="Northrend")return result;
    for(unsigned c=0;c<3;++c)if(!std::isfinite(position[c])||std::fabs(position[c])>100000)return result;
    const double gx=(WorldZero-position[1])/ChunkSize-.5,gy=(WorldZero-position[0])/ChunkSize-.5;
    const double ix=std::floor(gx),iy=std::floor(gy);const float u=float(gx-ix),v=float(gy-iy);
    for(unsigned row=0;row<2;++row)for(unsigned col=0;col<2;++col){
        const auto zone=zoneAtChunk(region,int(ix)+int(col),int(iy)+int(row));
        NorthlightForever::add(result,at(map,zone,day,storm),(col?u:1-u)*(row?v:1-v));
    }
    // These profiles author only the medium. Preserve the client's regional
    // lighting, including Outland's coloured sky, instead of publishing black.
    if(result.valid){std::copy_n(direct,3,result.direct);std::copy_n(ambient,3,result.ambient);}
    return result;
}
}
