#pragma once
#include "draw_snapshot.h"
#include "displacement_geometry.h"
#include "displacement_shader_patch.h"
#include "actor_texture.h"
#include "material_height.h"
#include "replay_bounds.h"
#include "upload_lock.h"
#include <memory>
#include <cfloat>

class NorthlightAutoDisplacement {
    using Program=NorthlightActorDeformation::Program;
    using Mesh=NorthlightDrawSnapshot::Mesh;
    using Height=NorthlightDisplacementGeometry::Height;
    struct Shader {IDirect3DVertexShader9* patched=nullptr,*source=nullptr;NorthlightDisplacementPatch::Binding binding;};
    struct Material {Program uv,view,uv1,blend;bool layered=false,skinned=false;uint64_t id=0;unsigned first=0,count=0,influences=0;};
    struct Bounds {std::shared_ptr<const Mesh> source;float low[3],high[3];uint64_t used=0;std::unique_ptr<NorthlightReplayBounds::SkinEnvelope> skin;};
    struct Texture {IDirect3DTexture9* source=nullptr;Height height;uint64_t used=0,version=0;};
    struct Entry {
        std::shared_ptr<const Mesh> source;IDirect3DTexture9* texture=nullptr,*texture1=nullptr;
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexDeclaration9* decl=nullptr,*sourceDecl=nullptr;
        std::vector<float> uvConstants;unsigned vertices=0,primitives=0,stride=0,detail=0,address=0,domain=0,material=0;size_t bytes=0;uint64_t used=0,program=0,baseFrame=0,heightVersion=0,heightVersion1=0;
        ~Entry(){drop(texture);drop(texture1);drop(vb);drop(ib);drop(decl);drop(sourceDecl);}
    };
    IDirect3DDevice9* d;
    NorthlightDrawSnapshot::Frame snapshots{2u<<20,0};
    std::unordered_map<IDirect3DVertexShader9*,Shader> shaders;
    std::unordered_map<IDirect3DVertexShader9*,Material> materials;
    std::vector<Texture> textures;
    std::unordered_map<const Mesh*,Bounds> bounds;
    std::vector<std::unique_ptr<Entry>> entries;
    Entry* current=nullptr;float controls[4][4]={};
    float strength=.12f,range=12,worldStrength=.12f,worldRange=12,modelStrength=.03f,modelRange=8;unsigned worldDetail=4,modelDetail=2,polarity=0,heightDetails=75,heightBalance=50;unsigned detail=4,buildsThisFrame=0,readsThisFrame=0,poseVerticesThisFrame=0;
    size_t resident=0,boundsBytes=0;uint64_t clock=0,materialClock=0;unsigned builds=0,hits=0,draws=0,triangles=0,refused=0,frames=0;bool userDraw=false,indexedUserDraw=false;
    static std::vector<float> uvKey(const Material& m,const float* constants){
        bool used[256]={};for(const auto* p:{&m.uv,&m.uv1,&m.blend})for(const auto& op:p->operations)for(const auto& s:op.source){if(NorthlightShadowShader::regType(s.token)!=2)continue;const auto index=NorthlightShadowShader::regIndex(s.token);bool defined=false;for(const auto& def:p->definitions)defined|=def.reg==index;if(!defined&&index<256)used[index]=true;}
        std::vector<float> key;for(unsigned i=0;i<256;++i)if(used[i]){key.push_back(float(i));key.insert(key.end(),constants+i*4,constants+i*4+4);}return key;
    }
    const Height* height(IDirect3DTexture9* source){
        const auto revision=NorthlightMaterialHeight::version(source);
        for(auto it=textures.begin();it!=textures.end();++it)if(it->source==source){if(it->version!=revision){drop(it->source);textures.erase(it);break;}it->used=++clock;return it->height.width?&it->height:nullptr;}
        if(readsThisFrame>=2)return nullptr;++readsThisFrame;
        auto reject=[&]()->const Height*{if(textures.size()>=32){auto old=std::min_element(textures.begin(),textures.end(),[](const Texture& a,const Texture& b){return a.used<b.used;});drop(old->source);textures.erase(old);}Texture failed;failed.source=source;source->AddRef();failed.used=++clock;failed.version=revision;textures.push_back(std::move(failed));return nullptr;};
        NorthlightMaterialHeight::Image image;if(!NorthlightMaterialHeight::read(source,image))return reject();
        if(textures.size()>=32){auto oldest=std::min_element(textures.begin(),textures.end(),[](const Texture& a,const Texture& b){return a.used<b.used;});drop(oldest->source);textures.erase(oldest);}
        Texture item;item.source=source;source->AddRef();item.used=++clock;item.version=revision;item.height.width=image.width;item.height.height=image.height;item.height.values.resize(size_t(image.width)*image.height);
        const auto profile=NorthlightMaterialHeight::profile(image,polarity,heightDetails,heightBalance);
        for(size_t i=0;i<item.height.values.size();++i)item.height.values[i]=NorthlightMaterialHeight::value(image.rgba[i*4]/255.f,image.rgba[i*4+1]/255.f,image.rgba[i*4+2]/255.f,profile);
        textures.push_back(std::move(item));return &textures.back().height;
    }
    bool viewRows(const Program& program,const float* constants){
        using Four=NorthlightActorDeformation::Four;Four input[16]={};unsigned position=16;
        for(auto i:program.inputs){if(i.usage==D3DDECLUSAGE_POSITION&&!i.index)position=i.reg;else return false;}
        if(position==16)return false;input[position]={0,0,0,1};Four origin;if(!NorthlightActorDeformation::evaluateFast(program,input,constants,origin))return false;
        for(unsigned r=0;r<3;++r)controls[r][3]=origin[r];
        for(unsigned k=0;k<3;++k){input[position]={0,0,0,1};input[position][k]=1;Four v;if(!NorthlightActorDeformation::evaluateFast(program,input,constants,v))return false;for(unsigned r=0;r<3;++r)controls[r][k]=v[r]-origin[r];}
        const float a=range*range,b=a*.5625f;controls[3][0]=strength;controls[3][1]=-1/(a-b);controls[3][2]=a/(a-b);controls[3][3]=0;return true;
    }
public:
    struct Args {NorthlightDrawSnapshot::Draw draw;const void* indices=nullptr;D3DFORMAT format=D3DFMT_UNKNOWN;const void* vertices=nullptr;UINT stride=0;};
    explicit NorthlightAutoDisplacement(IDirect3DDevice9* device):d(device){textures.reserve(32);snapshots.setSnapshotCacheLimit(16u<<20);snapshots.setChargeResidentHits(false);}
    ~NorthlightAutoDisplacement(){reset();for(auto& s:shaders){drop(s.second.patched);drop(s.second.source);}}
    void tracking(uint64_t(*identity)(void*,bool),uint64_t(*version)(void*,bool),NorthlightCaptureMetadata::Read metadata){snapshots.setIdentityProvider(identity);snapshots.setVersionProvider(version);snapshots.setMetadataProvider(metadata);}
    void configure(const NorthlightQuality::Settings& s){
        current=nullptr;if(worldDetail!=s.autoDisplacementDetail||modelDetail!=s.autoModelDisplacementDetail||polarity!=s.autoHeightPolarity||heightDetails!=s.autoHeightDetails||heightBalance!=s.autoHeightBalance)reset();
        worldDetail=s.autoDisplacementDetail;modelDetail=s.autoModelDisplacementDetail;worldStrength=s.autoDisplacementStrength*.0012f;worldRange=float(s.autoDisplacementDistance);modelStrength=s.autoModelDisplacementStrength*.0012f;modelRange=float(s.autoModelDisplacementDistance);
        polarity=s.autoHeightPolarity;heightDetails=s.autoHeightDetails;heightBalance=s.autoHeightBalance;strength=worldStrength;range=worldRange;detail=worldDetail;
        if(!s.autoBump||(s.autoBumpTerrain!=3&&s.autoBumpWalls!=3&&s.autoBumpGrass!=3&&s.autoBumpModels!=3))reset();
    }
    bool hasDisplacement()const{return worldStrength>0||modelStrength>0;}
    bool prepared()const{return current!=nullptr;}
    bool skinned(IDirect3DVertexShader9* original)const{auto it=materials.find(original);return it!=materials.end()&&it->second.skinned;}
    unsigned preparedPrimitives()const{return current?current->primitives:0;}
    void beginDraw(){current=nullptr;}
    void reset(){current=nullptr;entries.clear();bounds.clear();boundsBytes=resident=0;for(auto& t:textures)drop(t.source);textures.clear();snapshots.clearIndexCache();}
    void endFrame(){current=nullptr;buildsThisFrame=readsThisFrame=poseVerticesThisFrame=0;snapshots.clearFrame();if(draws&&frames%600==0)logf("DISPLACEMENT cached=%zu bytes=%zu builds=%u hits=%u draws=%u triangles=%u refused=%u",entries.size(),resident,builds,hits,draws,triangles,refused);++frames;}
    void registerVariant(IDirect3DVertexShader9* original,const DWORD* code,size_t count,bool skinned=false){
        auto old=shaders.find(original);if(old!=shaders.end()){drop(old->second.patched);drop(old->second.source);shaders.erase(old);}std::vector<uint32_t> patched;Shader shader;
        if(NorthlightDisplacementPatch::patch(reinterpret_cast<const uint32_t*>(code),count,patched,shader.binding,skinned)){const HRESULT result=d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader.patched);if(FAILED(result))logf("DISPLACEMENT vertex shader rejected: 0x%08lx model=%08lx",static_cast<unsigned long>(result),static_cast<unsigned long>(code[0]));}
        shader.source=original;original->AddRef();shaders.emplace(original,shader);
    }
    void registerMaterial(IDirect3DVertexShader9* original,const DWORD* code,size_t count,unsigned domain){
        materials.erase(original);if(domain>4)return;Material m;const auto* words=reinterpret_cast<const uint32_t*>(code);
        if(!NorthlightActorDeformation::compile(words,count,m.uv,true)||!NorthlightActorDeformation::compile(words,count,m.view,false,(domain==1||domain==3)?1:2))return;
        m.skinned=m.view.skinned;m.influences=m.skinned?1:0;
        for(const auto& input:m.view.inputs)if(input.usage==D3DDECLUSAGE_BLENDWEIGHT){unsigned channels=0;for(const auto& op:m.view.operations)for(const auto& source:op.source)if(NorthlightShadowShader::regType(source.token)==1&&NorthlightShadowShader::regIndex(source.token)==input.reg){unsigned mask=op.code==9?15:op.code==8?7:(op.destination>>16)&15;for(unsigned c=0;c<4;++c)if(mask&(1u<<c))channels=std::max(channels,1u+((source.token>>(16+2*c))&3));}m.influences=std::clamp(channels,1u,4u);}
m.layered=NorthlightActorDeformation::compile(words,count,m.uv1,true,2,5,1)&&NorthlightActorDeformation::compile(words,count,m.blend,true,2,10,1,8);
        unsigned first=256,last=0;for(const auto* p:{&m.uv,&m.view,&m.uv1,&m.blend})for(const auto& op:p->operations)for(const auto& source:op.source){if(NorthlightShadowShader::regType(source.token)!=2)continue;unsigned i=NorthlightShadowShader::regIndex(source.token);bool defined=false;for(const auto& def:p->definitions)defined|=def.reg==i;if(!defined){first=std::min(first,i);last=std::max(last,i);}}
        if(first>last||last>=256)return;m.first=m.skinned?0:first;m.count=m.skinned?256:last-first+1;m.id=++materialClock;
        const bool skinned=m.skinned;materials.emplace(original,std::move(m));registerVariant(original,code,count,skinned);
    }
    bool prepare(IDirect3DVertexShader9* original,unsigned domain,unsigned material,const Args& args){
        current=nullptr;strength=domain==4?modelStrength:worldStrength;range=domain==4?modelRange:worldRange;detail=domain==4?modelDetail:domain==3?std::min(worldDetail,3u):worldDetail;auto shader=materials.find(original);auto patched=shaders.find(original);if(shader==materials.end()||strength<=0||domain>4||patched==shaders.end()||!patched->second.patched)return false;
        DWORD z=0,write=0,blendState=0;d->GetRenderState(D3DRS_ZENABLE,&z);d->GetRenderState(D3DRS_ZWRITEENABLE,&write);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blendState);
        if(!z||(!write&&domain!=1)||(blendState&&!(domain==1&&material==2)))return false;
        float constants[256*4]={};if(FAILED(d->GetVertexShaderConstantF(shader->second.first,constants+shader->second.first*4,shader->second.count))||(!shader->second.skinned&&!viewRows(shader->second.view,constants)))return false;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;if(FAILED(d->GetVertexDeclaration(decl.out()))||!decl.p)return false;
        D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1];UINT count=MAXD3DDECLLENGTH+1;if(FAILED(decl->GetDeclaration(elements,&count))||count>MAXD3DDECLLENGTH)return false;
        Mesh local;std::shared_ptr<const Mesh> source;
        if(args.vertices){if(!snapshots.readUP(decl.p,args.draw,args.indices,args.format,args.vertices,args.stride,local))return false;}
        else if(!snapshots.read(d,decl.p,args.draw,local,nullptr,false,&source))return false;
        if(!source)source=std::make_shared<Mesh>(std::move(local));
        auto sameSource=[&](const Entry& e){return e.source==source||((args.vertices||(domain==1&&material==2))&&e.source->vertexCount==source->vertexCount&&e.source->primitiveCount==source->primitiveCount&&e.source->topology==source->topology&&e.source->indexed==source->indexed&&e.source->streams[0].bytes==source->streams[0].bytes&&e.source->indices==source->indices);};
        if(domain==1&&material!=2)for(auto& e:entries)if(sameSource(*e)&&e->sourceDecl==decl.p&&e->domain==domain)e->baseFrame=0;
        // Reject whole draws outside the sphere before decoding textures or subdividing.
        const auto position=std::find_if(elements,elements+count,[](const D3DVERTEXELEMENT9& e){return e.Stream==0&&e.Usage==0&&e.UsageIndex==0;});
        if(position==elements+count||position->Type!=D3DDECLTYPE_FLOAT3)return false;
        const auto& stream=source->streams[0];auto foundBounds=bounds.find(source.get());
        if(foundBounds==bounds.end()){
            Bounds b;b.source=source;for(unsigned i=0;i<3;++i){b.low[i]=FLT_MAX;b.high[i]=-FLT_MAX;}
            for(unsigned i=0;i<source->vertexCount;++i){if(size_t(i)*stream.stride+position->Offset+12>stream.bytes.size())return false;float p[3];std::memcpy(p,stream.bytes.data()+size_t(i)*stream.stride+position->Offset,12);for(unsigned k=0;k<3;++k){if(!std::isfinite(p[k]))return false;b.low[k]=std::min(b.low[k],p[k]);b.high[k]=std::max(b.high[k],p[k]);}}
            if(shader->second.view.kernel.skin){auto envelope=std::make_unique<NorthlightReplayBounds::SkinEnvelope>();bool valid=true;
                for(unsigned i=0;i<source->vertexCount&&valid;++i){NorthlightActorDeformation::Four input[16];valid=NorthlightActorDeformation::sampledVertexInputsAt(shader->second.view,*source,elements,count,i,input)&&envelope->add(input);}if(valid)b.skin=std::move(envelope);}
            const auto bytes=source->byteSize();if(bytes>(8u<<20))return false;
            while(!bounds.empty()&&(boundsBytes+bytes>(8u<<20)||bounds.size()>=1024)){auto oldest=std::min_element(bounds.begin(),bounds.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});boundsBytes-=oldest->second.source->byteSize();bounds.erase(oldest);}
            boundsBytes+=bytes;foundBounds=bounds.emplace(source.get(),std::move(b)).first;
        }
        float low[3],high[3];
        const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};NorthlightReplayBounds::Bounds skinBounds;
        if(shader->second.skinned&&foundBounds->second.skin&&foundBounds->second.skin->evaluate(constants,identity,skinBounds)){foundBounds->second.used=++clock;std::copy(skinBounds.low,skinBounds.low+3,low);std::copy(skinBounds.high,skinBounds.high+3,high);}
        else if(shader->second.skinned){if(source->vertexCount>8192-poseVerticesThisFrame)return false;poseVerticesThisFrame+=source->vertexCount;for(unsigned k=0;k<3;++k){low[k]=FLT_MAX;high[k]=-FLT_MAX;}
            for(unsigned i=0;i<source->vertexCount;++i){NorthlightActorDeformation::Four input[16],view;if(!NorthlightActorDeformation::sampledVertexInputsAt(shader->second.view,*source,elements,count,i,input)||!NorthlightActorDeformation::evaluateFast(shader->second.view,input,constants,view))return false;
                for(unsigned k=0;k<3;++k){if(!std::isfinite(view[k]))return false;low[k]=std::min(low[k],view[k]);high[k]=std::max(high[k],view[k]);}}
        }else{foundBounds->second.used=++clock;const auto& b=foundBounds->second;
            for(unsigned r=0;r<3;++r){low[r]=high[r]=controls[r][3];for(unsigned k=0;k<3;++k){const float a=controls[r][k]*b.low[k],z=controls[r][k]*b.high[k];low[r]+=std::min(a,z);high[r]+=std::max(a,z);}}}
        float closestSquared=0;for(unsigned r=0;r<3;++r){float v=low[r]>0?low[r]:high[r]<0?high[r]:0;closestSquared+=v*v;}if(closestSquared>=range*range)return false;
        if(shader->second.skinned){const float a=range*range,b=a*.5625f;controls[0][0]=strength*std::clamp((a-closestSquared)/(a-b),0.f,1.f);controls[0][1]=controls[0][2]=controls[0][3]=0;}
        NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> tex,tex1;if(FAILED(d->GetTexture(0,tex.out()))||!tex.p||tex->GetType()!=D3DRTYPE_TEXTURE)return false;
        if(domain==2&&material==6&&(FAILED(d->GetTexture(1,tex1.out()))||!tex1.p||tex1->GetType()!=D3DRTYPE_TEXTURE||!shader->second.layered))return false;
        DWORD u=0,v=0;d->GetSamplerState(0,D3DSAMP_ADDRESSU,&u);d->GetSamplerState(0,D3DSAMP_ADDRESSV,&v);if((u!=D3DTADDRESS_WRAP&&u!=D3DTADDRESS_CLAMP)||(v!=D3DTADDRESS_WRAP&&v!=D3DTADDRESS_CLAMP))return false;
        const auto revision=NorthlightMaterialHeight::version(static_cast<IDirect3DTexture9*>(tex.p)),revision1=tex1.p?NorthlightMaterialHeight::version(static_cast<IDirect3DTexture9*>(tex1.p)):0;
        const unsigned address=u|(v<<8);auto key=uvKey(shader->second,constants);
        Entry* overlay=nullptr;
        for(auto& e:entries){if(sameSource(*e)&&e->sourceDecl==decl.p&&e->domain==domain){
                if(domain==1&&material==2){if(e->baseFrame==uint64_t(frames)+1&&(!overlay||e->used>overlay->used))overlay=e.get();}
                else if(e->program==shader->second.id&&e->detail==detail&&e->address==address&&e->material==material&&e->texture==tex.p&&e->texture1==tex1.p&&e->uvConstants==key&&e->heightVersion==revision&&e->heightVersion1==revision1){current=e.get();userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;e->used=++clock;e->baseFrame=uint64_t(frames)+1;++hits;return true;}}}
        if(overlay){current=overlay;userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;overlay->used=++clock;++hits;return true;}
        if((domain==1&&material==2)||buildsThisFrame>=2){++refused;return false;}
        auto* a=height(static_cast<IDirect3DTexture9*>(tex.p));if(!a)return false;Height first=*a;const Height* second=nullptr;
        if(tex1.p){second=height(static_cast<IDirect3DTexture9*>(tex1.p));if(!second)return false;}
        std::vector<std::array<float,2>> uv,uv1;std::vector<float> weights;
        if(!NorthlightActorDeformation::textureUVs(shader->second.uv,*source,elements,count,constants,uv))return false;
        if(second){if(!NorthlightActorDeformation::textureUVs(shader->second.uv1,*source,elements,count,constants,uv1))return false;
            weights.resize(source->vertexCount);for(unsigned i=0;i<source->vertexCount;++i){NorthlightActorDeformation::Four inputs[16],value;if(!NorthlightActorDeformation::sampledVertexInputsAt(shader->second.blend,*source,elements,count,i,inputs,true)||!NorthlightActorDeformation::evaluateFast(shader->second.blend,inputs,constants,value))return false;if(!std::isfinite(value[3]))return false;weights[i]=value[3];}}
        if(source->primitiveCount>8192)return false;
        ++buildsThisFrame;Mesh output;if(!NorthlightDisplacementGeometry::build(*source,elements,count,uv,first,1u<<detail,domain==1,u==D3DTADDRESS_CLAMP,v==D3DTADDRESS_CLAMP,output,second,second?&uv1:nullptr,second?&weights:nullptr,shader->second.influences))return false;
        if(builds<8){float peak=0;for(unsigned i=0;i<output.vertexCount;++i){float delta[3];std::memcpy(delta,output.streams[0].bytes.data()+size_t(i)*output.streams[0].stride+stream.stride,12);peak=std::max(peak,std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2])*strength);}logf("DISPLACEMENT build domain=%u vertices=%u triangles=%u maxOffset=%.5f VScontrol=%u",domain,output.vertexCount,output.primitiveCount,peak,shaders.find(original)->second.binding.constant);}
        auto entry=std::make_unique<Entry>();entry->bytes=output.byteSize()+source->byteSize();constexpr size_t budget=64u<<20;
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(entry->bytes>budget||!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<entry->bytes+(160u<<20)){++refused;return false;}
        while(!entries.empty()&&(resident+entry->bytes>budget||entries.size()>=256)){auto old=std::min_element(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a->used<b->used;});resident-=(*old)->bytes;entries.erase(old);}
        const auto vbBytes=UINT(output.streams[0].bytes.size()),ibBytes=UINT(output.indices.size()*4);void* data=nullptr;
        if(FAILED(d->CreateVertexBuffer(vbBytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&entry->vb,nullptr))||FAILED(entry->vb->Lock(0,vbBytes,&data,NorthlightUpload::FreshBufferLock)))return false;std::memcpy(data,output.streams[0].bytes.data(),vbBytes);if(FAILED(entry->vb->Unlock()))return false;
        if(FAILED(d->CreateIndexBuffer(ibBytes,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&entry->ib,nullptr))||FAILED(entry->ib->Lock(0,ibBytes,&data,NorthlightUpload::FreshBufferLock)))return false;std::memcpy(data,output.indices.data(),ibBytes);if(FAILED(entry->ib->Unlock()))return false;
        elements[count-1]={0,WORD(stream.stride),D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,7};elements[count]=D3DDECL_END();if(FAILED(d->CreateVertexDeclaration(elements,&entry->decl)))return false;
        entry->source=source;entry->sourceDecl=decl.p;entry->sourceDecl->AddRef();entry->texture=static_cast<IDirect3DTexture9*>(tex.p);entry->texture->AddRef();entry->texture1=static_cast<IDirect3DTexture9*>(tex1.p);if(entry->texture1)entry->texture1->AddRef();entry->detail=detail;entry->program=shader->second.id;entry->domain=domain;entry->material=material;entry->uvConstants=std::move(key);entry->address=address;entry->heightVersion=revision;entry->heightVersion1=revision1;entry->vertices=output.vertexCount;entry->primitives=output.primitiveCount;entry->stride=output.streams[0].stride;entry->used=++clock;entry->baseFrame=uint64_t(frames)+1;resident+=entry->bytes;current=entry.get();userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;entries.push_back(std::move(entry));++builds;return true;
    }
    template<class Draw> HRESULT draw(Draw original,bool native=false){
        if(!current)return original();
        NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> vs;if(FAILED(d->GetVertexShader(vs.out()))||!vs.p)return original();auto shader=shaders.find(vs.p);
        // World terrain-coverage variants keep the native input layout but are
        // created by a separate subsystem. Patch each live variant once.
        if(shader==shaders.end()&&shaders.size()<4096){UINT bytes=0;if(SUCCEEDED(vs->GetFunction(nullptr,&bytes))&&bytes&&bytes<=65536*4){std::vector<DWORD> words(bytes/4);if(SUCCEEDED(vs->GetFunction(words.data(),&bytes))){bool relative=false;for(auto word:words)if((word&0x80002000u)==0x80002000u&&NorthlightShadowShader::regType(word)==2)relative=true;registerVariant(vs.p,words.data(),words.size(),relative);}}shader=shaders.find(vs.p);}
        if(shader==shaders.end()||!shader->second.patched)return original();
        HRESULT hr=D3D_OK;bool bound=false;
        {
        struct State {IDirect3DDevice9* d;NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9> vb;NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> ib;NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;UINT offset=0,stride=0,constant=0,constantCount=4;float values[16];bool changed=false;IDirect3DVertexShader9* vs;
            ~State(){if(changed){d->SetStreamSource(0,vb.p,offset,stride);d->SetIndices(ib.p);d->SetVertexDeclaration(decl.p);d->SetVertexShader(vs);d->SetVertexShaderConstantF(constant,values,constantCount);}}} saved{d,{}, {}, {},0,0,shader->second.binding.constant,shader->second.binding.count,{},false,vs.p};
        if(FAILED(d->GetStreamSource(0,saved.vb.out(),&saved.offset,&saved.stride))||FAILED(d->GetIndices(saved.ib.out()))||FAILED(d->GetVertexDeclaration(saved.decl.out()))||FAILED(d->GetVertexShaderConstantF(saved.constant,saved.values,saved.constantCount)))return original();
        saved.changed=true;
        bound=SUCCEEDED(d->SetStreamSource(0,current->vb,0,current->stride))&&SUCCEEDED(d->SetIndices(current->ib))&&SUCCEEDED(d->SetVertexDeclaration(current->decl))&&SUCCEEDED(d->SetVertexShader(shader->second.patched))&&SUCCEEDED(d->SetVertexShaderConstantF(saved.constant,controls[0],saved.constantCount));
        if(bound){hr=d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,current->vertices,0,current->primitives);if(SUCCEEDED(hr)){++draws;triangles+=current->primitives;}}
        }
        if(!bound)return original();
        if(native&&userDraw){d->SetStreamSource(0,nullptr,0,0);if(indexedUserDraw)d->SetIndices(nullptr);}
        return hr;
    }
};
