#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
#include "m2_shader_families.h"
namespace NorthlightWaterRenderer {enum UserPointer:unsigned{NoUserPointer,UserVertices,UserVerticesAndIndices};}
namespace NorthlightAutoDisplacement {
struct Args {struct {D3DPRIMITIVETYPE topology;INT base;UINT minimum,vertices,start,primitives;bool indexed;}draw;const void* indices=nullptr;D3DFORMAT format=D3DFMT_UNKNOWN;const void* vertices=nullptr;UINT stride=0;};
struct CaptureSource {unsigned mesh=0,pose=0;IDirect3DVertexDeclaration9* declaration=nullptr;};
}
namespace NorthlightCaptureMetadata {struct PositionBounds {const void* owner=nullptr;IDirect3DVertexShader9* shader=nullptr;bool known=false;};}
namespace NorthlightSkyComposition {enum class Role{Unknown,Sky};}
namespace NorthlightWorldDrawDomain {enum class Band{World,Sky};inline Band classify(float,float,float lo,float){return lo>.9f?Band::Sky:Band::World;}}
namespace NorthlightWeather {enum class Kind{Rain,None};}
namespace NorthlightRenderThreadProbe {inline bool sampleFrame(unsigned f){return f%2==0;}}
namespace NorthlightParticleCapture {struct Prepared{bool blocked=false;};}
namespace NorthlightSpellLayer {
struct DrawArgs {
 template<class... A>static DrawArgs primitive(A...){return {};}
 template<class... A>static DrawArgs indexed(A...){return {};}
 template<class... A>static DrawArgs primitiveUP(A...){return {};}
 template<class... A>static DrawArgs indexedUP(A...){return {};}
};
}
struct ExtensionDevice {struct RawScope {template<class T>explicit RawScope(T&){}};};
enum Event {Lock=1,Unlock,Raw,Wrapped,WorldCapture,WorldUP,Material,Normal,Water,BeginBump,BeforeNative,Resume,SetFrame,Effect,Actor,HdrAdmission,DropBlob,DropTerrain,First,Prepare,SkyBefore,SkyClaim,Blob,Particle,Spell,SkyReplay,WaterMrt,NameMask,HdrReplay,Invalidate,SubmitParticle,Observe,RainUnbind,Mist,Stage,Fault};
using Record=std::array<int64_t,12>;
struct Config {
 bool enabled=true,failed=false,applied=false,diagnostics=true,gates=true,world=true,material=true,materialActive=true,normals=true,normalDemand=true;
 bool terrain=false,hdr=false,linearSky=false,claimed=false,blob=false,blobClaim=false,particle=false,spell=false,defer=false,mist=false,rain=false;
 bool skyRole=false,skyReplay=false,hdrReplay=false,waterReplay=false,capturedSky=false,waterBound=false,rainBound=false,projection=true,skyGate=true;
 bool componentDraw=false,missingShader=false,sceneCurrent=true;HRESULT nativeResult=D3D_OK,resumeResult=D3D_OK;unsigned faultAt=0;
};
struct Env {
 Config cfg;std::vector<Record> trace;uint64_t hash=0;unsigned actions=0,raws=0;bool record=true;const void* owner=nullptr;
 void event(Event e,std::initializer_list<int64_t> args={}){Record row{};row[0]=e;unsigned i=1;for(auto a:args){assert(i<row.size());row[i++]=a;}for(auto a:row)hash=(hash^uint64_t(a))*1099511628211ull;if(record)trace.push_back(row);}
 void action(Event e,std::initializer_list<int64_t> args={}){event(e,args);if(++actions==cfg.faultAt)throw std::runtime_error("injected");}
};
struct Gate {Env* e;unsigned drawTid=7;void noteFirst(unsigned t){e->event(First,{t});}};
struct Guard {Gate& g;explicit Guard(Gate& v):g(v){g.e->event(Lock);}~Guard(){g.e->event(Unlock);}};
struct CpuScope {LONGLONG* value;explicit CpuScope(LONGLONG* v):value(v){}~CpuScope(){if(value)++*value;}};
static IDirect3DVertexShader9* shader(){return reinterpret_cast<IDirect3DVertexShader9*>(1);}
struct Ext {
 Env* e;
 HRESULT draw(unsigned form,D3DPRIMITIVETYPE t,INT base,UINT minimum,UINT vertices,UINT start,UINT count,const void* indices,D3DFORMAT format,const void* data,UINT stride){++e->raws;e->event(Raw,{form,t,base,minimum,vertices,start,count,int64_t(uintptr_t(indices)),format,int64_t(uintptr_t(data)),stride});return e->cfg.nativeResult;}
 HRESULT DrawPrimitive(D3DPRIMITIVETYPE t,UINT start,UINT count){return draw(0,t,0,0,0,start,count,nullptr,D3DFMT_UNKNOWN,nullptr,0);}
 HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT base,UINT minimum,UINT vertices,UINT start,UINT count){return draw(1,t,base,minimum,vertices,start,count,nullptr,D3DFMT_UNKNOWN,nullptr,0);}
 HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT count,const void* data,UINT stride){return draw(2,t,0,0,0,0,count,nullptr,D3DFMT_UNKNOWN,data,stride);}
 HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT minimum,UINT vertices,UINT count,const void* indices,D3DFORMAT format,const void* data,UINT stride){return draw(3,t,0,minimum,vertices,0,count,indices,format,data,stride);}
};
struct World {
 Env* e;bool terrainShader(IDirect3DVertexShader9*)const{return e->cfg.terrain;}bool rainBlendSetting()const{return true;}
 void capture(D3DPRIMITIVETYPE t,INT base,UINT minimum,UINT vertices,UINT start,UINT count,bool indexed,IDirect3DVertexShader9* vs,bool report,bool sample,NorthlightCaptureMetadata::PositionBounds* out=nullptr,const unsigned* mesh=nullptr,IDirect3DVertexDeclaration9* decl=nullptr){
  assert(vs==shader());if(out){assert(out->owner&&out->shader==vs);e->owner=out->owner;out->known=true;}
  e->action(WorldCapture,{t,base,minimum,vertices,start,count,indexed,report,sample,bool(out),mesh?*mesh:0});assert(!decl||mesh&&*mesh==17);
 }
 void captureUP(D3DPRIMITIVETYPE t,UINT minimum,UINT vertices,UINT count,const void* indices,D3DFORMAT format,const void* data,UINT stride,bool indexed,IDirect3DVertexShader9* vs,bool report,bool sample){assert(vs==shader());e->action(WorldUP,{t,minimum,vertices,count,int64_t(uintptr_t(indices)),format,int64_t(uintptr_t(data)),stride,indexed,report,sample});}
};
struct Bump {
 Env* e;bool active()const{return e->cfg.materialActive;}void beginDraw(){e->event(BeginBump);}void beforeNativeDraw(){e->event(BeforeNative);}
 template<class Draw>void capture(IDirect3DVertexShader9* vs,Draw draw,bool up,const NorthlightAutoDisplacement::Args* g,const NorthlightCaptureMetadata::PositionBounds* pos,NorthlightAutoDisplacement::CaptureSource* source){
  assert(vs==shader()&&g&&source);if(pos)assert(pos->owner==g&&pos->shader==vs);if(e->owner)assert(e->owner==g);e->action(Material,{up,bool(pos),pos&&pos->known,g->draw.indexed});source->mesh=17;source->pose=23;source->declaration=reinterpret_cast<IDirect3DVertexDeclaration9*>(2);if(e->cfg.componentDraw)draw();
 }
};
struct Normals {
 Env* e;template<class Draw,class Desc>void capture(IDirect3DVertexShader9* vs,Draw draw,bool up,const Desc*,const void*,D3DFORMAT,const void*,UINT,const unsigned* mesh,IDirect3DVertexDeclaration9* decl,const unsigned* pose){
  assert(vs==shader());assert(mesh&&pose&&(*mesh==0||(*mesh==17&&*pose==23&&decl)));e->action(Normal,{up,*mesh,*pose,bool(decl)});if(e->cfg.componentDraw)draw();
 }
};
struct Base {
 bool auroraDrawProven=true;std::array<float,3> auroraDrawScale{1,2,3};float auroraDrawIntensity=2;
 Env env;Ext extStorage{&env};Ext* ext=&extStorage;World worldStorage{&env};World* world=&worldStorage;Bump bumpStorage{&env};Bump* autoBump=&bumpStorage;Normals normalStorage{&env};Normals* modelNormals=&normalStorage;
 bool enabled=true,applied=false,failed=false,modelNormalsNeeded=true,frameDrawGates=true,particleOn=false,spellOn=false,skyBandSeen=false,rainMrtBound=false,blobOriginal=false,shadowSwapOriginal=false,extensionFault=false;
 unsigned worldDebug=0,drawCalls=0,frame=120;LONGLONG ticks=0;float worldMinDepth=0,worldMaxDepth=.9f;struct{bool active=false;}hdr;struct{bool valid=false;}waterPending;
 struct WaterState{bool bound=false;bool isMrtBound()const{return bound;}}waterStorage;WaterState* water=&waterStorage;
 struct {bool sky=true,blob=false;}drawGates;struct {Gate gate;bool textureKnown[1]={};const void* textures[1]={};bool viewportKnown=true;struct {float MinZ=0,MaxZ=1;}viewport={};}mirrorState{{&env}};
 struct {const void* hot=nullptr;NorthlightWeather::Kind hotKind=NorthlightWeather::Kind::None;bool mistArmed=false;}weatherDetect;struct {unsigned primitives=0,draws=0;}weatherSample;
 struct SkyLayers{Env* e;void invalidate(){e->event(Invalidate);}}skyLayers{&env};
 struct Scene{Env* e;struct Native{Env* e;bool currentFrame(unsigned)const{return e->cfg.sceneCurrent;}}native{e};void setFrame(unsigned f){e->event(SetFrame,{f});}}sceneStorage{&env};Scene* gpuScene=&sceneStorage;
 std::map<IDirect3DVertexShader9*,uint64_t>vsHashes;NorthlightSpellLayer::DrawArgs spellArgs;
 virtual ~Base()=default;
 virtual HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT)=0;virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)=0;
 virtual HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT)=0;virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)=0;
 void configure(const Config& c){env.cfg=c;enabled=c.enabled;failed=c.failed;applied=c.applied;world=c.world?&worldStorage:nullptr;autoBump=c.material?&bumpStorage:nullptr;modelNormals=c.normals?&normalStorage:nullptr;modelNormalsNeeded=c.normalDemand;frameDrawGates=c.gates;particleOn=c.particle;spellOn=c.spell;hdr.active=c.hdr;drawGates.sky=c.skyGate;drawGates.blob=c.blob;rainMrtBound=c.rainBound;waterStorage.bound=c.waterBound;weatherDetect.mistArmed=c.mist;weatherDetect.hot=c.rain?this:nullptr;weatherDetect.hotKind=c.rain?NorthlightWeather::Kind::Rain:NorthlightWeather::Kind::None;mirrorState.textureKnown[0]=c.rain;mirrorState.textures[0]=this;for(auto& v:kM2ShaderFamilies)if(v.family==3){vsHashes[shader()]=v.hash;break;}}
 bool diagnostics()const{return env.cfg.diagnostics;}HRESULT hdrResumeWorld(){env.event(Resume);return env.cfg.resumeResult;}
 LONGLONG* sampledHookTimer(){return &ticks;}
 template<class Work>bool extensionWork(const char* stage,Work work){if(extensionFault)return false;env.event(Stage,{int64_t(hashString(stage))});try{work();return true;}catch(const std::exception&){env.event(Fault,{int64_t(hashString(stage))});failed=extensionFault=true;enabled=false;return false;}}
 static uint64_t hashString(const char* s){uint64_t h=0;while(*s)h=h*131+unsigned(*s++);return h;}
 void dropBlobFaint(){env.event(DropBlob);blobOriginal=false;}void dropTerrainShadowSwap(){env.event(DropTerrain);shadowSwapOriginal=false;}
 template<class Capture>void prepareDrawImpl(Capture capture,UINT count){++drawCalls;env.owner=nullptr;env.action(Prepare,{count});if(enabled&&!failed&&!applied&&world&&!env.cfg.missingShader)capture(shader());}
 template<class Draw>HRESULT submitGameDraw(UINT count,bool sky,bool* captured,Draw draw){env.event(Wrapped,{count,sky});if(captured)*captured=env.cfg.capturedSky;return draw();}
 template<class Draw>HRESULT nativeEffectDraw(bool rain,Draw draw){env.event(Effect,{rain});return draw();}
 template<class Draw>HRESULT aoActorNativeDraw(Draw draw){env.event(Actor);return draw();}
 bool hdrSkyAdmission(){env.action(HdrAdmission);return env.cfg.linearSky;}
 bool mistDraw(UINT count){env.action(Mist,{count});return env.cfg.mist;}void rainMrtUnbind(){env.event(RainUnbind);rainMrtBound=false;waterStorage.bound=false;}
 bool nextDrawIsWater()const{return env.cfg.waterBound;}
 void skyLayersBefore(D3DPRIMITIVETYPE t,UINT count,NorthlightSkyComposition::Role& role){env.action(SkyBefore,{t,count});role=env.cfg.skyRole?NorthlightSkyComposition::Role::Sky:NorthlightSkyComposition::Role::Unknown;}
 void skyClaim(D3DPRIMITIVETYPE t,UINT count,bool& claimed){env.action(SkyClaim,{t,count});claimed=env.cfg.claimed;}
 bool blobFilterActive()const{return env.cfg.blob;}void blobFilter(UINT count,bool& claimed){env.action(Blob,{count});claimed=env.cfg.blobClaim;}
 NorthlightParticleCapture::Prepared prepareParticleDraw(bool blocked){env.action(Particle,{blocked});return {blocked};}
 void submitParticleDraw(const NorthlightParticleCapture::Prepared& p,bool accepted,bool deferred){env.event(SubmitParticle,{p.blocked,accepted,deferred});}
 bool spellGate()const{return true;}bool spellDraw(bool blocked,bool rain){env.action(Spell,{blocked,rain});return env.cfg.defer;}
 template<class Draw>void skyLayersReplay(NorthlightSkyComposition::Role r,bool draw,Draw raw){env.action(SkyReplay,{int(r),draw});if(env.cfg.skyReplay&&draw)raw();}
 template<class Draw,class Mask>HRESULT waterMrtDraw(Draw,Mask mask){env.event(WaterMrt);return mask();}
 template<class Draw>HRESULT nameMaskDraw(bool rain,bool skip,Draw draw){env.event(NameMask,{rain,skip});return skip?D3D_OK:draw();}
 template<class Draw>void hdrReplaySky(bool sky,bool skip,Draw draw){env.action(HdrReplay,{sky,skip});if(env.cfg.hdrReplay&&sky&&!skip)draw();}
 void skyObserve(HRESULT hr,D3DPRIMITIVETYPE t,UINT count){env.action(Observe,{hr,t,count});}
 template<class Draw>void captureWater(IDirect3DVertexShader9* vs,unsigned up,Draw draw){assert(vs==shader());env.action(Water,{up});if(env.cfg.waterReplay)draw();}
};
#include "draw_orchestration_routes.h"
template<class T>static HRESULT dispatch(T& d,unsigned form,unsigned count){const auto* v=reinterpret_cast<const void*>(0x12340);const auto* i=reinterpret_cast<const void*>(0x56780);switch(form){case 0:return d.DrawPrimitive(D3DPT_TRIANGLELIST,13,count);case 1:return d.DrawIndexedPrimitive(D3DPT_TRIANGLELIST,-3,7,19,13,count);case 2:return d.DrawPrimitiveUP(D3DPT_TRIANGLELIST,count,v,48);default:return d.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,7,19,count,i,count&1?D3DFMT_INDEX16:D3DFMT_INDEX32,v,48);}}
static void normalize(std::vector<Record>& rows){for(size_t i=1;i<rows.size();++i)if(rows[i-1][0]==DropBlob&&rows[i][0]==First){auto b=i;while(b+1<rows.size()&&rows[b+1][0]!=DropTerrain)++b;if(b+1<rows.size()){auto value=rows[i-1];rows.erase(rows.begin()+i-1);rows.insert(rows.begin()+b+1,value);}}}
int main(int argc,char**){unsigned checked=0;
 for(unsigned bits=0;bits<2048;++bits)for(unsigned form=0;form<4;++form){Config c;c.gates=bits&1;c.enabled=!(bits&2);c.failed=bits&4;c.applied=bits&8;c.material=!(bits&16);c.materialActive=bits&32;c.normalDemand=bits&64;c.terrain=bits&128;c.componentDraw=bits&256;c.waterReplay=bits&512;c.normals=!(bits&1024);c.skyGate=!(bits&1024);
  for(unsigned feature=0;feature<18;++feature){auto q=c;switch(feature){case 1:q.hdr=q.linearSky=q.hdrReplay=true;break;case 2:q.claimed=true;break;case 3:q.blob=q.blobClaim=true;break;case 4:q.particle=true;break;case 5:q.particle=q.spell=q.defer=true;break;case 6:q.mist=true;break;case 7:q.rain=q.rainBound=true;break;case 8:q.skyRole=q.skyReplay=true;break;case 9:q.nativeResult=D3DERR_INVALIDCALL;q.skyRole=q.particle=q.hdr=true;break;case 10:q.resumeResult=E_FAIL;break;case 11:q.enabled=q.failed=q.diagnostics=false;break;case 12:q.sceneCurrent=false;break;case 13:q.missingShader=true;break;case 14:q.waterBound=true;break;case 15:q.faultAt=1;break;case 16:q.faultAt=3;break;case 17:q.hdr=q.linearSky=q.hdrReplay=q.capturedSky=true;q.faultAt=6;break;}
   Reference before;Current after;before.configure(q);after.configure(q);auto run=[&](auto& d){try{return dispatch(d,form,(bits&256)?12:(bits&128)?3:2);}catch(const std::exception&){return E_UNEXPECTED;}};const auto a=run(before),b=run(after);normalize(before.env.trace);normalize(after.env.trace);
   if(a!=b||before.env.trace!=after.env.trace||before.failed!=after.failed||before.applied!=after.applied||before.enabled!=after.enabled||before.ticks!=after.ticks||before.drawCalls!=after.drawCalls){std::printf("DRAW_ORCHESTRATION_MISMATCH bits=%u form=%u feature=%u hr=%lx/%lx\n",bits,form,feature,long(a),long(b));for(unsigned j=0;j<std::max(before.env.trace.size(),after.env.trace.size());++j){const auto x=j<before.env.trace.size()?before.env.trace[j]:Record{},y=j<after.env.trace.size()?after.env.trace[j]:Record{};if(x!=y)std::printf("event=%u old=%lld new=%lld\n",j,(long long)x[0],(long long)y[0]);}return 1;}++checked;
  }
 }
 if(argc>1){for(unsigned mode=0;mode<3;++mode)for(unsigned round=0;round<12;++round)for(unsigned turn=0;turn<2;++turn){const unsigned current=(turn+round)&1;auto measure=[&](auto& d){Config c;c.enabled=mode!=0;c.diagnostics=mode!=0;c.terrain=mode==2;c.componentDraw=mode==2;d.configure(c);d.env.record=false;auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<20000;++n)dispatch(d,n%4,2);const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::printf("DRAW_ROUTE_TIMING mode=%u round=%u current=%u ms=%.6f hash=%llu native=%u\n",mode,round,current,ms,(unsigned long long)d.env.hash,d.env.raws);};if(current){Current d;measure(d);}else{Reference d;measure(d);}}}
 std::printf("DRAW_ORCHESTRATION_PASS cases=%u fourForms=1 sameArgsOrderHandoffResultFaults=1\n",checked);
}
