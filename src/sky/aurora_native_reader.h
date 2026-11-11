#pragma once
#include "aurora_asset_data.h"
#include "aurora_emission.h"
#include "aurora_source_projector.h"
#include "particle_light_proof.h"
#include "particle_texture_stats.h"
#include "spell_layer.h"
#include <mutex>

namespace NorthlightAuroraNative {
struct Material {
    NorthlightParticleLightProof::Pixel program;
    struct Texture {std::vector<std::uint8_t> rgba;unsigned width=0,height=0,addressU=0,addressV=0;bool srgb=false,linear=false;};
    Texture textures[2];bool sourceAlpha=false;unsigned alphaFunction=8,alphaReference=0;
    bool emission(const NorthlightAuroraSource::NativeVertex& vertex,std::array<float,6>& out)const{
        using namespace NorthlightParticleLightProof;
        Four values[2]{};
        for(unsigned i=0;i<program.samples;++i){const auto& image=textures[i];const auto& uv=vertex.uv[i?program.secondUv:program.uv];
            NorthlightParticleTextures::View view;view.rgba=image.rgba.data();view.width=image.width;view.height=image.height;
            if(!view.sample(uv[0],uv[1],image.srgb,image.addressU,image.addressV,values[i].data(),image.linear))return false;}
        // Barycentric interpolation of a native SAT color can round one ULP
        // above 1. Preserve its proven range before the strict PS evaluator.
        Four color=vertex.color;for(auto& value:color)value=std::clamp(value,0.f,1.f);
        for(auto& sample:values)for(auto& value:sample)value=std::clamp(value,0.f,1.f);
        Four native;if(!shade(program,color,values[0],nullptr,native,program.samples==2?&values[1]:nullptr))return false;
        const float alpha=std::clamp(native[3],0.f,1.f),reference=alphaReference/255.f;
        if((alphaFunction==5&&alpha<=reference)||(alphaFunction==7&&alpha<reference)){out={};return true;}
        const auto additional=NorthlightAuroraSource::additional(native,{1,1,1},sourceAlpha);
        const float factor=sourceAlpha?alpha:1;
        for(unsigned c=0;c<3;++c){const float value=std::max(native[c],0.f);
            const float decoded=value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);
            out[c]=std::max(value,decoded)*factor;out[c+3]=additional[c];}
        return true;
    }
    bool luminance(const NorthlightAuroraSource::NativeVertex& vertex,NorthlightAuroraSource::RGB& out)const{
        std::array<float,6> values{};if(!emission(vertex,values))return false;
        std::copy(values.begin()+3,values.end(),out.begin());return true;
    }
};
struct Packet {
    Material material;
    std::vector<NorthlightAuroraSource::NativeTriangle> triangles;
    std::array<float,1024> constants{};
    std::array<float,16> inverse{};unsigned cull=0;
    unsigned assetBatch=~0u;
    std::uint64_t drawGroup=0;
};
class Reader {
    using Program=NorthlightActorDeformation::Program;
    using Four=NorthlightActorDeformation::Four;
    template<class T> struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}};
    template<class T> static bool shader(T* source,std::vector<std::uint32_t>& code){
        UINT bytes=0;if(!source||FAILED(source->GetFunction(nullptr,&bytes))||bytes<8||bytes>65536||bytes%4)return false;
        code.resize(bytes/4);return SUCCEEDED(source->GetFunction(code.data(),&bytes));
    }
    struct Programs {Program position;NorthlightParticleLightProof::Vertex vertex;NorthlightParticleLightProof::Pixel pixel;bool ready;Programs():ready(false){} };
    inline static Programs programs_;
    inline static std::once_flag programsOnce_;
public:
    // Expand the copied native state on the source worker, never the draw hook.
    static bool expand(Packet& output){
        if(!programs_.ready||output.assetBatch>=sizeof(AssetBatches)/sizeof(AssetBatches[0]))return false;
        const auto& batch=AssetBatches[output.assetBatch];const auto* constants=output.constants.data();
        output.triangles.resize(batch.primitives);
        for(unsigned i=0;i<batch.primitives*3;++i){const auto& input=AssetVertices[AssetIndices[batch.offset+i]];
            Four inputs[16]{};inputs[0]={input.position[0],input.position[1],input.position[2],1};inputs[1]={input.uv[0][0],input.uv[0][1],0,1};inputs[2]={input.uv[1][0],input.uv[1][1],0,1};Four view{};
            if(!NorthlightActorDeformation::evaluateFast<true>(programs_.position,inputs,constants,view))return false;
            auto& triangle=output.triangles[i/3];triangle.cull=output.cull==D3DCULL_CW?1:0;auto& vertex=triangle.vertices[i%3];float xyz[3]{};
            for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)xyz[j]+=view[k]*output.inverse[k*4+j];vertex.position={xyz[0],xyz[1],xyz[2]};
            float uv0[2]{},uv1[2]{};if(!NorthlightParticleLightProof::evaluateVertex(programs_.vertex,inputs,constants,vertex.color,uv0,uv1))return false;
            vertex.uv[0]={uv0[0],uv0[1]};vertex.uv[1]={uv1[0],uv1[1]};
        }return true;
    }
    // Call only for a verified aurora texture candidate in the main sky band,
    // on a bounded source-refresh frame. CPU-upload misses fail open; no Lock.
    static bool read(IDirect3DDevice9* d,const NorthlightSpellLayer::DrawArgs& args,const float inverseView[16],Packet& output,bool geometry=true){
        output={};using namespace NorthlightSpellLayer;
        if(args.prim!=D3DPT_TRIANGLELIST||!args.primCount||args.primCount>4096||!inverseView)return false;
        for(unsigned i=0;i<16;++i)if(!std::isfinite(inverseView[i]))return false;
        Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps;
        if(FAILED(d->GetVertexShader(&vs.p))||FAILED(d->GetPixelShader(&ps.p)))return false;
        std::vector<std::uint32_t> vertexCode,pixelCode;if(!shader(vs.p,vertexCode)||!shader(ps.p,pixelCode))return false;
        std::uint64_t hash=14695981039346656037ull;const auto* bytes=reinterpret_cast<const unsigned char*>(vertexCode.data());
        for(std::size_t i=0;i<vertexCode.size()*4;++i)hash=(hash^bytes[i])*1099511628211ull;
        const unsigned family=m2ShaderFamily(hash);
        std::uint64_t pixelHash=14695981039346656037ull;for(std::size_t i=0;i<pixelCode.size()*4;++i)pixelHash=(pixelHash^reinterpret_cast<const unsigned char*>(pixelCode.data())[i])*1099511628211ull;
        // Audited live fiord capture: unskinned c31..33 position, c6..9 UV,
        // COLOR=saturate(c28+c29), two-texture Mod2x with native fog varying.
        const bool curated=hash==0x6c41afbe5336d3b9ull&&pixelHash==0x7d592fa43d938a6cull;
        if(!curated)return false;
        std::call_once(programsOnce_,[&]{
            programs_.ready=NorthlightActorDeformation::compile(vertexCode.data(),vertexCode.size(),programs_.position,false,2)&&
                NorthlightParticleLightProof::compileVertex(vertexCode.data(),vertexCode.size(),family,programs_.vertex)&&
                NorthlightParticleLightProof::compilePixel(pixelCode.data(),pixelCode.size(),programs_.pixel);
        });
        if(!programs_.ready)return false;output.material.program=programs_.pixel;
        auto& material=output.material;
        // A fogged output needs a separately proven all-one native fog varying;
        // until captured, do not mistake atmospheric color for aurora emission.
        if(material.program.fog&&!curated)return false;
        DWORD blend=0,src=0,dst=0,op=0,test=0,cull=0,srgbWrite=0;
        if(FAILED(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgbWrite))||srgbWrite||
           FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||!blend||
           FAILED(d->GetRenderState(D3DRS_SRCBLEND,&src))||(src!=D3DBLEND_ONE&&src!=D3DBLEND_SRCALPHA)||
           FAILED(d->GetRenderState(D3DRS_DESTBLEND,&dst))||dst!=D3DBLEND_ONE||
           FAILED(d->GetRenderState(D3DRS_BLENDOP,&op))||op!=D3DBLENDOP_ADD||
           FAILED(d->GetRenderState(D3DRS_CULLMODE,&cull))||(cull!=D3DCULL_NONE&&!(curated&&cull==D3DCULL_CW))||
           FAILED(d->GetRenderState(D3DRS_ALPHATESTENABLE,&test)))return false;
        material.sourceAlpha=src==D3DBLEND_SRCALPHA;
        if(test){DWORD function=0,reference=0;if(FAILED(d->GetRenderState(D3DRS_ALPHAFUNC,&function))||FAILED(d->GetRenderState(D3DRS_ALPHAREF,&reference))||
            (function!=D3DCMP_GREATER&&function!=D3DCMP_GREATEREQUAL&&function!=D3DCMP_ALWAYS)||reference>255)return false;
            material.alphaFunction=function;material.alphaReference=reference;}
        std::uint64_t textureHashes[2]{};
        for(unsigned i=0;i<material.program.samples;++i){auto& texture=material.textures[i];const unsigned sampler=i?material.program.secondSampler:material.program.sampler;
            Ref<IDirect3DBaseTexture9> native;NorthlightParticleTextures::View view;DWORD srgb=0,u=0,v=0,mag=0,min=0;
            if(FAILED(d->GetTexture(sampler,&native.p))||!native.p||!NorthlightParticleTextures::cache().lookup(native.p,view)||
               FAILED(d->GetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,&srgb))||FAILED(d->GetSamplerState(sampler,D3DSAMP_ADDRESSU,&u))||
               FAILED(d->GetSamplerState(sampler,D3DSAMP_ADDRESSV,&v))||FAILED(d->GetSamplerState(sampler,D3DSAMP_MAGFILTER,&mag))||
               FAILED(d->GetSamplerState(sampler,D3DSAMP_MINFILTER,&min))||u<1||u>3||v<1||v>3||mag!=min||(mag!=D3DTEXF_POINT&&mag!=D3DTEXF_LINEAR&&!(curated&&mag==D3DTEXF_ANISOTROPIC)))return false;
            textureHashes[i]=view.uploadHash;
            texture.width=view.width;texture.height=view.height;texture.addressU=u;texture.addressV=v;texture.srgb=srgb!=0;texture.linear=mag!=D3DTEXF_POINT;
            texture.rgba.assign(view.rgba,view.rgba+std::size_t(view.width)*view.height*4);
        }
        float constants[1024];if(FAILED(d->GetVertexShaderConstantF(0,constants,256)))return false;
        if(curated&&(constants[120]!=0||constants[121]!=1||constants[122]!=1||constants[8]<=0||constants[13]<=0||constants[22]!=1||constants[23]!=0))return false;
        Ref<IDirect3DVertexDeclaration9> declaration;D3DVERTEXELEMENT9 elements[65];UINT elementCount=65;
        if(FAILED(d->GetVertexDeclaration(&declaration.p))||!declaration.p||FAILED(declaration.p->GetDeclaration(elements,&elementCount))||elementCount>65)return false;
        std::uint64_t declarationHash=14695981039346656037ull;
        for(unsigned i=0;i<elementCount*sizeof elements[0];++i)declarationHash=(declarationHash^reinterpret_cast<const unsigned char*>(elements)[i])*1099511628211ull;
        if(curated&&declarationHash==0x1f5379b1c26276f5ull&&args.type==DrawType::Indexed&&args.baseVertex==0){
            Ref<IDirect3DVertexBuffer9> vb;UINT offset=0,stride=0;
            if(FAILED(d->GetStreamSource(0,&vb.p,&offset,&stride))||!vb.p||offset||stride!=48)return false;
            output.drawGroup=std::uint64_t(reinterpret_cast<std::uintptr_t>(vb.p));
            for(unsigned batchIndex=0;batchIndex<sizeof(AssetBatches)/sizeof(AssetBatches[0]);++batchIndex){const auto& batch=AssetBatches[batchIndex];
                if(batch.firstTexture!=textureHashes[0]||batch.secondTexture!=textureHashes[1]||batch.first!=args.first||
                   batch.minimum!=args.minIndex||batch.vertices!=args.numVertices||batch.primitives!=args.primCount)continue;
                output.assetBatch=batchIndex;if(!geometry)return true;
                std::copy(constants,constants+1024,output.constants.begin());
                std::copy(inverseView,inverseView+16,output.inverse.begin());output.cull=cull;
                return true;
            }
        }
        // Uncatalogued signatures retain native shading; no GPU buffer fallback.
        return false;
    }
};
}
