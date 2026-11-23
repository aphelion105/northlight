    NorthlightWorldShadowTransition shadowTransition;
    int shadowTransitionSource=-1;
    unsigned shadowTransitionStarts=0;
    void resetShadowTransitions(){shadowTransition.reset();shadowTransitionSource=-1;}
    void releaseShadowTransitions(){shadowTransition.release();shadowTransitionSource=-1;}
    bool shadowTransitionResources(int slot){
        return shadowTransition.prepare(d,slot,memoryPressure||geometryMemoryDemand.load(std::memory_order_relaxed),[&]{
            const auto budget=NorthlightGeometryMemory::dynamicBudget(8ull<<20);
            return NorthlightGeometryMemory::admits(geometryAdmission(budget),budget);
        },[&]{logf("WORLD shadow transition unavailable: retaining instantaneous cache updates");});
    }
    bool preserveShadowStatic(int slot){
        const auto& f=shadowTransition[slot];const auto& key=shadowCacheKey[slot];
        long x=0,y=0;float dz=0;
        if(!f.workingValid||!key.valid||!NorthlightWorldMath::cacheOffset(f.working,key.frame,x,y,dz)||
           std::labs(x)>long(ShadowCacheMargin)||std::labs(y)>long(ShadowCacheMargin)||!shadowTransitionResources(slot))return false;
        return shadowTransition.preserveStatic(d,slot,{screenFallbacks.white(),directionalMaps.cache(slot).texture,shadowPrograms.unionShader(),
                                                ShadowCacheMargin,ShadowCacheSize,x,y,dz});
    }
    bool shadowTransitionActive(int source)const{return effects.shadows&&shadowTransition.active(source);}
    bool shadowVolumeTransitionActive(int source)const{
        return shadowTransitionActive(source)&&shadowTransition[source*2+1].state.fading();
    }
    void bindShadowTransition(int source,bool previousEpoch=false){
        if(!shadowTransitionActive(source)&&!shadowTransitionActive(1-source))return;
        const auto& direction=sourceDirections[source];
        shadowTransition.bind(d,source,previousEpoch,sourceMatrices[source],{direction.x,direction.y,direction.z},screenFallbacks.white(),quality.softShadows);
    }
