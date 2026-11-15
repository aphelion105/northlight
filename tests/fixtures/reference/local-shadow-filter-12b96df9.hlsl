// Previous point-sampled PCF/PCSS, retained only as a regression negative control.
float previousLocalVisibilityInRange(float3 delta,float3 normal,float distance,bool soft) {
    // Task 43: dot(normal,delta)>=0 is a face turned from the lamp (facing 0, estimate 0): the correction is 0 whatever
    // the visibility, so the 4-tap cube PCF is skipped.
    if(dot(normal,delta)>=0)return 1;
    float3 direction=delta/max(distance,.00001);
    // World-unit normal bias remains independent of cube near/far precision.
    float grazing=1-abs(dot(normal,direction));
    float3 biased=delta+normal*LocalDepthInfo.w*(1+grazing*2);
    float3 axis=abs(direction.z)<.9?float3(0,0,1):float3(0,1,0);
    float3 tangent=normalize(cross(direction,axis)),bitangent=cross(direction,tangent);
    float plane=dot(normal,biased);
    if(plane>=-.0001)return 1; // The normal offset has passed the light's plane.
    float depthA=LocalShadow.y/(LocalShadow.y-LocalShadow.x);
    float3 depthPlane=normal*(depthA*LocalShadow.x/plane);
    float angular=2*LocalShadow.z*LocalShadow.w;
    float visibility=0;
    [loop]for(int i=0;i<4;++i) {
        float2 offset=float2((i==0||i==2)?-.5:.5,(i<2)?-.5:.5);
        float3 ray=localCubeTexelRay(biased+distance*angular*(tangent*offset.x+bitangent*offset.y));
        // Projected depth is affine in this face ray. No per-tap division or
        // grazing-angle fallback: both previously made a plane shadow itself.
        float deltaDepth=dot(depthPlane,ray);
        float receiver=saturate(depthA-deltaDepth);
        float stored=texCUBElod(LocalCube,float4(ray,0)).r;
        visibility+=(deltaDepth<=0||receiver<=stored+.000002)?1:0;
    }
    float baseline=visibility*.25;
    if(!soft)return baseline;
    // A bounded .08-world-unit emitter. Attenuation range is not emitter size.
    // Reconstruct each face's axial depth before measuring its ray distance.
    float cone=0,blockers=0;
    [loop]for(int y=0;y<4;++y){
        float3 row=bitangent*((y-1.5)/3);
        [loop]for(int x=0;x<4;++x){
        float offset=(x-1.5)/3;
        float3 ray=localCubeTexelRay(biased+distance*angular*4*(tangent*offset+row));
        float deltaDepth=dot(depthPlane,ray);
        float stored=texCUBElod(LocalCube,float4(ray,0)).r;
        if(deltaDepth>0&&stored<min(depthA-deltaDepth-.000002,.999999)){
            float rayLength=length(ray),receiverDistance=depthA*LocalShadow.x*rayLength/deltaDepth;
            float blockerDistance=depthA*LocalShadow.x*rayLength/max(depthA-stored,1e-6);
            cone+=.08*max(receiverDistance-blockerDistance,0)/max(blockerDistance*receiverDistance,.0001);++blockers;
        }
    }
    }
    // A sparse blocker miss retains the base PCF, including thin casters.
    float radius=clamp(2*cone/max(blockers,1),angular,angular*4);
    if(radius<=angular*1.001)return baseline;
    visibility=0;
    [loop]for(int fy=0;fy<4;++fy){
        float3 row=bitangent*((fy-1.5)/3);
        [loop]for(int fx=0;fx<4;++fx){
        float offset=(fx-1.5)/3;
        float3 ray=localCubeTexelRay(biased+distance*radius*(tangent*offset+row));
        float deltaDepth=dot(depthPlane,ray);
        float stored=texCUBElod(LocalCube,float4(ray,0)).r;
        visibility+=(deltaDepth<=0||saturate(depthA-deltaDepth)<=stored+.000002)?1:0;
    }
    }
    return visibility/16;
}
