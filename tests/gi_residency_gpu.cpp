// GPU helpers are prepended by test_gi_residency_gpu.py.
#include "probe_texture_upload.h"

int main(int argc,char** argv){
    assert(argc==3);GPU gpu(argv[1]);const std::string dir=argv[2];
    auto current=gpu.shader(dir+"/current.bin"),negative=gpu.shader(dir+"/negative.bin");
    Constants c{};c[18]={.2f,.3f,.4f,0};c[19][3]=8;c[20]={2,1,1,1};c[24][3]=5;c[66]={1,0,0,0};
    c[82]={3,1.f/27,1.f/27,1.f/3};c[117]={.25f,4,4,7.5f};
    auto solid=[&](float x,float y=0,float z=0,float w=0){return gpu.tex(rgbaImage(1,1,[&](unsigned,unsigned){return std::array<float,4>{x,y,z,w};}));};
    gpu.bind(13,solid(0,0,0,1));
    for(unsigned slot:{4u,5u,6u})gpu.bind(slot,solid(.1f/.28209479f));
    unsigned cases=0,negativeCoverage=0,negativeSeams=0;float maxError=0;
    // 0 missing, 1 all valid, 2 half inside solid, 3 all inside solid,
    // 4 half absent, 5 wrapped collision, 6 first residency half-faded.
    for(unsigned mode=0;mode<7;++mode){
        std::vector<NorthlightGI::ProbeAtlasEntry> atlas(8);
        for(unsigned i=0;i<8;++i){auto& e=atlas[i];e.key={int(i%2),int(i/2%2),int(i/4)};
            e.occupied=mode!=0&&(mode!=4||i%2);e.probe.valid=mode!=3&&(mode!=2||i%2);
            if(mode==5)e.key.x+=2;
        }
        NorthlightProbeActivation activation;activation.begin("fixture");
        NorthlightProbeBlend::Mirror blend;blend.begin("fixture");
        for(unsigned channel:{3u,4u}){
            auto im=rgbaImage(4,channel==3?12:2,[](unsigned,unsigned){return std::array<float,4>{};});
            NorthlightProbeUpload::write(channel,im.pixels.data(),64,2,atlas,blend,activation,mode==6?4.775f:0.f,false);
            gpu.bind(channel==3?7:10,gpu.tex(im));
        }
        for(float nz:{0.f,.5f,1.f})for(float confidence:{0.f,1.f})for(float brightness:{-1.f,0.f}){
            c[116]={0,std::sqrt(1-nz*nz),nz,confidence};c[66][1]=brightness;
            auto pixels=gpu.capture(current,c,32,2),old=gpu.capture(negative,c,32,2);
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<32;++x){
                float coverage=mode==0||mode==3||mode==5?0.f:mode==4?(.25f+(x+.5f)/32*7.5f)/8:mode==6?.5f:1.f;
                const float sky=.5f+.5f*nz,radiance=mode==3?0.f:.1f*(1+brightness);
                for(unsigned ch=0;ch<3;++ch){
                    const float expected=(1-coverage)*c[18][ch]*sky+coverage*radiance-c[18][ch];
                    const unsigned at=(y*32+x)*4+ch;const float error=std::fabs(pixels[at]-expected);
                    maxError=std::max(maxError,error);
                    if(error>4e-5f)std::fprintf(stderr,"mode=%u nz=%g confidence=%g brightness=%g x=%u actual=%g expected=%g\n",mode,nz,confidence,brightness,x,pixels[at],expected);
                    assert(std::isfinite(pixels[at])&&error<4e-5f);
                    negativeCoverage+=(mode==2||mode==3)&&std::fabs(old[at]-expected)>.01f;
                    negativeSeams+=mode==0&&confidence==0&&std::fabs(old[at]-expected)>.01f;
                }
            }++cases;
        }
    }
    assert(negativeCoverage&&negativeSeams);
    // Two packed layers use independent keys, activation and visibility. A
    // dark resident near cell must never acquire radiance from the coarse grid.
    c[83][3]=1;c[116]={0,0,1,1};c[66][1]=0;
    unsigned cascadeChecks=0;
    for(unsigned mode=0;mode<8;++mode){
        c[117][0]=mode==7?64.25f:.25f;
        std::vector<NorthlightGI::ProbeAtlasEntry> layers[2];
        for(unsigned level=0;level<2;++level){layers[level].resize(8);
            for(unsigned i=0;i<8;++i){auto& e=layers[level][i];e.key={int(i%2),int(i/2%2),int(i/4)};
                e.occupied=level||mode!=0;e.probe.valid=!(level==0&&mode==2)&&!(level==1&&mode==3);
                if(!level&&mode==1&&i%2==0)e.occupied=false;
                if(level&&(mode==4||mode==7))e.key.x+=2;
                auto local=std::make_shared<NorthlightGI::ProbeLocalVisibility>();
                for(auto& t:local->texels)t={0,0,uint16_t(level&&mode==6?(i/4?0xbc00:0x3c00):0),0x3c00};e.probe.localVisibility=local;
                for(auto& sh:e.probe.sh)sh={};e.probe.sh[0]=NorthlightGI::Vec3(level?.3f:.1f,level?.3f:.1f,level?.3f:.1f)/.28209479f;
            }
        }
        if(mode>=3)for(auto& e:layers[0])e.occupied=false;
        NorthlightProbeActivation activations[2];NorthlightProbeBlend::Mirror blends[2];
        for(auto& a:activations)a.begin("cascade");for(auto& b:blends)b.begin("cascade");
        for(unsigned channel=0;channel<5;++channel){
            unsigned rows=channel==3?12:2;
            auto im=rgbaImage(4,rows*2,[](unsigned,unsigned){return std::array<float,4>{};});
            for(unsigned level=0;level<2;++level)NorthlightProbeUpload::write(channel,im.pixels.data()+size_t(level)*rows*64,64,2,layers[level],blends[level],activations[level],mode==5?4.775f:0.f,false);
            gpu.bind(channel<3?channel+4:channel==3?7:10,gpu.tex(im));
        }
        auto combined=layers[0];combined.insert(combined.end(),layers[1].begin(),layers[1].end());
        auto layout=NorthlightGI::LocalProbeVisibility::atlas(2,2);
        Image localImage{layout.width,layout.height,D3DFMT_A16B16G16R16F,8,std::vector<unsigned char>(layout.width*layout.height*8)};
        NorthlightProbeUpload::LocalVisibilityMirror mirror;assert(mirror.prepare(combined.size()));
        mirror.write(localImage.pixels.data(),layout.width*8,2,combined,2);gpu.bind(13,gpu.tex(localImage));
        c[82]={float(layout.columns),1.f/layout.width,1.f/layout.height,1.f/layout.columns};
        auto pixels=gpu.capture(current,c,32,2);
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<32;++x){
            float fineCoverage=mode==1?(.25f+(x+.5f)/32*7.5f)/8:0.f;
            float coarseCoverage=mode==3||mode==4?0.f:mode==5?.5f:1.f;
            float value=fineCoverage*(mode==2?0:.1f)+(1-fineCoverage)*coarseCoverage*(mode==3||mode==6?0:.3f);
            for(unsigned ch=0;ch<3;++ch){float expected=value+(1-fineCoverage)*(1-coarseCoverage)*c[18][ch]-c[18][ch];
                if(std::fabs(pixels[(y*32+x)*4+ch]-expected)>=4e-5f)std::fprintf(stderr,"CASCADE mode=%u x=%u ch=%u actual=%g expected=%g\n",mode,x,ch,pixels[(y*32+x)*4+ch],expected);
                assert(std::fabs(pixels[(y*32+x)*4+ch]-expected)<4e-5f);++cascadeChecks;}
        }
    }
    std::printf("GI_CASCADE_PASS checks=%u nearBlockersPreserved=1 missingFarFallback=1 independentActivation=1\n",cascadeChecks);
    std::printf("GI_RESIDENCY_PASS cases=%u maxError=%g oldCoverageFailures=%u oldSeamFailures=%u uploader=production\n",cases,maxError,negativeCoverage,negativeSeams);
}
