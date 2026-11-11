#pragma once
#include "spell_layer.h"
#include "replay_copies.h"
#include "particle_uploads.h"
#include "particle_texture_stats.h"
#include "celestial_context.h"
#include <fstream>
#include <string>
#include <vector>

namespace NorthlightAuroraNative {
// One explicitly requested frame, bounded CPU-copy diagnostics. Never locks a
// GPU buffer, replays a draw, changes device state, or claims a native emitter.
class DiagnosticCapture {
    std::string directory_;
    std::ofstream journal_;
    unsigned frame_=~0u,draws_=0;
    std::size_t bytes_=0;
    static constexpr std::size_t MaxBytes=16*1024*1024;
    static constexpr unsigned MaxDraws=256;
    template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}};
    bool bytes(unsigned draw,const std::string& label,const void* data,std::size_t size){
        if(!data||!size||size>MaxBytes-bytes_)return false;
        const std::string name="aurora-"+std::to_string(draw)+"-"+label+".bin";
        std::ofstream out(directory_+"/"+name,std::ios::binary);
        if(!out.write(static_cast<const char*>(data),std::streamsize(size)))return false;
        bytes_+=size;return true;
    }
    bool buffer(unsigned draw,const std::string& label,const void* key,unsigned offset,unsigned size){
        if(!size||size>1024*1024||size>MaxBytes-bytes_)return false;
        std::vector<unsigned char> data(size);
        if(!NorthlightParticleUploads::cache().read(key,offset,size,data.data())){
            NorthlightReplayCopies::Slot* pin=nullptr;
            const auto* source=NorthlightReplayCopies::read(key,offset,size,pin);
            if(!source)return false;
            std::memcpy(data.data(),source,size);NorthlightReplayCopies::unpin(*pin);
        }
        return bytes(draw,label,data.data(),size);
    }
    template<class T> void shader(unsigned draw,const char* label,T* shader){
        UINT size=0;bool saved=false;
        if(shader&&SUCCEEDED(shader->GetFunction(nullptr,&size))&&size>=8&&size<=65536){
            std::vector<unsigned char> code(size);
            saved=SUCCEEDED(shader->GetFunction(code.data(),&size))&&bytes(draw,label,code.data(),size);
        }
        journal_<<",\""<<label<<"\":"<<(saved?"true":"false");
    }
public:
    void reset(){if(journal_.is_open())journal_.close();directory_.clear();frame_=~0u;draws_=0;bytes_=0;}
    void arm(const std::string& directory){reset();directory_=directory;}
    bool armed()const{return !directory_.empty();}
    // Called for each new renderer frame even if no aurora draws were observed.
    void beginFrame(unsigned frame){
        if(!armed())return;
        if(frame_==~0u){frame_=frame;journal_.open(directory_+"/aurora-native.jsonl",std::ios::binary);
            if(!journal_){reset();return;}
            journal_<<"{\"event\":\"begin\",\"frame\":"<<frame<<",\"mode\":\"native-diagnostic-only\",\"gpu_readback\":false}\n";
        }else if(frame!=frame_){journal_<<"{\"event\":\"end\",\"draws\":"<<draws_<<",\"bytes\":"<<bytes_<<"}\n";reset();}
    }
    void draw(IDirect3DDevice9* d,unsigned frame,const NorthlightSpellLayer::DrawArgs& args,std::uint64_t textureHash){
        if(!armed())return;beginFrame(frame);
        if(!journal_||draws_>=MaxDraws||bytes_>=MaxBytes)return;
        const unsigned draw=draws_++;
        using namespace NorthlightSpellLayer;
        const bool indexed=args.type==DrawType::Indexed||args.type==DrawType::IndexedUP;
        const bool up=args.type==DrawType::PrimitiveUP||args.type==DrawType::IndexedUP;
        journal_<<"{\"event\":\"draw\",\"draw\":"<<draw<<",\"type\":"<<unsigned(args.type)<<",\"topology\":"<<unsigned(args.prim)
            <<",\"primitive_count\":"<<args.primCount<<",\"first\":"<<args.first<<",\"base_vertex\":"<<args.baseVertex
            <<",\"min_index\":"<<args.minIndex<<",\"num_vertices\":"<<args.numVertices<<",\"texture0_hash\":\""<<textureHash<<"\"";
        Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps;d->GetVertexShader(&vs.p);d->GetPixelShader(&ps.p);
        shader(draw,"vs",vs.p);shader(draw,"ps",ps.p);
        float vertex[1024]{},pixel[896]{};
        const bool vc=SUCCEEDED(d->GetVertexShaderConstantF(0,vertex,256))&&bytes(draw,"vs-constants",vertex,sizeof vertex);
        const bool pc=SUCCEEDED(d->GetPixelShaderConstantF(0,pixel,224))&&bytes(draw,"ps-constants",pixel,sizeof pixel);
        journal_<<",\"vs_constants\":"<<(vc?"true":"false")<<",\"ps_constants\":"<<(pc?"true":"false");
        D3DMATRIX matrix{};
        for(auto transform:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION})if(SUCCEEDED(d->GetTransform(transform,&matrix)))
            bytes(draw,"transform-"+std::to_string(unsigned(transform)),&matrix,sizeof matrix);
        NorthlightCelestial::Snapshot sky{};
        if(NorthlightCelestial::skyboxVerified()&&NorthlightCelestial::readSnapshot(NorthlightWorldContext::readSelf,sky))
            bytes(draw,"celestial-snapshot",&sky,sizeof sky);
        DWORD state=0;
        journal_<<",\"render_states\": [";bool comma=false;
        for(auto which:{D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_CULLMODE,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_SRGBWRITEENABLE}){
            if(SUCCEEDED(d->GetRenderState(which,&state))){journal_<<(comma?",":"")<<"["<<unsigned(which)<<","<<state<<"]";comma=true;}}
        journal_<<"],\"samplers\":[";
        for(unsigned s=0;s<2;++s){if(s)journal_<<",";journal_<<"{\"slot\":"<<s<<",\"states\":[";comma=false;
            for(auto which:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE})
                if(SUCCEEDED(d->GetSamplerState(s,which,&state))){journal_<<(comma?",":"")<<"["<<unsigned(which)<<","<<state<<"]";comma=true;}
            journal_<<"]";Ref<IDirect3DBaseTexture9> texture;
            if(SUCCEEDED(d->GetTexture(s,&texture.p))&&texture.p&&texture.p->GetType()==D3DRTYPE_TEXTURE){D3DSURFACE_DESC desc{};
                if(SUCCEEDED(static_cast<IDirect3DTexture9*>(texture.p)->GetLevelDesc(0,&desc)))journal_<<",\"width\":"<<desc.Width<<",\"height\":"<<desc.Height<<",\"format\":"<<unsigned(desc.Format);
                NorthlightParticleTextures::View image;
                if(NorthlightParticleTextures::cache().lookup(texture.p,image)){
                    const bool saved=bytes(draw,"texture-"+std::to_string(s),image.rgba,std::size_t(image.width)*image.height*4);
                    journal_<<",\"mip\":"<<image.mip<<",\"mip_width\":"<<image.width<<",\"mip_height\":"<<image.height
                        <<",\"upload_hash\":\""<<image.uploadHash<<"\",\"rgba_hash\":\""<<image.rgbaHash<<"\",\"texture_saved\":"<<(saved?"true":"false");
                }}
            journal_<<"}";
        }
        journal_<<"],\"streams\":[";
        Ref<IDirect3DVertexDeclaration9> declaration;D3DVERTEXELEMENT9 elements[65]{};UINT count=65;
        bool declared=SUCCEEDED(d->GetVertexDeclaration(&declaration.p))&&declaration.p&&SUCCEEDED(declaration.p->GetDeclaration(elements,&count))&&count<=65;
        if(declared)bytes(draw,"declaration",elements,count*sizeof elements[0]);
        bool used[16]{};if(declared)for(unsigned i=0;i<count&&elements[i].Stream!=0xff;++i)if(elements[i].Stream<16)used[elements[i].Stream]=true;
        const std::int64_t first=indexed?std::int64_t(up?0:args.baseVertex)+args.minIndex:std::int64_t(up?0:args.first);
        const unsigned vertices=indexed?args.numVertices:args.prim==D3DPT_TRIANGLELIST?args.primCount*3:args.primCount+2;
        comma=false;
        for(unsigned stream=0;stream<16;++stream)if(used[stream]){
            Ref<IDirect3DVertexBuffer9> vb;UINT offset=0,stride=up?args.stride:0;bool saved=false;
            if(!up)d->GetStreamSource(stream,&vb.p,&offset,&stride);
            const std::uint64_t start=first>=0?std::uint64_t(offset)+std::uint64_t(first)*stride:UINT64_MAX;
            const std::uint64_t length=std::uint64_t(vertices)*stride;
            if(first>=0&&stride&&stride<=256&&start<=UINT32_MAX&&length<=1024*1024&&start+length<=UINT32_MAX){
                if(up&&stream==0&&args.vertexData)saved=bytes(draw,"vb-0",static_cast<const char*>(args.vertexData)+start,std::size_t(length));
                else if(vb.p)saved=buffer(draw,"vb-"+std::to_string(stream),vb.p,unsigned(start),unsigned(length));
            }
            journal_<<(comma?",":"")<<"{\"slot\":"<<stream<<",\"offset\":"<<offset<<",\"stride\":"<<stride<<",\"first_vertex\":"<<first<<",\"bytes\":"<<length<<",\"saved\":"<<(saved?"true":"false")<<"}";comma=true;
        }
        journal_<<"]";
        if(indexed){Ref<IDirect3DIndexBuffer9> ib;D3DFORMAT format=args.indexFormat;
            if(!up&&SUCCEEDED(d->GetIndices(&ib.p))&&ib.p){D3DINDEXBUFFER_DESC desc{};if(SUCCEEDED(ib.p->GetDesc(&desc)))format=desc.Format;}
            const unsigned stride=format==D3DFMT_INDEX16?2:format==D3DFMT_INDEX32?4:0;
            const std::uint64_t start=std::uint64_t(up?0:args.first)*stride,length=std::uint64_t(args.prim==D3DPT_TRIANGLELIST?args.primCount*3:args.primCount+2)*stride;
            bool saved=false;if(stride&&start+length<=UINT32_MAX&&length<=1024*1024){
                if(up&&args.indexData)saved=bytes(draw,"ib",args.indexData,std::size_t(length));
                else if(ib.p)saved=buffer(draw,"ib",ib.p,unsigned(start),unsigned(length));}
            journal_<<",\"index_format\":"<<unsigned(format)<<",\"indices_saved\":"<<(saved?"true":"false");
        }
        journal_<<"}\n";journal_.flush();
    }
};
}
