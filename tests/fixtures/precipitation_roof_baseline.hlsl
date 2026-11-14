// Frozen production precipitation at 64684a78, before sparse roof sampling.
// Northlight-owned supplemental precipitation. Native water rendering is untouched.
sampler2D FinalDepth : register(s3);
sampler2D FrostMaterial : register(s1);
sampler2D SnowMaterial : register(s2);
sampler2D WeatherNoise : register(s4);
sampler2D WeatherRoof : register(s5);
float4 RoofMap : register(c12); // world XY origin, inverse span, available
float4 Clip : register(c0);       // inverse image size, near, far
float4 Proj : register(c1);       // scale x/y, forward sign, animation time
float4 DepthRange : register(c2); // viewport min, reciprocal extent
float4 Weather : register(c3);    // amount, snow flag, particle radius scale minus one, reserved
float4 Inv0 : register(c4);
float4 Inv1 : register(c5);
float4 Inv2 : register(c6);
float4 Inv3 : register(c7);
float4 Ambient : register(c8);
float4 Direct : register(c9);

float linearDepth(float d) {
    d=saturate((d-DepthRange.x)*DepthRange.y);
    return Clip.z*Clip.w/max(Clip.w-d*(Clip.w-Clip.z),.00001);
}
float3 position(float2 uv,float z) {
    float2 xy=((uv-.5*Clip.xy)*float2(2,-2)+float2(-1,1))/Proj.xy;
    return float3(xy,Proj.z)*z;
}
float3 toWorld(float3 v) {return v.x*Inv0.xyz+v.y*Inv1.xyz+v.z*Inv2.xyz+Inv3.xyz;}
float3 random3(float3 cell) {return frac(sin(float3(dot(cell,float3(127.1,311.7,74.7)),dot(cell,float3(269.5,183.3,246.1)),dot(cell,float3(113.5,271.9,124.6))))*43758.5453);}
float2 snowCoordinates(float2 p,float seed) {
    float angle=seed*6.2831853;
    p=float2(dot(p,float2(cos(angle),-sin(angle))),dot(p,float2(sin(angle),cos(angle))));
    p*=float2(lerp(.75,1.65,frac(seed*7.3)),lerp(.7,1.25,frac(seed*3.7)));
    return p*.25+.5;
}
float snowAtlas(float2 uv,float index) {
    index=frac(index/16)*16;float row=floor(index*.25);
    float2 tile=float2(index-row*4,row);
    float2 edge=min(uv,1-uv);
    return tex2D(SnowMaterial,(tile+clamp(uv,.01,.99))*.25).r*smoothstep(0,.035,min(edge.x,edge.y));
}
float snowFloc(float2 p,float seed) {
    float ice=snowAtlas(snowCoordinates(p,seed),floor(seed*15.999));
    return ice*ice*1.4;
}
float snowMorph(float2 p,float seed,float phase) {
    float2 uv=snowCoordinates(p,seed);float index=floor(phase),blend=smoothstep(0,1,frac(phase));
    float ice=lerp(snowAtlas(uv,index),snowAtlas(uv,index+1),blend);
    return ice*ice*1.4;
}
float4 precipitation(float2 uv,int layers,bool snow) {
    float scene=linearDepth(tex2D(FinalDepth,uv).r);float opacity=0;
    [unroll] for(int layer=0;layer<3;++layer) {
        if(layer>=layers)break;
        float distance=layer==0?3.5:layer==1?11:26;
        float weight=layer==0?.45:layer==1?.35:.20;
        float3 p=toWorld(position(uv,distance));
        float exposed=1;
        if(RoofMap.w>.5){
            float2 roofUV=(p.xy-RoofMap.xy)*RoofMap.z;
            float roof=tex2Dlod(WeatherRoof,float4(roofUV,0,0)).r;
            exposed=step(roof+.03,p.z)*step(0,min(roofUV.x,roofUV.y))*step(max(roofUV.x,roofUV.y),.999999);
        }
        float time=Proj.w,scale=snow?.8:1.2;
        // Sampling moves against particle travel: +X/+Y wind, falling along -Z.
        p+=float3(-time*.5,-time*.2,time*(snow?1.3:19));p*=scale;
        float3 cell=floor(p),seed=random3(cell);float3 delta=frac(p)-seed;
        if(snow)delta.xy+=sin(time*.7+cell.z+seed.xy*6.28)*.08;
        float radius=(snow?.036+.026*seed.x:.014)*clamp(1+Weather.z,.25,3);
        float shape=snow?snowFloc(delta.xy/radius,seed.y)*saturate(1-abs(delta.z)/(radius*.9)):saturate(1-length(delta.xy)/radius)*saturate(1-abs(delta.z)/.32);
        shape*=smoothstep(distance-.5,distance+.5,scene)*step(seed.z,Weather.x)*exposed;
        opacity+=shape*weight;
    }
    float alpha=saturate(opacity*.45*Weather.x);
    float3 color=saturate(float3(.45,.53,.62)+Ambient.rgb*.45+Direct.rgb*.15);
    if(snow)color=lerp(color,1,.55);
    return float4(color*alpha,alpha);
}
float4 RainQuality(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,3,false);}
float4 SnowQuality(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,3,true);}
float4 RainPerformance(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,2,false);}
float4 SnowPerformance(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,2,true);}

