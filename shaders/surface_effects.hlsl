// Northlight-owned water and precipitation. No donor shaders or textures.
sampler2D SourceColor : register(s0);
sampler2D SourceDepth : register(s1);
sampler2D LiquidMask : register(s2);
sampler2D FinalDepth : register(s3);
sampler2D Reflections : register(s4);
sampler2D DepthBounds : register(s5);
sampler2D AirFog : register(s6);
sampler2D AirDistance : register(s7);
float4 Clip : register(c0);       // inverse image size, near, far
float4 Proj : register(c1);       // scale x/y, forward sign, animation time
float4 DepthRange : register(c2); // viewport min, reciprocal extent
float4 Weather : register(c3);    // amount, snow, layer count, reserved
float4 Inv0 : register(c4);
float4 Inv1 : register(c5);
float4 Inv2 : register(c6);
float4 Inv3 : register(c7);
float4 Ambient : register(c8);
float4 Direct : register(c9);
float4 Light : register(c10);     // world-space direction toward sun/moon
float4 PointPosition[4] : register(c11);
float4 PointColor[4] : register(c15);
float4 PointCount : register(c19);
float4 NativeFog : register(c20); // signed native view-Z ramp, enabled flag
float4 NativeFogColor : register(c21);
float4 WaterOptions : register(c22); // refraction, celestial glints, SSR strength, SSR ready
float4 SSR : register(c23); // screen steps, range (0 = camera clip), hit thickness, refinement steps
float4 Hierarchy : register(c24); // trace: inverse base size, last mip, enabled; build: inverse input size, input mip, first pass
float4 FogControl : register(c25); // inverse fog image size, enabled, reserved

float waterTransmittance(float z) {
    return lerp(1,saturate(pow(max(z*Proj.z*NativeFog.x+NativeFog.y,0),NativeFog.z)),NativeFog.w);
}

float4 SurfaceDepthCopy(float2 uv : TEXCOORD0) : COLOR0 { return tex2D(SourceColor,uv).rrrr; }
float linearDepth(float d) {
    d=saturate((d-DepthRange.x)*DepthRange.y);
    return Clip.z*Clip.w/max(Clip.w-d*(Clip.w-Clip.z),.00001);
}
float3 position(float2 uv,float z) {
    float2 xy=((uv-.5*Clip.xy)*float2(2,-2)+float2(-1,1))/Proj.xy;
    return float3(xy,Proj.z)*z;
}
float3 toWorld(float3 v) {return v.x*Inv0.xyz+v.y*Inv1.xyz+v.z*Inv2.xyz+Inv3.xyz;}
float3 toViewDirection(float3 v) {return float3(dot(v,Inv0.xyz),dot(v,Inv1.xyz),dot(v,Inv2.xyz));}
float2 screen(float3 p) {return p.xy/max(p.z*Proj.z,.001)*Proj.xy*float2(.5,-.5)+.5+.5*Clip.xy;}
float opaqueDistance(float2 uv) {return linearDepth(tex2Dlod(SourceDepth,float4(uv,0,0)).r);}
float edgeFade(float2 uv) {return saturate(min(min(uv.x,uv.y),min(1-uv.x,1-uv.y))*16);}

float4 SSRDepthBounds(float2 uv:TEXCOORD0):COLOR0 {
    float2 offset=Hierarchy.xy*.5;
    // Linear depth is monotone: reduce raw depth before the two reciprocals.
    [branch]if(Hierarchy.w>.5){
        float2 raw=float2(1,0);
        [unroll]for(int y=0;y<4;++y)[unroll]for(int x=0;x<4;++x){
            float2 at=uv+offset*float2(x*2-3,y*2-3);
            float d=tex2Dlod(FinalDepth,float4(at,0,0)).r;
            raw=float2(min(raw.x,d),max(raw.y,d));
        }
        return float4(min(Clip.w,linearDepth(raw.x)),max(0,linearDepth(raw.y)),0,1);
    }
    float2 bounds=float2(Clip.w,0);
    [unroll]for(int y=0;y<4;++y)[unroll]for(int x=0;x<4;++x){
        float2 at=uv+offset*float2(x*2-3,y*2-3);
        float2 sample=tex2Dlod(DepthBounds,float4(at,0,Hierarchy.z)).rg;
        bounds=float2(min(bounds.x,sample.x),max(bounds.y,sample.y));
    }
    return float4(bounds,0,1);
}
float2 intervalBounds(float2 a,float2 b) {
    float2 span=abs(b-a)/Hierarchy.xy;
    float level=clamp(ceil(log2(max(max(span.x,span.y),1))),0,Hierarchy.z);
    // Padded reduction covers the interval's enclosing cells from one central lookup.
    return tex2Dlod(DepthBounds,float4((a+b)*.5,0,level)).rg;
}
float4 reflectionHit(float2 a,float2 delta,float k,float dk,float low,float high) {
    float za=1/(k+dk*low),zb=1/(k+dk*high),at=(low+high)*.5;
    [loop]for(int refine=0;refine<=SSR.w;++refine){
        float2 uv=(floor((a+delta*at)/Clip.xy)+.5)*Clip.xy;
        float scene=linearDepth(tex2Dlod(FinalDepth,float4(uv,0,0)).r);
        float thickness=max(SSR.z,scene*.0002);
        if(scene>=Clip.w*.999||scene<min(za,zb)-thickness||scene>max(za,zb)+thickness)break;
        float hit=abs(dk)>1e-9?saturate((1/scene-k)/dk):at;
        uv=(floor((a+delta*hit)/Clip.xy)+.5)*Clip.xy;
        float receiver=linearDepth(tex2Dlod(FinalDepth,float4(uv,0,0)).r);
        if(abs(receiver-scene)<=thickness){
            float2 wet=tex2Dlod(LiquidMask,float4(uv,0,0)).rg;
            if(!(wet.y>.003&&wet.x>Clip.z&&wet.x<=receiver+max(.012,wet.x*.0007))){
                float confidence=edgeFade(uv);
                // Radiance and tested depth must refer to the same full-resolution texel.
                return float4(tex2Dlod(SourceColor,float4(uv,0,0)).rgb*confidence,confidence);
            }
            break;
        }
        if((1/(k+dk*at)>scene)==(dk<0))high=at;else low=at;
        at=(low+high)*.5;
    }
    return 0;
}
float4 reflection(float3 p,float3 direction) {
    float forward=direction.z*Proj.z,z=p.z*Proj.z;
    float distance=SSR.y>0?SSR.y:Clip.w*4;
    if(forward>1e-6)distance=min(distance,(Clip.w*.999-z)/forward);
    if(forward<-1e-6)distance=min(distance,(Clip.z*1.001-z)/forward);
    if(distance<=.04)return 0;
    float3 start=p+direction*.02,end=p+direction*distance;
    float2 a=screen(start),delta=screen(end)-a;
    float2 bound=float2(delta.x<0?0:1,delta.y<0?0:1);
    float2 extent=abs(bound-a)/max(abs(delta),1e-6);
    float clipped=min(1,min(extent.x,extent.y));
    float k=1/(start.z*Proj.z),dk=(1/(end.z*Proj.z)-k)*clipped;delta*=clipped;
    float span=max(abs(delta.x)/Clip.x,abs(delta.y)/Clip.y);
    float steps=max(1,min(SSR.x,ceil(span)));
    // Conservatively skip empty depth intervals, then visit source pixels around geometry.
    // The bounded visit budget scales with Steps within the SM3 per-loop limit.
    float cursor=0,base=1/steps,stride=base,pixel=.5/max(span,1);
    float visits=Hierarchy.w>.5?min(SSR.x*12,240):steps;
    [loop]for(int visit=0;visit<visits&&cursor<1;++visit){
        float high=min(1,cursor+stride);bool possible=true;
        if(Hierarchy.w>.5){
            float za=1/(k+dk*cursor),zb=1/(k+dk*high);
            float2 bounds=intervalBounds(a+delta*cursor,a+delta*high);
            float thickness=max(SSR.z,max(za,zb)*.0002);
            possible=bounds.x<=max(za,zb)+thickness&&bounds.y>=min(za,zb)-thickness;
            if(possible&&stride>pixel){stride*=.5;continue;}
        }
        if(possible){float4 hit=reflectionHit(a,delta,k,dk,cursor,high);if(hit.a>0)return hit;}
        cursor=high;stride=min(base,stride*2);
    }
    return 0;
}
float lightVisibility(float3 p,float3 direction,int steps) {
    if(steps==0)return 1;
    float visible=1;
    [loop] for(int i=0;i<steps;++i){float3 q=p+direction*(.5+(i+1.)*(i+1.)*.8);
        if(q.z*Proj.z<=Clip.z)break;float2 uv=screen(q);if(any(uv<=0)||any(uv>=1))break;
        float z=linearDepth(tex2Dlod(FinalDepth,float4(uv,0,0)).r);if(z<Clip.w*.999&&q.z*Proj.z>z+max(.15,z*.008))visible=0;}
    return visible;
}
float4 waveHash(float2 cell) {
    float4 x=frac((cell.x+float4(0,1,0,1))*.1031),y=frac((cell.y+float4(0,0,1,1))*.1030);
    float4 h=x*(y+33.33)+y*(x+33.33);
    return frac((x+h)*(y+h)*19.19);
}
float3 liquidWaves(float2 world) {
    float2 p=world*.19+Proj.w*float2(.12,-.09);
    float3 wave=0;float weight=.57;
    [loop]for(int octave=0;octave<3;++octave){
        float2 f=frac(p),u=f*f*(3-2*f),du=6*f*(1-f);
        float4 h=waveHash(floor(p));float k=h.x-h.y-h.z+h.w;
        float height=h.x+(h.y-h.x)*u.x+(h.z-h.x)*u.y+k*u.x*u.y;
        float2 gradient=du*float2(h.y-h.x+k*u.y,h.z-h.x+k*u.x);
        wave+=float3(height,gradient*.18)*weight;
        p=float2(.6*p.x+.8*p.y,-.8*p.x+.6*p.y)*2.07+height*.7+17.3;
        weight*=.48;
    }
    return wave;
}
void liquidGeometry(float2 uv,out float3 p,out float3 world,out float3 normal,out float waterZ,out float coverage,out float crest,bool repairEdge=true) {
    float2 mask=tex2D(LiquidMask,uv).rg;float finalZ=linearDepth(tex2D(FinalDepth,uv).r);
    float maskEdge=abs(ddx(mask.y))+abs(ddy(mask.y));
    p=position(uv,mask.x);world=toWorld(p);
    float3 dx=ddx(p),dy=ddy(p);
    float footprint=dot(fwidth(world.xy),fwidth(world.xy));
    if(mask.y<=.003||mask.x<=Clip.z||mask.x>finalZ+max(.012,mask.x*.0007))discard;
    // A foreground actor can leave a hole in the liquid mask. Reconstruct its
    // edge from covered neighbours instead of retaining bright native water.
    if(!repairEdge&&maskEdge>.5)discard; // SSR retains the material's sky term at an uncertain edge.
    [branch]if(repairEdge&&maskEdge>.5){
        float2 stepX=float2(Clip.x,0),stepY=float2(0,Clip.y);
        float2 right=tex2Dlod(LiquidMask,float4(uv+stepX,0,0)).rg,left=tex2Dlod(LiquidMask,float4(uv-stepX,0,0)).rg;
        float2 down=tex2Dlod(LiquidMask,float4(uv+stepY,0,0)).rg,up=tex2Dlod(LiquidMask,float4(uv-stepY,0,0)).rg;
        if(max(right.y,left.y)<=.003||max(down.y,up.y)<=.003)discard;
        bool useRight=right.y>.003,useDown=down.y>.003;
        dx=position(uv+stepX*(useRight?1:-1),useRight?right.x:left.x)-p;
        dy=position(uv+stepY*(useDown?1:-1),useDown?down.x:up.x)-p;
    }
    waterZ=mask.x;coverage=saturate(mask.y);
    float3 geometric=cross(dx,dy);float lengthSquared=dot(geometric,geometric);
    if(!(lengthSquared>1e-12&&lengthSquared<1e20))discard;geometric*=rsqrt(lengthSquared);
    float3 worldNormal=geometric.x*Inv0.xyz+geometric.y*Inv1.xyz+geometric.z*Inv2.xyz;
    if(abs(worldNormal.z)<.88)discard; // waterfalls keep their native material
    float3 wave=liquidWaves(world.xy);crest=wave.x;
    float3 up=toViewDirection(float3(0,0,1));float facing=dot(up,normalize(-p));
    // At grazing angles a wave must not turn a reflection down into the bed.
    float2 slope=wave.yz*abs(facing)*rsqrt(1+footprint*.25);
    normal=normalize(up-toViewDirection(float3(slope,0)));
    normal*=facing<0?-1:1;
}
float4 WaterSSR(float2 uv:TEXCOORD0):COLOR0 {
    uv=(floor(uv/Clip.xy)+.5)*Clip.xy;
    float3 p,world,normal;float waterZ,coverage,crest;liquidGeometry(uv,p,world,normal,waterZ,coverage,crest,false);
    return reflection(p,reflect(normalize(p),normal));
}
float airTransmission(float2 uv,float waterZ) {
    if(FogControl.z<.5)return 1;
    float2 grid=uv/FogControl.xy-.5,cell=floor(grid),fraction=frac(grid);
    float weighted=0,total=0,nearest=1,nearestDelta=1e20;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
        float2 at=(cell+float2(x,y)+.5)*FogControl.xy;
        float receiver=tex2Dlod(AirDistance,float4(at,0,0)).r;
        float2 wet=tex2Dlod(LiquidMask,float4(at,0,0)).rg;
        if(wet.y>.003&&wet.x>Clip.z&&wet.x<=receiver+max(.012,wet.x*.0007))receiver=wet.x;
        float delta=abs(receiver-waterZ),transmission=tex2Dlod(AirFog,float4(at,0,0)).a;
        float weight=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y)*exp(-delta/max(.2,waterZ*.02));
        weighted+=transmission*weight;total+=weight;
        if(delta<nearestDelta){nearestDelta=delta;nearest=transmission;}
    }
    return saturate(total>1e-5?weighted/total:nearest);
}
float4 LateWaterSSR(float2 uv:TEXCOORD0):COLOR0 {
    float4 hit=tex2D(Reflections,uv);
    float4 current=tex2D(SourceColor,(floor(uv/Clip.xy)+.5)*Clip.xy);
    float3 p,world,normal;float waterZ,coverage,crest;liquidGeometry(uv,p,world,normal,waterZ,coverage,crest);
    // Geometry derivatives must run before divergent flow. Empty reflections
    // replace no sky radiance and need no depth-aware fog upsample.
    [branch]if(all(hit==0))return current;
    float fresnel=.02+.98*pow(1-saturate(dot(normal,normalize(-p))),5);
    float3 sky=saturate(Ambient.rgb*1.25+float3(.11,.18,.25));
    float3 delta=(hit.rgb-sky*hit.a)*(fresnel*WaterOptions.z*coverage*waterTransmittance(waterZ)*airTransmission(uv,waterZ));
    // Replace only the early material's sky term. Signed delta is evaluated before UNORM storage.
    return float4(current.rgb+delta,current.a);
}
float4 water(float2 uv,int lightSteps) {
    float3 p,world,normal;float waterZ,coverage,crest;liquidGeometry(uv,p,world,normal,waterZ,coverage,crest);
    float3 view=normalize(-p);
    float fresnel=.02+.98*pow(1-saturate(dot(normal,view)),5);
    float thickness=max(0,opaqueDistance(uv)-waterZ);
    float3 transmitted=float3(.025,.16,.20);
    if(WaterOptions.x>.5){
        float3 plane=toViewDirection(float3(0,0,1));plane*=dot(plane,view)<0?-1:1;
        float2 offset=(normal.xy-plane.xy)*min(.018,thickness*.0015)*edgeFade(uv);float2 refracted=saturate(uv+offset);
        if(opaqueDistance(refracted)<waterZ-.03)refracted=uv;
        float3 attenuation=exp(-min(thickness,80)*float3(.13,.048,.023));
        transmitted=tex2D(SourceColor,refracted).rgb*attenuation+transmitted*(1-attenuation);
    }
    float3 sky=saturate(Ambient.rgb*1.25+float3(.11,.18,.25));
    float4 hit=0;if(WaterOptions.w>.5)hit=tex2D(Reflections,uv);
    float3 reflected=sky*(1-hit.a*WaterOptions.z)+hit.rgb*WaterOptions.z;
    float3 light=normalize(toViewDirection(Light.xyz));
    float glint=0;if(WaterOptions.y>.5)glint=pow(saturate(dot(reflect(-light,normal),view)),96)*lightVisibility(p,light,lightSteps);
    float shore=(1-smoothstep(.1,1.25,thickness))*smoothstep(.35,.7,crest);
    float3 color=lerp(transmitted,reflected,fresnel)+Direct.rgb*glint*.5+max(0,shore)*.12;
    color=lerp(NativeFogColor.rgb,color,waterTransmittance(waterZ));
    return float4(color*coverage,coverage);
}
float4 WaterQuality(float2 uv:TEXCOORD0):COLOR0{return water(uv,8);}
float4 WaterBalanced(float2 uv:TEXCOORD0):COLOR0{return water(uv,4);}
float4 WaterPerformance(float2 uv:TEXCOORD0):COLOR0{return water(uv,0);}
float4 WaterPointLights(float2 uv:TEXCOORD0):COLOR0 {
    float3 p,world,normal;float waterZ,coverage,crest;liquidGeometry(uv,p,world,normal,waterZ,coverage,crest);
    float3 view=normalize(-p),color=0;
    [loop] for(int light=0;light<4;++light){
        if(light>=PointCount.x)break;
        float3 delta=PointPosition[light].xyz-world;float distance=length(delta);
        float falloff=saturate((PointPosition[light].w-distance)*PointColor[light].w);
        if(falloff<=0)continue;
        float3 direction=normalize(toViewDirection(delta));float visible=1;
        [loop] for(int step=1;step<4;++step){float3 q=p+direction*(distance*(step*.25));
            if(q.z*Proj.z<=Clip.z)break;float2 at=screen(q);if(any(at<=0)||any(at>=1))break;
            float opaque=linearDepth(tex2Dlod(FinalDepth,float4(at,0,0)).r);if(opaque<Clip.w*.999&&q.z*Proj.z>opaque+max(.15,opaque*.008))visible=0;}
        float specular=pow(saturate(dot(reflect(-direction,normal),view)),64);
        color+=min(PointColor[light].rgb,8)*falloff*falloff*visible*specular*.2;
    }
    return float4(color*(coverage*waterTransmittance(waterZ)),0);
}

float3 random3(float3 cell) {return frac(sin(float3(dot(cell,float3(127.1,311.7,74.7)),dot(cell,float3(269.5,183.3,246.1)),dot(cell,float3(113.5,271.9,124.6))))*43758.5453);}
float4 precipitation(float2 uv,int layers) {
    float scene=linearDepth(tex2D(FinalDepth,uv).r);float opacity=0;
    [unroll] for(int layer=0;layer<3;++layer) {
        if(layer>=layers)break;
        float distance=layer==0?3.5:layer==1?11:26;
        float weight=layer==0?.45:layer==1?.35:.20;
        float3 p=toWorld(position(uv,distance));
        float time=Proj.w,scale=Weather.y>.5?.8:1.2;
        p+=float3(time*.5,time*.2,time*(Weather.y>.5?1.3:19));p*=scale;
        float3 cell=floor(p),seed=random3(cell);float3 delta=frac(p)-seed;
        if(Weather.y>.5)delta.xy+=sin(time*.7+cell.z+seed.xy*6.28)*.08;
        float radius=Weather.y>.5?.055:.014;
        float shape=Weather.y>.5?saturate(1-length(delta)/radius):saturate(1-length(delta.xy)/radius)*saturate(1-abs(delta.z)/.32);
        shape*=smoothstep(distance-.5,distance+.5,scene)*step(seed.z,Weather.x);
        opacity+=shape*weight;
    }
    float alpha=saturate(opacity*.45*Weather.x);
    float3 color=saturate(float3(.45,.53,.62)+Ambient.rgb*.45+Direct.rgb*.15);
    if(Weather.y>.5)color=lerp(color,1,.55);
    return float4(color*alpha,alpha);
}
float4 PrecipitationQuality(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,3);}
float4 PrecipitationPerformance(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,2);}
