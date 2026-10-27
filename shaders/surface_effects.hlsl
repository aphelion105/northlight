// Northlight-owned supplemental precipitation. Native water rendering is untouched.
sampler2D FinalDepth : register(s3);
float4 Clip : register(c0);       // inverse image size, near, far
float4 Proj : register(c1);       // scale x/y, forward sign, animation time
float4 DepthRange : register(c2); // viewport min, reciprocal extent
float4 Weather : register(c3);    // amount, reserved
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
float4 precipitation(float2 uv,int layers,bool snow) {
    float scene=linearDepth(tex2D(FinalDepth,uv).r);float opacity=0;
    [unroll] for(int layer=0;layer<3;++layer) {
        if(layer>=layers)break;
        float distance=layer==0?3.5:layer==1?11:26;
        float weight=layer==0?.45:layer==1?.35:.20;
        float3 p=toWorld(position(uv,distance));
        float time=Proj.w,scale=snow?.8:1.2;
        p+=float3(time*.5,time*.2,time*(snow?1.3:19));p*=scale;
        float3 cell=floor(p),seed=random3(cell);float3 delta=frac(p)-seed;
        if(snow)delta.xy+=sin(time*.7+cell.z+seed.xy*6.28)*.08;
        float radius=snow?.055:.014;
        float shape=snow?saturate(1-length(delta)/radius):saturate(1-length(delta.xy)/radius)*saturate(1-abs(delta.z)/.32);
        shape*=smoothstep(distance-.5,distance+.5,scene)*step(seed.z,Weather.x);
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

// Screen-bound optics are independently generated; no weather textures are required.
sampler2D LensScene : register(s0);
float4 Lens : register(c10); // elapsed seconds, visibility, aspect, unused
float lensRandom(float2 p){return frac(sin(dot(p,float2(41.7,289.3)))*19417.318);}
float4 RainLens(float2 uv:TEXCOORD0):COLOR0 {
    float2 scale=float2(13*Lens.z,13),grid=uv*scale,cell=floor(grid),local=frac(grid)-.5;
    float seed=lensRandom(cell),phase=frac(Lens.x*.22+seed);
    float life=smoothstep(0,.10,phase)*(1-smoothstep(.90,1,phase));
    float2 center=float2((lensRandom(cell+3.7)-.5)*.55,.8*phase*phase-.4);
    float2 d=local-center;float radius=.06+.03*seed;float distance=length(d);
    float body=(1-smoothstep(radius*.82,radius,distance))*life;
    float trail=(1-smoothstep(.005,.012,abs(d.x)))*(1-smoothstep(.02,.16,-d.y))*step(d.y,0)*life;
    // Bend rays toward the droplet center; the lens strength fades with weather.
    float2 normal=d/(distance+.0001);float4 base=tex2D(LensScene,uv),bent=tex2D(LensScene,uv-normal*(.022*body*Lens.y));
    float rim=(smoothstep(radius*.45,radius*.76,distance)-smoothstep(radius*.76,radius,distance))*life;
    float highlight=pow(saturate(normal.x*.35-normal.y*.65),5)*body*.36;
    base.rgb=lerp(base.rgb,bent.rgb,body);base.rgb+=(rim*.025+trail*.018+highlight)*Lens.y;return base;
}
float4 SnowLens(float2 uv:TEXCOORD0):COLOR0 {
    float2 grid=uv*float2(6*Lens.z,6),cell=floor(grid),local=frac(grid)-.5;
    float seed=lensRandom(cell+17.0),phase=frac(Lens.x*.035+seed);
    float life=smoothstep(0,.12,phase)*(1-smoothstep(.62,1,phase))*step(.68,lensRandom(cell+43.0));
    float2 center=(float2(lensRandom(cell+1.9),lensRandom(cell+7.3))-.5)*.48;
    float2 d=local-center;float size=.08+.07*seed;float r=length(d*float2(1,.82));
    float soft=1-smoothstep(size*.25,size*1.4,r);
    float chip1=1-smoothstep(size*.25,size*.75,length(d-float2(size*.48,size*.22)));
    float chip2=1-smoothstep(size*.18,size*.65,length(d+float2(size*.28,size*.44)));
    float alpha=saturate((soft*.30+chip1*.24+chip2*.20)*life*Lens.y);
    float4 color=tex2D(LensScene,uv);color.rgb=lerp(color.rgb,float3(.68,.72,.76),alpha);return color;
}
