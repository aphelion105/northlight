// Frozen Northlight march before noise specialization and empty-prefix elision.
float4 foreverFogReference(float2 uv,bool actorIntegration) {
    if(FogInfo.x<.5)return float4(0,0,0,1);
    uv=depthUV(uv);float depth=normalizedDepth(uv);
    float3 view=viewPositionDistance(uv,fogReceiverDistance(uv,depth));
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float distance=min(length(ray),Forever[1].w);ray*=rsqrt(max(dot(ray,ray),1e-12));
    float3 g=Forever[6].xyz;
    // HG integrates to one over solid angle; imported intensity is not a
    // sphere-average radiance. Omitting 1/(4*pi) amplifies it by 12.57x.
    float3 phase=(1.0/(4*3.14159265))*(1-g*g)*pow(rsqrt(max(1+g*g-2*g*dot(ray,SunDirection.xyz),1e-6)),3);
    float3 direct=phase*Forever[11].xyz*Forever[11].w;
    float T=1;float3 scatter=0;
    // Quadratic spacing resolves near shafts while retaining the distant haze.
    [loop]for(int j=0;j<48;++j){
        float a=distance*(j*j)*(1.0/2304),b=distance*((j+1)*(j+1))*(1.0/2304),t=(a+b)*.5;
        float visible=fogShadowProjected(affinePoint(Camera.xyz+ray*t,FarMatrix));
        if(actorIntegration&&FogActorInfo.x>0&&visible>0)visible=fogCombineActor(visible,fogActorShadow(affinePoint(Camera.xyz+ray*a,FogActorMatrix),(ray.x*FogActorMatrix[0].xyz+ray.y*FogActorMatrix[1].xyz+ray.z*FogActorMatrix[2].xyz)*(b-a)));
        float3 p=Camera.xyz+ray*t;float coverage;
        float4 field=regionalFogAt((p.xy-RegionalFogInfo.xy)*RegionalFogInfo.z+.5/64,coverage);
        // Outside the local terrain cache absolute-height atmosphere continues;
        // covered indoor/underground samples are excluded by the authored field.
        float inside=all(p.xy>=RegionalFogInfo.xy)&&all(p.xy<=RegionalFogInfo.xy+63.0/(64*max(RegionalFogInfo.z,1e-8)));
        float valid=inside?(p.z>=field.x?coverage:0):1;
        float ground=coverage>0?field.x:Forever[0].w;
        float3 height=p.z-ground*Forever[10].xyz;
        float3 shape=exp(min(0,(Forever[2].xyz-height)*Forever[3].xyz)+min(0,(height-Forever[4].xyz)*Forever[5].xyz));
        float3 curve=1+Forever[7].xyz*pow(max(saturate((t-Forever[1].xyz)*.001),1e-8),Forever[8].xyz);
        float3 density=Forever[0].xyz*shape*curve*step(Forever[1].xyz,t)*valid;
        float3 noise=foreverNoise(p);density*=noise;
        float sigma=dot(density,1),absorb=1-exp(-sigma*(b-a));
        float3 lit=lerp(1,visible,Forever[9].xyz)*direct;
        float3 ambient=SourcePolicy.x*lerp(1,visible,Forever[9].xyz);
        float3 shade=SourcePolicy.x*(1-lerp(1,visible,Forever[9].xyz));
        float3 source0=Forever[12].rgb*lit.x+lerp(Forever[35].rgb,Forever[15].rgb,noise.x)*ambient.x+Forever[18].rgb*shade.x;
        float3 source1=Forever[13].rgb*lit.y+lerp(Forever[36].rgb,Forever[16].rgb,noise.y)*ambient.y+Forever[19].rgb*shade.y;
        float3 source2=Forever[14].rgb*lit.z+lerp(Forever[37].rgb,Forever[17].rgb,noise.z)*ambient.z+Forever[20].rgb*shade.z;
        scatter+=T*absorb*(source0*density.x+source1*density.y+source2*density.z)/max(sigma,1e-8);
        T*=1-absorb;
    }
    return float4(scatter,T);
}
float4 ForeverFogReference(float2 uv:TEXCOORD0):COLOR0{return foreverFogReference(uv,false);}
float4 ForeverFogActorReference(float2 uv:TEXCOORD0):COLOR0{return foreverFogReference(uv,true);}
