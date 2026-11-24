#include "world_fog_field.h"
#include "forwarders.h"
#include "mirror_resource_forwarders.h"
#include <cassert>
#include <cstdio>
#include <utility>
#include <vector>

namespace {
constexpr unsigned N=NorthlightRegionalFog::N;
constexpr unsigned Pitch=(N+1)*16;
enum class Op {Create, Lock, Unlock, Release, Bind};
using Event=std::pair<Op,unsigned>;

struct Device: ForwardIDirect3DDevice9 {
    struct Texture final: NorthlightMirrorResources::ForwardIDirect3DTexture9 {
        Device& device;
        unsigned serial;
        std::array<unsigned char,N*Pitch> bytes{};
        bool locked=false;

        Texture(Device& d,unsigned id):ForwardIDirect3DTexture9(nullptr,d.testGate,nullptr,nullptr),device(d),serial(id){}
        HRESULT STDMETHODCALLTYPE LockRect(UINT level,D3DLOCKED_RECT* rect,const RECT* region,DWORD flags) override {
            assert(!level&&!region&&!flags&&!locked);
            const HRESULT result=device.record(Op::Lock,serial);
            if(FAILED(result))return result;
            locked=true;
            *rect={device.badPitch==serial?INT(N*16-1):INT(Pitch),bytes.data()};
            return result;
        }
        HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override {
            assert(!level&&locked);
            locked=false;
            return device.record(Op::Unlock,serial);
        }
        ULONG STDMETHODCALLTYPE Release() override {
            assert(!locked&&device.live);
            device.record(Op::Release,serial);
            --device.live;
            delete this;
            return 0;
        }
    };

    MirrorGate testGate;
    std::vector<Event> events;
    std::vector<IDirect3DBaseTexture9*> binds;
    Event fault={Op::Create,0};
    HRESULT error=E_OUTOFMEMORY;
    unsigned created=0,live=0,badPitch=0;

    Device():ForwardIDirect3DDevice9(nullptr){}
    HRESULT record(Op op,unsigned value){
        events.emplace_back(op,value);
        return fault==events.back()?error:D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width,UINT height,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,
                                             IDirect3DTexture9** texture,HANDLE* shared) override {
        assert(width==N&&height==N&&levels==1&&!usage&&format==D3DFMT_A32B32G32R32F&&pool==D3DPOOL_MANAGED&&!shared);
        const HRESULT result=record(Op::Create,++created);
        *texture=nullptr;
        if(SUCCEEDED(result)){*texture=new Texture(*this,created);++live;}
        return result;
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD slot,IDirect3DBaseTexture9* texture) override {
        binds.push_back(texture);
        return record(Op::Bind,slot);
    }
};

struct Fixture {
    Device device;
    NorthlightWorldFogField field;
    using Field=NorthlightWorldFogField::Field;
    std::shared_ptr<Field> input=std::make_shared<Field>();
    unsigned invalidations=0,warnings=0,errors=0,clockReads=0;

    Fixture(){
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x)input->texels[y*N+x]={float(x+y),.01f,.005f,2.f};
    }
    bool upload(DWORD now,const std::string& map="Northrend"){
        return field.upload(&device,input,map,[&]{++clockReads;return now;},
            [&](HRESULT hr,const char*){assert(FAILED(hr));++errors;return false;},
            [&](HRESULT hr,const char*){assert(hr==E_OUTOFMEMORY||hr==D3DERR_OUTOFVIDEOMEMORY);++warnings;},
            [&]{++invalidations;});
    }
    void bind(bool height){
        device.events.clear();
        device.binds.clear();
        field.bindVolumeInputs(&device);
        assert((device.events==std::vector<Event>{{Op::Bind,13},{Op::Bind,12}}));
        assert(device.binds[0]==field.texture());
        assert(device.binds[1]==(height?field.heightAllocation():nullptr));
    }
};
}

int main(){
    {
        Fixture f;
        assert(f.upload(0));
        assert((f.device.events==std::vector<Event>{{Op::Create,1},{Op::Lock,1},{Op::Unlock,1},{Op::Create,2},{Op::Lock,2},{Op::Unlock,2}}));
        assert(f.field.field()==f.input&&f.field.globalHeights()==NorthlightRegionalFog::globalHeightBounds(*f.input));
        const auto* density=static_cast<Device::Texture*>(f.field.texture());
        const auto* height=static_cast<Device::Texture*>(f.field.heightTexture());
        for(unsigned y=0;y<N;++y){
            assert(!std::memcmp(density->bytes.data()+y*Pitch,f.input->texels.data()+y*N,N*16));
            for(unsigned x=0;x<N;++x){
                const auto expected=NorthlightRegionalFog::heightBounds(*f.input,x,y);
                assert(!std::memcmp(height->bytes.data()+y*Pitch+x*16,expected.data(),16));
            }
        }
        f.bind(true);
        f.device.events.clear();
        assert(f.upload(1)&&f.invalidations==1&&f.clockReads==1&&f.device.events.empty());
        assert(f.upload(2,"Other")&&f.invalidations==2&&f.device.created==2&&f.field.map()=="Other");
        f.input.reset();
        assert(f.upload(3,"Other")&&!f.field.field()&&!f.field.heightTexture()&&f.device.live==2);
        f.bind(false);
        f.field.reset();
        f.field.reset();
        assert(!f.field.texture()&&!f.field.heightAllocation()&&!f.device.live);
        f.input=std::make_shared<Fixture::Field>();
        assert(f.upload(4)&&f.field.field()==f.input&&f.device.created==4);
    }
    for(auto failure:{Op::Create,Op::Lock,Op::Unlock}){
        Fixture f;
        f.device.fault={failure,1};
        assert(!f.upload(100)&&f.warnings==1&&!f.errors&&!f.device.live&&!f.field.field());
        f.device.events.clear();
        assert(!f.upload(1099)&&f.device.events.empty());
        f.device.fault={failure,2};
        assert(!f.upload(1100)&&f.warnings==1&&!f.device.live);
        f.device.fault={failure,3};
        assert(!f.upload(60100)&&f.warnings==2&&!f.device.live);
        f.field.reset();
        assert(f.upload(60101)&&f.field.heightTexture());
    }
    for(auto failure:{Op::Create,Op::Lock,Op::Unlock}){
        Fixture f;
        f.device.fault={failure,2};
        assert(f.upload(100)&&f.field.field()==f.input&&f.field.texture()&&!f.field.heightTexture()&&!f.warnings&&!f.errors);
        f.bind(false);
    }
    {
        Fixture f;
        f.device.badPitch=1;
        assert(!f.upload(0)&&f.errors==1&&!f.field.field()&&f.device.live==1);
        assert(f.device.events.back()==Event(Op::Unlock,1));
        f.device.badPitch=2;
        assert(f.upload(1)&&f.field.field()==f.input&&!f.field.heightTexture()&&f.device.live==2);
        f.bind(false);
    }
    std::puts("WORLD_FOG_FIELD_PASS formats_and_upload_order=1 pitched_texels_exact=1 s13_s12_order=1 required_retry=3 optional_fallback=3 layout_failures=2 identity_reuse=1 reset_rebuild=1");
}
