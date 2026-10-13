#pragma once
// Task 34 SpellFog: replayed spell draws (they are drawn after Northlight's fog composite) get Northlight's fog through the game's own fog constants, at the cost of
// changing one or two constants of the record, not a pass. The M2 effect vertex shaders write fog = min(pow(max(z*c30.x + c30.y, 0), c30.z), 1) and the ps_3_0 combiners mix
// lerp(cF, X, fog) (cF a PS constant); so a draw replayed with fog' = fog * T_N(z) (T_N: Northlight's transmittance along the view ray) is fogged by both. The fit F maps the game's
// (c30.x, c30.y, c30.z) to (a, b, p) with pow(max(z*a + b, 0), p) ~ g(z) * T_N(z) on [0, min(game fog end, 128)] (Northlight's fog reaches 128 units); it is computed once per distinct
// game constant and frame, not per entry. Pure CPU maths: no D3D, no game data.
// Approximations, by design: the camera's heading stands for every pixel of the effect (the optical depth is taken on rays from the eye to a point at the heading, at the height of an effect
// above the ground there; off-axis pixels are farther per unit of view depth); the optical depth is sampled at six distances
// and interpolated, Northlight's fog reaches 128 units; the fit is one power law (a game ramp that starts late with a high power is followed less well); the horizon haze that
// Northlight adds to the picture is not folded; the in-scatter colour is the ambient sky source only (no direct-light term); the estimate follows the camera with a 0.25 s smoothing and
// is the previous frame's. Fixed-function effects and the ps_2_0 fixed-function fog colour are not touched.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace NorthlightSpellFog {
constexpr unsigned kSamples=6;
constexpr float kDistance[kSamples]={6,12,24,48,80,128};   // view distances (world units) the frame's optical depth is sampled at
constexpr float kRange=128;                                  // Northlight's fog is integrated to 128 units (FogInfo.w)
constexpr float kDepthScale=2;                               // the published optical depth is doubled: the view-axis estimate fogs held-back glows less than the world around them looks fogged
struct Frame{
    bool active=false;
    float tau[kSamples]={};        // optical depth from the eye to kDistance[i] along the view ray (nondecreasing)
    float scatter[3]={};           // radiance of the fog's in-scatter per unit of (1 - T): the neutral sky source
    unsigned revision=0;           // changes whenever the numbers do (the integration's cache key)
    unsigned stamp=0;              // counts the publishes (Hold::update): a frame that never reached the publish leaves it unchanged (see Freshness)
    // T_N(z): exp(-tau(z)) with tau interpolated linearly through (0,0) and the samples, constant beyond the last
    float transmittance(float z)const{
        if(!active||!(z>0))return 1;
        if(z>=kDistance[kSamples-1])return std::exp(-tau[kSamples-1]);
        float z0=0,t0=0;
        for(unsigned i=0;i<kSamples;++i){
            if(z<=kDistance[i]){const float u=(z-z0)/(kDistance[i]-z0);return std::exp(-(t0+(tau[i]-t0)*u));}
            z0=kDistance[i];t0=tau[i];
        }
        return std::exp(-tau[kSamples-1]);
    }
};
// The frame's numbers move toward the raw estimate with a time constant (no pumping when the camera crosses a fog bank), fade to nothing when the effect is off, and the frame is
// inactive (identity) below a negligible optical depth.
struct Hold{
    Frame out;
    void update(const Frame& raw,bool wanted,float dt){
        const float k=1-std::exp(-std::clamp(dt,0.f,.5f)/.25f);
        bool changed=false;
        for(unsigned i=0;i<kSamples;++i){
            const float target=wanted&&raw.active?raw.tau[i]:0.f,v=out.tau[i]+(target-out.tau[i])*k;
            if(std::fabs(v-out.tau[i])>1e-5f)changed=true;
            out.tau[i]=v;
        }
        if(wanted&&raw.active)for(unsigned c=0;c<3;++c){if(out.scatter[c]!=raw.scatter[c])changed=true;out.scatter[c]=raw.scatter[c];}
        const bool active=out.tau[kSamples-1]>.004f;
        if(active!=out.active)changed=true;
        out.active=active;
        if(changed)++out.revision;
        ++out.stamp;
    }
};
// A frame whose world render did not publish (an early return, the effects bailing out) leaves the estimate frozen: the integration asks once per frame whether the stamp moved since
// the last frame and tells the layer; a frozen estimate is not folded (identity) until the next publish.
struct Freshness{
    unsigned seen=0;
    bool next(unsigned stamp){const bool moved=stamp!=seen;seen=stamp;return moved;}
};
// bareBase: the vertex shader raises the bare ramp to the power (vs_3_0 Model/Projected variants) instead of max(ramp, 0) or lit; a fit for it must not end before the game's ramp does.
// The game's visibility at view distance z for its fog constant (x, y, power).
inline float gameVisibility(const float g[3],float z){return std::min(1.f,std::pow(std::max(z*g[0]+g[1],0.f),g[2]));}
// The fit F: out = (a, b, p). False: identity (leave the game's constant): no fog, a constant the shader would not use as a fog ramp, or a range too short to matter.
inline bool emitFog(const Frame& f,const float game[3],float out[3],bool bareBase=false){
    if(!f.active||!(f.tau[kSamples-1]>=.004f))return false;                 // no fog (transmittance 1 everywhere): the game's constant
    for(unsigned i=0;i<3;++i)if(!std::isfinite(game[i]))return false;
    if(!(game[0]<-1e-7f)||!(game[2]>.05f&&game[2]<=16.f))return false;   // a ramp that falls with distance, a sane exponent
    const float end=-game[1]/game[0];                                       // where the game's visibility reaches 0
    const float hi=std::min(end,kRange);
    if(!(hi>2.f))return false;
    constexpr unsigned N=12;float z[N],h[N];
    for(unsigned i=0;i<N;++i){z[i]=hi*(float(i)+.5f)/float(N);h[i]=gameVisibility(game,z[i])*f.transmittance(z[i]);}
    static const float scale[]={.6f,.8f,1.f,1.25f,1.6f,2.f,2.6f,3.5f};
    float best=1e30f,bestFit[3]={game[0],game[1],game[2]};bool found=false;
    auto error=[&](float a,float b,float p){float e=0;for(unsigned i=0;i<N;++i){const float v=std::min(1.f,std::pow(std::max(a*z[i]+b,0.f),p)),d=v-h[i];e+=d*d;}return e;};
    for(float s:scale){
        const float p=game[2]*s;
        // start: the line through h^(1/p), then a few damped Gauss-Newton steps on the error in visibility itself (the clamp at 1 and the plateau of a game ramp that starts late)
        float sz=0,sy=0,szz=0,szy=0;
        for(unsigned i=0;i<N;++i){const float y=std::pow(std::max(h[i],0.f),1.f/p);sz+=z[i];sy+=y;szz+=z[i]*z[i];szy+=z[i]*y;}
        const float den=N*szz-sz*sz;if(!(std::fabs(den)>1e-9f))continue;
        float a=(N*szy-sz*sy)/den,b=(sy-a*sz)/N;
        if(!(b>0))b=1;if(!(a<-1e-7f))a=-1.f/hi;
        float e=error(a,b,p),lambda=1e-3f;
        for(int it=0;it<8;++it){
            float jaa=0,jab=0,jbb=0,ga=0,gb=0;
            for(unsigned i=0;i<N;++i){
                const float w=a*z[i]+b;if(!(w>0))continue;
                const float v=std::pow(w,p);if(v>=1)continue;
                const float d=p*std::pow(w,p-1),r=v-h[i];
                jaa+=d*z[i]*d*z[i];jab+=d*z[i]*d;jbb+=d*d;ga+=d*z[i]*r;gb+=d*r;
            }
            const float m00=jaa*(1+lambda)+1e-12f,m11=jbb*(1+lambda)+1e-12f,det=m00*m11-jab*jab;
            if(!(std::fabs(det)>1e-18f))break;
            const float da=-(m11*ga-jab*gb)/det,db=-(m00*gb-jab*ga)/det;
            const float na=a+da,nb=b+db;
            if(!(na<-1e-7f)||!(nb>0)){lambda*=10;continue;}
            const float ne=error(na,nb,p);
            if(ne<e){a=na;b=nb;e=ne;lambda=std::max(lambda*.3f,1e-6f);}else lambda*=10;
        }
        if(!(a<-1e-7f)||!(b>0))continue;                                    // nonincreasing and visible at the eye
        if(bareBase&&-b/a<end*.98f)continue;                                // a shader that takes the pow of the bare ramp (no max): the ramp must not reach 0 before the game's own, a negative base is not 0 there and the fog would come back
        if(e<best){best=e;bestFit[0]=a;bestFit[1]=b;bestFit[2]=p;found=true;}
    }
    if(!found&&bareBase){ // no free fit reaches 0 late enough (thick fog and a far game end): pin the end at the game's and fit the slope and the power only
        const float E=end*.98f;
        for(float sc:scale){
            const float p=game[2]*sc;float num=0,dd=0;
            for(unsigned i=0;i<N;++i){const float y=std::pow(std::max(h[i],0.f),1.f/p),w=E-z[i];num+=y*w;dd+=w*w;}
            if(!(dd>0))continue;
            const float a=-num/dd,b=-a*E;if(!(a<-1e-7f)||!(b>0))continue;
            const float e=error(a,b,p);if(e<best){best=e;bestFit[0]=a;bestFit[1]=b;bestFit[2]=p;found=true;}
        }
    }
    if(!found)return false;
    for(unsigned i=0;i<3;++i)out[i]=bestFit[i];
    return true;
}
// The fit's visibility at z (what the replayed shader will compute).
inline float fitVisibility(const float fit[3],float z){return std::min(1.f,std::pow(std::max(z*fit[0]+fit[1],0.f),fit[2]));}
// The alpha sub-class mixes lerp(cF, X, fog'): the fog colour that makes the mix at a representative distance equal what Northlight's fog does to the game's
// result, X g T + cF (1-g) T + S (1-T), with fog' = g T. cF' = (cF (1-g) T + S (1-T)) / (1 - g T) at z = half of the fitted range. Unchanged where the fit leaves the
// denominator too small to matter (near the eye there is no fog to add).
inline void emitColor(const Frame& f,const float game[3],const float fit[3],const float cF[3],float out[3]){
    const float hi=std::min(-game[1]/game[0],kRange),zm=hi*.5f;
    const float g=gameVisibility(game,zm),T=f.transmittance(zm),gp=fitVisibility(fit,zm),den=1-gp;
    for(unsigned c=0;c<3;++c){
        out[c]=cF[c];
        if(den>.08f){const float v=(cF[c]*(1-g)*T+f.scatter[c]*(1-T))/den;out[c]=std::clamp(v,0.f,std::max(cF[c],4.f));}
    }
}
}
