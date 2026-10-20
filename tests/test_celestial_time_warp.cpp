#include "celestial_time_warp.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightCelestialOrbit;
static double wrap(double seconds){double d=std::fmod(seconds,kDaySeconds);if(d<0)d+=kDaySeconds;return d/kDaySeconds;}
// The natural orbit (ambient policies); the displayed one, with its under-horizon dives, is checked at the end.
static Result at(double seconds){return evaluateNatural(wrap(seconds));}
static Result shown(double seconds){return evaluate(wrap(seconds));}
static void near(double a,double b,double e=1e-8){if(!(std::fabs(a-b)<e)){std::fprintf(stderr,"near failed: %.15g vs %.15g tolerance %.15g\n",a,b,e);assert(false);}}
// Independent five-key linear table, as recovered from the client executable.
static double nativeSun(double day){
 const double times[]={double(float(5.5/24)),double(float((11.+55./60)/24)),.5,double(float((12.+5./60)/24)),double(float(21.5/24))};
 const double values[]={double(float(100*kPi/180)),0.0872664675116539,0.0872664675116539,0.0872664675116539,double(float(100*kPi/180))};
 for(unsigned i=1;i<5;++i)if(day>=times[i-1]&&day<=times[i])return 90-(values[i-1]+(values[i]-values[i-1])*(day-times[i-1])/(times[i]-times[i-1]))*180/kPi;
 return 90-values[0]*180/kPi;
}
int main(){
 near(kSunriseSeconds,6*3600+10*60+31.5789,.01);near(kSunsetSeconds,20*3600+30*60+31.5789,.01);
 near(at(kSunriseSeconds).sun.elevation,0);near(at(kSunsetSeconds).sun.elevation,0);
 near(at(kMoonsetSeconds).moon.elevation,0);near(kMoonsetSeconds,5*3600+45*60);near(at(kMoonriseSeconds).moon.elevation,0);near(kMoonriseSeconds,20*3600+50*60);
 assert(at(kSunsetSeconds+60).moon.elevation<0); // 0.3.215: the moon rises at 20:50, after the sun has set behind the far edge
 near(at(12*3600).sun.elevation,85,1e-5);near(at(0).moon.elevation,43);
 assert(at(19*3600+45*60).sun.elevation>7);assert(at(20*3600+15*60).moon.elevation<0);
 assert(at(6*3600+25*60).sun.elevation<4); // No former 15-degree jump.
 unsigned rises[2]={},sets[2]={},crests[2]={};double maxStep[2]={};auto previous=at(-1);double previousDelta[2]={};
 for(int t=0;t<5*86400;++t){auto r=at(t);assert(r.valid);near(r.sun.elevation,nativeSun((t%86400)/86400.),1e-9);
  assert(!(r.sun.elevation>1e-8&&r.moon.elevation>1e-8));
  const Body now[]={r.sun,r.moon},before[]={previous.sun,previous.moon};
  for(unsigned i=0;i<2;++i){const double delta=now[i].elevation-before[i].elevation;
   assert(std::fabs(now[i].elevation)<(i?43.000001:85.000001));assert(std::fabs(delta)<.006);
   if(before[i].elevation<0&&now[i].elevation>=0)++rises[i];
   if(before[i].elevation>0&&now[i].elevation<=0)++sets[i];
   if(previousDelta[i]>0&&delta<=0)++crests[i];
   if(i&&t>1)assert(std::fabs(delta-previousDelta[i])<.000002);
   previousDelta[i]=delta;maxStep[i]=std::max(maxStep[i],std::fabs(delta));
   double len=0;for(float c:now[i].direction){assert(std::isfinite(c));len+=double(c)*c;}
   near(len,1,2e-7);near(elevationDegrees(now[i].direction[2]),now[i].elevation,3e-5);
  }previous=r;
 }
 for(unsigned i=0;i<2;++i)assert(rises[i]==5&&sets[i]==5&&crests[i]==5);
 // Native sun is piecewise linear with its original 11:55--12:05 plateau;
 // its speed changes at those native keys, not at invented horizon shoulders.
 for(double t:{kSunriseSeconds,kSunsetSeconds,kNativeSunStartDay*kDaySeconds,kNativeSunCrestBeginDay*kDaySeconds,kNativeSunCrestEndDay*kDaySeconds,kNativeSunEndDay*kDaySeconds,0.}){
  auto a=at(t-.001),b=at(t+.001);assert(std::fabs(a.sun.elevation-b.sun.elevation)<.00001);assert(std::fabs(a.moon.elevation-b.moon.elevation)<.00002);
 }
 for(unsigned i=0;i<50000;++i){const float d=float(((i*7919u)%86400u)/86400.);auto a=evaluateNatural(d);(void)evaluateNatural(.5);auto b=evaluateNatural(d);near(a.sun.elevation,b.sun.elevation);near(a.moon.elevation,b.moon.elevation);}
 double last=at(2*3600).moon.elevation;for(int t=1;t<=300;++t){double current=at(2*3600+t).moon.elevation;assert(current<last&&last-current<.006);last=current;}
 // 0.3.177 quarter-sine moon: horizon rates, velocity continuity at every joint, the day-side hold,
 // and the short dusk/dawn windows in which neither body gives full light (both below 4.59 degrees).
 auto moon=[](double t){return at(t).moon.elevation;};
 auto rate=[&](double t){return (moon(t+1)-moon(t-1))*.5;}; // degrees per second, central difference
 const double riseSpan=kMoonPeakSeconds-kMoonriseSeconds,fallSpan=kDaySeconds+kMoonsetSeconds-kMoonPeakSeconds;
 near(rate(kMoonriseSeconds-1)*3600,21.33,.05);near(rate(kMoonriseSeconds+1)*3600,21.33,.05);
 near(rate(kMoonsetSeconds-1)*3600,-11.75,.05);near(rate(kMoonsetSeconds+1)*3600,-11.75,.05);
 const double holdStart=kMoonsetSeconds+fallSpan,holdEnd=kMoonriseSeconds-riseSpan; // 11:30:00, 17:40:00
 near(holdStart,11*3600+30*60,1);near(holdEnd,17*3600+40*60,1);
 for(double joint:{kMoonriseSeconds,kMoonsetSeconds,holdStart,holdEnd,0.})assert(std::fabs(rate(joint-1)-rate(joint+1))<1e-5);
 for(double t=holdStart+1;t<holdEnd;t+=97)assert(moon(t)==-43);
 const double full=elevationDegrees(.08);unsigned dusk=0,dawn=0;
 for(int t=18*3600;t<23*3600;++t)if(at(t).sun.elevation<full&&moon(t)<full)++dusk;
 for(int t=3*3600;t<8*3600;++t)if(at(t).sun.elevation<full&&moon(t)<full)++dawn;
 // The native sun sets at about 10.1 degrees/h (4.59 degrees 27 min before sunset) and rises at
 // 14.8 (18.6 min): dusk 27.3+19.5 (moonrise 20:50)+12.9 min, dawn 23.4+25.4 (moonset 05:45)+18.6 min (the cosine-ease moon: about 71 and 97).
 assert(dusk<=61*60&&dawn<=68*60);
 std::printf("PASS quarter-sine moon: rise %.3f / set %.3f deg/h, hold -43 from %.0f to %.0f s, dusk %.1f / dawn %.1f min without full light\n",
  rate(kMoonriseSeconds+1)*3600,rate(kMoonsetSeconds+1)*3600,holdStart,holdEnd,dusk/60.,dawn/60.);
 for(double bad:{-1.,1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})assert(!evaluate(bad).valid&&!evaluateNatural(bad).valid&&!evaluateNative(bad).valid);
 // The native moon (DayNight::SetPlanets): 55 deg at midnight, parked 10 under from 04:00 to 22:00, up 22:17:41-03:23:51;
 // 2.46 deg at gameDay .931987 in a game log. The native sun is the sun above.
 near(evaluateNative(0).moon.elevation,55,1e-4);near(evaluateNative(.5).moon.elevation,-10,1e-4);
 near(evaluateNative(80261.54/kDaySeconds).moon.elevation,0,1e-3);near(evaluateNative(12230.77/kDaySeconds).moon.elevation,0,1e-3);
 near(evaluateNative(.931987).moon.elevation,2.47,.01);
 for(int t=0;t<86400;t+=60){auto a=evaluateNative(t/kDaySeconds),b=evaluateNatural(t/kDaySeconds);near(a.sun.elevation,b.sun.elevation);}
 // Held clock samples remain smoothed for both lights/rays at native speeds.
 for(double start:{6*3600.+15*60,19*3600.+40*60,21*3600.,5*3600.})for(unsigned fps:{30u,60u,144u}){
  LightMotion smooth;double lastRaw=0,lastLight=0,maxRaw=0,maxLight=0;bool movedOnHeld=false;
  for(unsigned frame=0;frame<fps*300;++frame){double t=start+double(frame/fps),d=t/kDaySeconds;auto raw=evaluate(d);auto light=smooth.update(raw,d,uint32_t(std::llround(1000.*frame/fps)));
   bool sun=start>=6*3600&&start<20*3600;double a=sun?raw.sun.elevation:raw.moon.elevation,b=sun?light.sun.elevation:light.moon.elevation;
   if(frame){maxRaw=std::max(maxRaw,std::fabs(a-lastRaw));maxLight=std::max(maxLight,std::fabs(b-lastLight));if(a==lastRaw&&std::fabs(b-lastLight)>1e-7)movedOnHeld=true;}lastRaw=a;lastLight=b;
  }assert(movedOnHeld&&maxLight<maxRaw*.10);
 }
 for(unsigned fps:{25u,50u,100u}){LightMotion f;const auto initial=at(12*3600);f.update(initial,.5,0);auto target=initial;target.sun=body(20);Result r;
  for(unsigned i=1;i<=fps;++i)r=f.update(target,.5,1000*i/fps);
  near(r.sun.elevation,20+(initial.sun.elevation-20)*std::exp(-2.5),1e-10);near(f.update(target,.5,1000).sun.elevation,r.sun.elevation);
  near(f.update(at(0),0,1010).moon.elevation,43);near(f.update(target,.5,1020,true).sun.elevation,20);near(f.update(at(0),0,5000).moon.elevation,43);
 }
 LightMotion f;f.update(at(86399.9),86399.9/86400.,UINT32_MAX-9);auto m=f.update(at(0),0,10);assert(std::fabs(m.moon.elevation-43)<.00001);
 auto invalid=f.update(evaluate(-1),-1,20);assert(!invalid.valid&&!f.ready);
 // Displayed orbit: the natural track until a disc is under the horizon (centre one radius down), C1 dives to a
 // DiveDepth hold below that, and the two bodies pass each other 8-10 degrees under the horizon, evening and morning.
 {const double radius[2]={kSunRadiusDegrees,kMoonRadiusDegrees};near(kSunRadiusDegrees,1.2096,1e-4);near(kMoonRadiusDegrees,1.9284,1e-4);
  double lastDelta[2]={},maxStep=0,maxBend=0;unsigned crossings=0;double previousGap=0;auto before=shown(-1);
  for(int t=0;t<86400;++t){const auto r=shown(t),n=at(t);
   const double e[2]={r.sun.elevation,r.moon.elevation},natural[2]={n.sun.elevation,n.moon.elevation},prior[2]={before.sun.elevation,before.moon.elevation};
   for(unsigned i=0;i<2;++i){
    if(natural[i]>=-radius[i]+1e-6)near(e[i],natural[i],1e-9);
    assert(e[i]>=-kDiveDepth-1e-9&&e[i]<=std::max(natural[i],-radius[i])+1e-9);
    const double delta=e[i]-prior[i];maxStep=std::max(maxStep,std::fabs(delta));
    if(t&&e[i]<0)maxBend=std::max(maxBend,std::fabs(delta-lastDelta[i]));lastDelta[i]=delta; // under the horizon (the native sun's crest keys bend)
   }
   const double gap=e[0]-e[1];
   if(t&&(gap>0)!=(previousGap>0)){++crossings;assert(e[0]<-8&&e[0]>-10);}
   previousGap=gap;before=r;
  }
  assert(crossings==2&&maxStep<.05&&maxBend<.0008);
  near(shown(12*3600).moon.elevation,-kDiveDepth);near(shown(2*3600).sun.elevation,-kDiveDepth);
  near(shown(kSunsetSeconds).sun.elevation,0,1e-9);near(shown(kMoonriseSeconds).moon.elevation,0,1e-9);near(shown(kMoonsetSeconds).moon.elevation,0,1e-9);
  std::printf("PASS displayed orbit: 2 crossings 8-10 deg under the horizon, max step %.4f deg/s, max bend %.6f deg/s^2 under the horizon\n",maxStep,maxBend);
 }
 std::printf("PASS native solar table: 432000 samples, rise %.6f s / set %.6f s, max steps sun %.6f / moon %.6f deg/s\n",kSunriseSeconds,kSunsetSeconds,maxStep[0],maxStep[1]);
 std::puts("PASS five daily rise/set/crest cycles; stable nocturnal moon; 50000 shuffled queries; smoothing at 30/60/144 Hz; frame-rate invariance, clock jumps, midnight and timer wrap");
}
