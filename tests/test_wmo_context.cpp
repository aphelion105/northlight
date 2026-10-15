#include "wmo_context.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightWmoContext;
int main(){
    std::array<unsigned char,SkyBytes> sky{};
    auto set=[&](size_t at,float f){std::memcpy(sky.data()+at,&f,4);};
    auto color=[&](size_t at,std::uint32_t n){std::memcpy(sky.data()+at,&n,4);};
    const float eye[]={120,-450,70};set(4,.75f);for(unsigned i=0;i<3;++i)set(0x18+i*4,eye[i]);
    set(0x19c,-.6f);set(0x1a0,0);set(0x1a4,-.8f);color(0x1a8,0xffa04020);color(0x1ac,0xff204060);
    Lighting light;assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light));
    assert(std::fabs(light.lightDirection[0]-.6f)<1e-6f&&std::fabs(light.lightDirection[2]-.8f)<1e-6f);
    assert(light.direct[0]==160.f/255&&light.ambient[2]==96.f/255&&light.dayFraction==.75f);
    // the 18 light slots at +0xd4 (slot 10 = band 10 sunHalo at +0xfc); unproven until a read proves the code.
    assert(light.slots[0]==0&&!light.slotsProven);
    color(0xd4,0xff2e2538);color(0xfc,0xff243f32);color(0xd4+17*4,0xff123456);
    assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light)&&light.slots[0]==0xff2e2538&&light.slots[10]==0xff243f32&&light.slots[17]==0xff123456&&!light.slotsProven);
    color(0x17c,0xff010203);assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light)&&!light.slotsConsistent); /* slot 1 differs from its raw copy at +0x17c: layout not confirmed */
    color(0xd8,0xff6ba1c1);color(0x17c,0xff6ba1c1);assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light)&&light.slotsConsistent&&!light.slotsProven);
    /* 0.3.163 Tirisfal: weather rescales the ambient (+0x1ac) and direct (+0x1a8) after the copy; the check must not care. */
    color(0x1ac,0xff0a0b0c);color(0x1a8,0xff111213);assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light)&&light.slotsConsistent);
    color(0x1ac,0xff204060);color(0x1a8,0xffa04020);
    {size_t count=0;const auto* proof=lightSlotProofs(count);assert(count==4&&proof[0].address==0x7ec0b8&&proof[2].address==0x7f36f6&&proof[3].address==0x7ee887);}
    for(unsigned i=0;i<=360;++i){
        const float angle=float(i)*.01745329252f,c=std::cos(angle),s=std::sin(angle);
        float view[16]={c,-s,0,0,s,c,0,0,0,0,1,0,0,0,0,1};
        for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)view[12+j]-=eye[k]*view[4*k+j];
        NorthlightWorldContext::TerrainContext decoded;assert(context(view,light,decoded));
        assert(NorthlightWorldContext::cameraAgrees(decoded,eye));
        for(unsigned j=0;j<3;++j)assert(std::fabs(decoded.lightDirection[j]-light.lightDirection[j])<1e-5f);
        // The camera remains valid when a terrain variant leaves native light
        // registers unset, negative, or nonfinite. Global lighting must win.
        float native[12]={0,0,1,0,.1f,.2f,.3f,0,.4f,.5f,.6f,0};
        for(unsigned mutation=0;mutation<4;++mutation){
            float stale[12];std::memcpy(stale,native,sizeof stale);
            if(mutation==0)stale[2]=0;
            if(mutation==1)stale[4]=-.001f;
            if(mutation==2)stale[8]=std::numeric_limits<float>::quiet_NaN();
            if(mutation==3)stale[11]=std::numeric_limits<float>::infinity();
            assert(!NorthlightWorldContext::decodeTerrain(view,stale,decoded));
            assert(terrainContext(view,stale,eye,&light,decoded));
            for(unsigned j=0;j<3;++j)assert(decoded.direct[j]==light.direct[j]&&decoded.ambient[j]==light.ambient[j]);
            assert(!terrainContext(view,stale,eye,nullptr,decoded));
        }
        assert(terrainContext(view,nullptr,eye,&light,decoded));
        assert(terrainContext(view,native,eye,nullptr,decoded));
        float wrongEye[]={eye[0]+1,eye[1],eye[2]};
        assert(!terrainContext(view,native,wrongEye,&light,decoded));
        float nonrigid[16];std::memcpy(nonrigid,view,sizeof nonrigid);nonrigid[3]=.1f;
        assert(!terrainContext(nonrigid,native,eye,&light,decoded));
        std::uint64_t lit=0,prelit=0;for(const auto& entry:kWmoShaderSignatures){if(entry.lighting)lit=entry.hash;else prelit=entry.hash;}
        assert(lit&&prelit);float constants[12]={};
        for(unsigned j=0;j<3;++j){constants[j]=light.ambient[j];constants[4+j]=light.direct[j];
            for(unsigned k=0;k<3;++k)constants[8+j]-=light.lightDirection[k]*view[k*4+j];}
        assert(decodeLitShader(lit,view,constants,decoded));assert(!decodeLitShader(prelit,view,constants,decoded));
        assert(!decodeLitShader(0,view,constants,decoded));
        for(unsigned j=0;j<3;++j){assert(std::fabs(decoded.lightDirection[j]-light.lightDirection[j])<1e-5f);assert(decoded.ambient[j]==light.ambient[j]&&decoded.direct[j]==light.direct[j]);}
    }
    const auto valid=sky;
    {sky=valid;set(0x18,eye[0]+7);Lighting trailing;assert(decodeGlobalLighting(sky.data(),sky.size(),eye,trailing));sky=valid;} /* 0.3.200: several frames of travel behind the camera are accepted */
    for(unsigned mutation=0;mutation<8;++mutation){sky=valid;
        switch(mutation){case 0:set(4,1);break;case 1:set(4,-.1f);break;case 2:set(4,std::numeric_limits<float>::quiet_NaN());break;
            case 3:set(0x18,eye[0]+50);break;case 4:set(0x19c,0);set(0x1a4,0);break;
            case 5:set(0x19c,2);break;case 6:set(0x1a0,std::numeric_limits<float>::infinity());break;
            case 7:set(0x18,100001);break;}
        Lighting output=light;assert(!decodeGlobalLighting(sky.data(),sky.size(),eye,output));assert(output.dayFraction==light.dayFraction);
    }
    sky=valid;assert(!decodeGlobalLighting(sky.data(),SkyBytes-1,eye,light));assert(!decodeGlobalLighting(nullptr,SkyBytes,eye,light));
    auto torn=sky;torn[0x1a8]^=1;assert(!decodeCoherentGlobalLighting(sky.data(),torn.data(),sky.size(),eye,light));
    assert(decodeCoherentGlobalLighting(sky.data(),sky.data(),sky.size(),eye,light));
    for(size_t i=0;i<sizeof(kWmoShaderSignatures)/sizeof(kWmoShaderSignatures[0]);++i){assert(signature(kWmoShaderSignatures[i].hash)==kWmoShaderSignatures+i);if(i)assert(kWmoShaderSignatures[i-1].hash<kWmoShaderSignatures[i].hash);}
    { /* 0.3.207: a read between the colour copy and the weather rescale keeps the last rescaled colours (Duskwood flash) */
        auto read=[](std::uint32_t direct,std::uint32_t ambient){Lighting l;l.slotsConsistent=true;l.slots[1]=0xff84bce2;l.slots[0]=0xff3a6a9a;
            rgb(direct,l.direct);rgb(ambient,l.ambient);return l;};
        const std::uint32_t scaledDirect=0xff7bafd3,scaledAmbient=0xff366290,rawDirect=0xff84bce2,rawAmbient=0xff3a6a9a;
        LightingHold hold;Lighting l=read(scaledDirect,scaledAmbient);
        assert(!hold.filter(l,"Azeroth",1000));
        l=read(rawDirect,rawAmbient);assert(hold.filter(l,"Azeroth",1016)); /* both colours back to the rescaled ones */
        {float d[3],a[3];rgb(scaledDirect,d);rgb(scaledAmbient,a);for(unsigned i=0;i<3;++i)assert(l.direct[i]==d[i]&&l.ambient[i]==a[i]);}
        l=read(scaledDirect,scaledAmbient);assert(!hold.filter(l,"Azeroth",1032)); /* the race lasts one read */
        l=read(rawDirect,scaledAmbient);assert(hold.filter(l,"Azeroth",1048)); /* direct caught alone */
        l=read(scaledDirect,rawAmbient);assert(hold.filter(l,"Azeroth",1064)); /* between the two rescales */
        {float a[3];rgb(scaledAmbient,a);assert(l.ambient[0]==a[0]&&l.ambient[2]==a[2]);}
        l=read(0xff7aaed2,0xff356190);assert(!hold.filter(l,"Azeroth",1080)); /* a new rescaled colour is taken as read */
        /* the weather ended: MaxHeld unscaled reads in a row are held, the next one is accepted and kept */
        for(unsigned i=0;i<LightingHold::MaxHeld;++i){l=read(rawDirect,rawAmbient);assert(hold.filter(l,"Azeroth",1096+16*i));}
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Azeroth",1128));
        {float d[3];rgb(rawDirect,d);assert(l.direct[1]==d[1]);}
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Azeroth",1144));
        /* a brighter rescale (a lightning flash) is never held over the band colours that follow it */
        l=read(0xff94cce8,0xff4a7aaa);assert(!hold.filter(l,"Azeroth",1160));
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Azeroth",1176));
        {float d[3];rgb(rawDirect,d);assert(l.direct[0]==d[0]&&l.direct[2]==d[2]);}
        /* a long gap without reads (loading screen, alt-tab): the first unscaled read after HoldMs is accepted */
        l=read(scaledDirect,scaledAmbient);assert(!hold.filter(l,"Azeroth",1192));
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Azeroth",1192+LightingHold::HoldMs+1));
        l=read(scaledDirect,scaledAmbient);assert(!hold.filter(l,"Azeroth",2000));
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Kalimdor",2016)); /* a new map forgets the hold */
        l=read(scaledDirect,scaledAmbient);assert(!hold.filter(l,"Kalimdor",2032));
        l=read(rawDirect,rawAmbient);l.slotsConsistent=false;assert(!hold.filter(l,"Kalimdor",2048)); /* unconfirmed slot layout: as read */
        assert(hold.held==3+LightingHold::MaxHeld);
        /* a copy filtered for a rejected read leaves the kept state as it was (the renderer keeps the copy only on acceptance) */
        LightingHold kept;l=read(scaledDirect,scaledAmbient);kept.filter(l,"Azeroth",3000);
        {auto copy=kept;l=read(rawDirect,rawAmbient);assert(copy.filter(l,"Azeroth",3016));}
        assert(kept.held==0&&kept.direct.run==0);
    }
    { /* the glare darkening is predicted: scale = int((1 - .35 glow) 255), c = (scale c + 255) >> 8 per channel (DayNight::Update 0x7816f0) */
        assert(predictScale(0)==255&&predictScale(1)==166&&predictScale(.8f)==184&&predictScale(-0.f)==255); /* 1-.35 = .65 -> 165.75 -> 166 */
        assert(predictScale(std::numeric_limits<float>::quiet_NaN())==255&&predictScale(1e30f)==255); /* unusable input: the identity */
        assert(predictColour(0xff84bce2,184)==0xff5f88a3&&predictColour(0xff3a6a9a,184)==0xff2a4d6f);
        assert(predictColour(0xff84bce2,255)==0xff84bce2&&predictColour(0x80ffffff,255)==0x80ffffff&&predictColour(0xffffffff,0)==0xff000000); /* scale 255 is the identity, 0 maps to 0 */
        for(unsigned c=0;c<256;++c)assert(((255u*c+255)>>8)==c);
        assert(withinLsb(0xff102030,0xff112031)&&withinLsb(0xff102030,0xff0f1f2f)&&!withinLsb(0xff102030,0xff122030)&&!withinLsb(0xff102030,0xff10202e)&&withinLsb(0x00102030,0xff102030)); /* alpha ignored */
        /* a fake memory serves the two predictor reads (glow 0xd38f4c, liquid 0xcd8794), each twice */
        float glow=.8f;std::uint32_t liquid=0;unsigned reads=0;bool tear=false;
        auto fake=[&](std::uintptr_t a,void* out,size_t n){++reads;if(n!=4)return false;
            if(a==0xd38f4c){float g=glow;if(tear)glow+=.01f;std::memcpy(out,&g,4);return true;}
            if(a==0xcd8794){std::memcpy(out,&liquid,4);return true;}return false;};
        Lighting base;base.slotsConsistent=true;base.slots[1]=0xff84bce2;base.slots[0]=0xff3a6a9a;
        Lighting p=base;readPredictors(fake,p);assert(p.predictors&&p.glow==.8f&&p.liquidType==0&&reads==4);
        liquid=7;p=base;readPredictors(fake,p);assert(p.predictors&&p.liquidType==7);
        tear=true;p=base;readPredictors(fake,p);assert(!p.predictors); /* the glow moved between its two reads */
        tear=false;p=base;readPredictors([](std::uintptr_t,void*,size_t){return false;},p);assert(!p.predictors);
        /* predicted(): only for a confirmed layout, no liquid, no flash */
        liquid=0;glow=.8f;p=base;readPredictors(fake,p);std::uint32_t wd=0,wa=0;
        assert(predicted(p,wd,wa)&&wd==0xff5f88a3&&wa==0xff2a4d6f);
        {Lighting q=p;q.liquidType=2;assert(!predicted(q,wd,wa));q=p;q.flash=9;assert(!predicted(q,wd,wa));q=p;q.slotsConsistent=false;assert(!predicted(q,wd,wa));q=p;q.predictors=false;assert(!predicted(q,wd,wa));}
        auto with=[&](std::uint32_t direct,std::uint32_t ambient){Lighting l=p;l.packedDirect=direct;l.packedAmbient=ambient;rgb(direct,l.direct);rgb(ambient,l.ambient);return l;};
        const std::uint32_t band1=0xff84bce2,band0=0xff3a6a9a,dark1=0xff5f88a3,dark0=0xff2a4d6f;
        LightingHold hold;Lighting l=with(dark1,dark0);
        assert(!hold.filter(l,"Azeroth",1000)&&hold.predictedAccepts==2&&hold.predictedMisses==0); /* matches the prediction: accepted as read */
        l=with(0xff6089a2,0xff2b4c70);assert(!hold.filter(l,"Azeroth",1016)&&hold.predictedAccepts==4); /* one LSB per channel off: still accepted */
        l=with(band1,band0);assert(hold.filter(l,"Azeroth",1032)&&hold.predictedMisses==2); /* the unscaled colours of the race: not the prediction, held */
        {float d[3];rgb(0xff6089a2,d);assert(l.direct[0]==d[0]&&l.direct[2]==d[2]);} /* held: the last accepted colour (the one of 1016) */
        /* an exact match is accepted even when it equals the band (scale 255: glow 0), and never held over */
        {Lighting open=p;open.glow=0;open.packedDirect=band1;open.packedAmbient=band0;rgb(band1,open.direct);rgb(band0,open.ambient);
         LightingHold fresh;Lighting a=with(dark1,dark0);fresh.filter(a,"Azeroth",2000);assert(!fresh.filter(open,"Azeroth",2016)&&fresh.predictedAccepts==4);}
        /* a prediction that does not apply (flash, liquid, no predictors) leaves the hold behaviour of 0.3.207 untouched */
        {LightingHold h2;Lighting a=with(dark1,dark0);a.flash=3;h2.filter(a,"Azeroth",3000);Lighting b=with(band1,band0);b.flash=3;assert(h2.filter(b,"Azeroth",3016)&&h2.predictedAccepts==0&&h2.predictedMisses==0);}
        {LightingHold h3;Lighting a=with(dark1,dark0);a.liquidType=1;h3.filter(a,"Azeroth",3000);Lighting b=with(band1,band0);b.liquidType=1;assert(h3.filter(b,"Azeroth",3016));}
        /* a partial read: ambient unscaled, direct already scaled in the other order is not possible; direct unscaled with ambient scaled still held per colour */
        {LightingHold h4;Lighting a=with(dark1,dark0);h4.filter(a,"Azeroth",4000);Lighting b=with(band1,dark0);assert(h4.filter(b,"Azeroth",4016)&&h4.predictedAccepts==3&&h4.predictedMisses==1);
         float d[3];rgb(dark1,d);assert(b.direct[0]==d[0]&&b.direct[2]==d[2]);}
        /* the proofs of the prediction: 15 entries, each within the 64 byte proof read (the real bytes are checked against the exe by validate_wmo_context.py) */
        size_t count=0;const auto* proof=predictionProofs(count);assert(count==15&&proof[0].address==0x7ee756&&proof[13].address==0xa3e860&&proof[14].address==0x9e30c0);
        for(size_t i=0;i<count;++i)assert(proof[i].size<=64);
    }
    std::puts("PASS the glare darkening of light1 is predicted from the glow (integer formula, 1 LSB tolerance); flash, liquid and unproven reads fall back to the hold");
    std::puts("PASS sky block reads between the colour copy and the glare darkening keep the last rescaled colours");
    std::puts("PASS 361 independent camera rotations, exact WMO ambient/direct/source sign and prelit/unknown rejection");
    std::puts("PASS engine sky RGB/255, fresh camera/day/direction checks, torn snapshot and malformed input rejection");
    std::puts("PASS all1967 exact WMO-only shader identities and sorted lookup");
    std::puts("PASS 1444 stale terrain-light cases with stable global lighting, native fallback, camera/rigidity rejection");
}
