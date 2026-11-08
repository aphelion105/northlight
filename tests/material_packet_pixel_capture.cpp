#include "packet_batch_fixture.h"
#define NORTHLIGHT_MODEL_NORMALS_TEST
#include "model_normal_renderer.h"

struct PixelBankDevice:PacketDevice {
    unsigned pixelReads=0;bool failPixel=false;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT first,float* out,UINT count)override{
        ++pixelReads;return failPixel?E_FAIL:PacketDevice::GetPixelShaderConstantF(first,out,count);
    }
    PixelBankDevice(){for(unsigned i=0;i<state.pf.size();++i)state.pf[i]=float(i)*.125f;}
};
static void controls(float* values,unsigned count){
    for(unsigned i=0;i<count;++i){const uint32_t bits=i%4==0?0x80000000u:i%4==1?0x7fc12345u:i%4==2?0x3f800001u:0xbf000001u;std::memcpy(values+i,&bits,4);}
}
static void exactPacket(const std::vector<float>& packet,const PixelBankDevice& d,unsigned first,unsigned control,const float* values,unsigned registers){
    for(unsigned i=0;i<packet.size();++i){const unsigned reg=first+i/4;
        const float* expected=reg>=control&&reg<control+registers?values+(reg-control)*4+i%4:d.state.pf.data()+first*4+i;
        assert(!std::memcmp(&packet[i],expected,4));}
}
void MaterialCaptureStateTestAccess::packetPixelConstants(){
    for(unsigned prefix=0;prefix<2;++prefix)for(unsigned suffix=0;suffix<2;++suffix)for(bool fail:{false,true}){
        PixelBankDevice d;d.failPixel=fail;const auto before=d.state.pf;NorthlightAutoBump b(&d);b.profileTag=19;
        NorthlightAutoBump::Vertex vertex;vertex.kind=4;vertex.patched=&d.vs;vertex.floatOnly=true;vertex.constantFirst=0;vertex.constantCount=4;
        NorthlightAutoBump::NativeCapture model;model.shader=&d.ps;model.constant=20;model.first=20-prefix;model.last=28+suffix;model.pixelFloatOnly=true;
        float vertices[12]={0,0,0,0,1,0,0,0,0,1,0,0},values[36];controls(values,36);
        NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,16};
        NorthlightAutoBump::ReplayState saved(&d,&d.vs,d.state.viewport);saved.constantCount=9;NorthlightAutoNormalCache::Lease normal;
        const bool complete=!prefix&&!suffix;const bool recorded=b.recordCapture(args,normal,false,saved,model,vertex,values,D3DTADDRESS_WRAP,D3DTADDRESS_WRAP);
        assert(recorded==(complete||!fail)&&d.pixelReads==unsigned(!complete)&&b.packetPixelReadsSkipped==unsigned(complete));
        if(recorded)exactPacket(b.deferred.back()->pixelConstants,d,model.first,20,values,9);
        assert(d.state.pf==before);b.endFrame();assert(!b.packetPixelReadsSkipped);
    }
}
struct ModelNormalPacketTestAccess {
    static void run(){
        for(unsigned prefix=0;prefix<2;++prefix)for(unsigned suffix=0;suffix<2;++suffix)for(bool fail:{false,true}){
            PixelBankDevice d;d.failPixel=fail;const auto before=d.state.pf;NorthlightModelNormals b(&d);b.setDeferredCapture(true);
            NorthlightModelNormals::Vertex vertex;vertex.shader=&d.vs;vertex.reads.exact=true;vertex.first=0;vertex.count=4;
            NorthlightModelNormals::Pixel pixel;pixel.shader=&d.ps;pixel.reads.exact=true;pixel.control=20;pixel.first=20-prefix;pixel.count=1+prefix+suffix;
            float vertices[12]={0,0,0,0,1,0,0,0,0,1,0,0},values[4];controls(values,4);
            const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,3,0,1,false};auto admit=[](){return true;};
            const bool complete=!prefix&&!suffix;const bool recorded=b.record(&vertex,&pixel,values,d.state.viewport,admit,&geometry,nullptr,D3DFMT_UNKNOWN,vertices,16,nullptr,nullptr,nullptr);
            assert(recorded==(complete||!fail)&&d.pixelReads==unsigned(!complete)&&b.packetPixelReadsSkipped==unsigned(complete));
            if(recorded)exactPacket(b.pending.back()->ps,d,pixel.first,20,values,1);
            assert(d.state.pf==before);b.endFrame();assert(!b.packetPixelReadsSkipped);
        }
    }
};
int main(){MaterialCaptureStateTestAccess::packetPixelConstants();ModelNormalPacketTestAccess::run();
    std::puts("PACKET_PIXEL_CAPTURE_PASS cases=16 complete_reads=0 mixed_reads=1 exact_bits=1 unchanged_native_bank=1 failure_fallback=1 frame_reset=1");}
