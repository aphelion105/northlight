// Hidden-device GPU helpers are prepended by the Python driver.
int main(int argc,char** argv){
    assert(argc==3);GPU gpu(argv[1]);std::string dir=argv[2];
    auto current=gpu.shader(dir+"/current.bin"),negative=gpu.shader(dir+"/negative.bin");
    Constants c{};c[0]={1.f/128,1.f/8,.2f,1000};c[1]={1,1,1,0};
    c[2]={1,48,288,1.f/1024};c[7][0]=1.f/48;c[8][1]=1.f/48;c[9][2]=-1.f/1280;c[10]={3,0,.5f,1};
    c[11][0]=1.f/288;c[12][1]=1.f/288;c[13][2]=-1.f/1280;c[14]={0,0,.5f,1};
    c[16]={0,0,1,0};c[24][0]=.00004f;c[33]={128,8,64,4};c[35][0]=1.f/1280;c[115]={1,0,1,1};
    gpu.bind(1,gpu.tex(rgbaImage(1,1,[](unsigned,unsigned){return std::array<float,4>{1000.f/999.8f*(1-.2f/100),0,0,0};})));
    unsigned negativeFailures=0;float creaseOld=0,creaseNew=0;
    for(unsigned crease=0;crease<2;++crease)for(unsigned blocker=0;blocker<2;++blocker){
        c[116][0]=float(crease);
        auto map=Map([&](float u,float){float x=((u-.5f/1024)*2-1)*288;
            float height=2*(crease?std::fabs(x):x)+(blocker&&x>3?2.f:0.f);
            return .5f-height/1280;});
        gpu.bind(2,gpu.tex(map.image()));gpu.bind(3,gpu.tex(map.image()));
        std::vector<float> confident;
        for(float confidence:{1.f,0.f}){
            auto normals=rgbaImage(64,4,[&](unsigned x,unsigned){float wx=((x+.5f)/64-.5f)*16;
                float slope=crease?2*std::clamp(wx/.5f,-1.f,1.f):2.f;
                float inv=1/std::sqrt(1+slope*slope);return std::array<float,4>{-slope*inv,0,inv,confidence};});
            gpu.bind(14,gpu.tex(normals));
            auto pixels=gpu.capture(current,c,64,4),old=gpu.capture(negative,c,64,4);
            if(confidence==1)confident=pixels;
            else assert(pixels==confident); // Same surface, independent of shading confidence.
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<64;++x){
                unsigned at=(y*64+x)*4;assert(std::isfinite(pixels[at]));
                if(!crease&&x<38)assert(pixels[at]>.999f); // Vertical rays cannot be blocked by a height field.
                if(blocker&&x>50)assert(pixels[at]<.001f); // Independent blocker remains dark.
                if(confidence==0&&old[at]<.95f&&pixels[at]>.999f)++negativeFailures;
                if(crease&&!blocker&&confidence==1){creaseOld+=1-old[at];creaseNew+=1-pixels[at];}
                if(!crease&&confidence==1)assert(pixels[at]==old[at]); // Flat shadow edges retain their bias.
            }
        }
    }
    // A neighbouring object at a different depth must not enlarge the bias
    // of a flat receiver and erase its close, independently authored blocker.
    c[116][0]=0;
    const float inv=1/std::sqrt(5.f);
    gpu.bind(14,gpu.tex(rgbaImage(64,4,[&](unsigned x,unsigned y){return std::array<float,4>{x==32&&y==2?-2*inv:2*inv,0,inv,1};})));
    gpu.bind(1,gpu.tex(rgbaImage(64,4,[](unsigned x,unsigned y){float z=x==32&&y==2?100.f:50.f;return std::array<float,4>{1000.f/999.8f*(1-.2f/z),0,0,0};})));
    auto blockerMap=Map([](float u,float){float x=((u-.5f/1024)*2-1)*288;return .5f-(2*x+1.5f)/1280;});
    gpu.bind(2,gpu.tex(blockerMap.image()));gpu.bind(3,gpu.tex(blockerMap.image()));
    auto separated=gpu.capture(current,c,64,4),separatedOld=gpu.capture(negative,c,64,4);
    const unsigned center=(2*64+32)*4;
    assert(separated[center]<.001f&&separated[center]==separatedOld[center]);
    std::printf("crease old=%g current=%g negativeFailures=%u\n",creaseOld,creaseNew,negativeFailures);std::fflush(stdout);
    assert(negativeFailures>0&&creaseNew<creaseOld*.4f);
    // Even an empty depth map cannot illuminate the rear of an opaque face.
    // Native angular lighting uses a separate direction, so leaked PCF values
    // otherwise survive composition as teeth at silhouettes.
    auto clear=Map([](float,float){return 1.f;});gpu.bind(2,gpu.tex(clear.image()));gpu.bind(3,gpu.tex(clear.image()));
    for(float confidence:{0.f,1.f}){
        gpu.bind(14,gpu.tex(rgbaImage(64,4,[&](unsigned,unsigned){return std::array<float,4>{0,0,-1,confidence};})));
        auto rear=gpu.capture(current,c,64,4),leak=gpu.capture(negative,c,64,4);
        for(unsigned i=0;i<rear.size();i+=4){assert(rear[i]==0);assert(leak[i]>.999f);}
    }
    std::puts("SHADOW_RECEIVER_PASS shadingConfidenceIndependent=1 flatEdgesExact=1 blockersRetained=1");
}
