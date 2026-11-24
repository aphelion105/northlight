// The renderer keeps the near cascade's schedule alongside other replay state.
    void releaseNearEnvironment(){
        directionalMaps.releaseNear();
        for(auto& state:nearShadow)state.invalidate();
    }
    bool nearEnvironmentResources(unsigned source){
        return directionalMaps.prepareNear(d,source,[&]{
            const auto budget=NorthlightGeometryMemory::defaultTargetBudget();
            return NorthlightGeometryMemory::admits(geometryAdmission(budget),budget);
        },[&]{nearShadow[source].invalidate();});
    }
    bool unionShadow(IDirect3DSurface9* target,IDirect3DTexture9* live,IDirect3DTexture9* cached,float x,float y,float dz,UINT size){
        return NorthlightWorldDirectionalTargets::merge(d,shadowPrograms.unionShader(),target,live,cached,x,y,dz,size,
            [&](HRESULT hr,const char* name){return check(hr,name);});
    }
