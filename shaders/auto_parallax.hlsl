// Shared by native diffuse lookups and the normal/occlusion replay.
struct MaterialFrame {float3 p,tangent,bitangent,normal;float2 tx,ty;};
MaterialFrame materialFrame(float2 uv,float4 clipPosition,float4 projection) {
    MaterialFrame f;
    float3 p=float3(clipPosition.xy/projection.xy,clipPosition.w*projection.z);
    float3 dx=ddx(p),dy=ddy(p);float2 tx=ddx(uv),ty=ddy(uv);
    float determinant=tx.x*ty.y-tx.y*ty.x;
    float handedness=determinant<0?-1:1;
    float3 tangent=(dx*ty.y-dy*tx.y)*handedness,bitangent=(dy*tx.x-dx*ty.x)*handedness;
    // Pixel derivatives shrink at high resolution; keep valid small frames unit length.
    tangent*=rsqrt(max(dot(tangent,tangent),1e-30));bitangent*=rsqrt(max(dot(bitangent,bitangent),1e-30));
    float3 normal=cross(dx,dy);normal*=rsqrt(max(dot(normal,normal),1e-30));normal=dot(normal,-p)<0?-normal:normal;
    f.p=p;f.tangent=tangent;f.bitangent=bitangent;f.normal=normal;f.tx=tx;f.ty=ty;return f;
}
float parallaxFade(float distance,float2 tx,float2 ty,float range){return saturate((range-distance)/max(range*.4,1))*saturate(2-sqrt(max(dot(tx,tx),dot(ty,ty)))*128);}
float2 parallaxUVFrame(sampler2D heightMap,float2 uv,float distance,float4 control,MaterialFrame f) {
    float2 tx=f.tx,ty=f.ty;float3 p=f.p,tangent=f.tangent,bitangent=f.bitangent,normal=f.normal;
    float amplitude=control.x*parallaxFade(distance,tx,ty,control.y);
    if(amplitude<=0)return uv;
    float3 view=float3(dot(-p,tangent),dot(-p,bitangent),abs(dot(-p,normal)));
    // Limit the lateral footprint smoothly at grazing incidence. Independent
    // axis clamps rotate diagonal rays and stretch narrow painted grooves.
    float2 rayUV=view.xy*rsqrt(max(view.z*view.z+.25*dot(view.xy,view.xy),1e-20))*amplitude;
    // Generated height maps are at most 512 texels on either axis. Short rays
    // need fewer layers; grazing rays retain the configured search budget.
    float travel=max(abs(rayUV.x),abs(rayUV.y))*512;
    float stride=rcp(min(max(ceil(travel),4),control.z));
    float2 bottom=uv-.5*rayUV;
    float level=1;
    float gap=1-tex2Dgrad(heightMap,uv+.5*rayUV,tx,ty).b;
    float above=gap;
    // A level of zero is the bottom of the volume, so a crossing is guaranteed.
    [loop]for(int i=0;i<32 && gap>0;++i){
        above=gap;
        level-=stride;
        gap=level-tex2Dgrad(heightMap,bottom+level*rayUV,tx,ty).b;
    }
    // Split the contact interval once before interpolating. Both endpoints
    // come from actual samples; reconstruct positions from the original UV.
    float halfStride=stride*.5;
    float middle=level+halfStride;
    float midGap=middle-tex2Dgrad(heightMap,bottom+middle*rayUV,tx,ty).b;
    if(midGap<=0){level=middle;gap=midGap;}else above=midGap;
    level+=halfStride*saturate(-gap/max(above-gap,1e-6));
    return uv+(level-.5)*rayUV;
}
float2 parallaxUV(sampler2D heightMap,float2 uv,float4 clipPosition,float4 projection,float4 control) {
    return parallaxUVFrame(heightMap,uv,clipPosition.w,control,materialFrame(uv,clipPosition,projection));
}
float parallaxShadow(sampler2D heightMap,float2 uv,float height,float distance,MaterialFrame f,float4 control,float4 light,float steps) {
    float3 ray=float3(dot(light.xyz,f.tangent),dot(light.xyz,f.bitangent),dot(light.xyz,f.normal));
    float amplitude=control.x*parallaxFade(distance,f.tx,f.ty,control.y);
    if(amplitude<=0||light.w<=0||ray.z<=.04)return 1;
    float span=1-height;
    float2 extent=clamp(ray.xy/max(ray.z,.08),-6,6)*amplitude*span;
    float blocked=0;
    // The generated height is UNORM. At part=1 the ray is already above
    // every possible occluder once the bias is applied.
    [loop]for(int i=1;i<=16&&i<steps&&blocked<1;++i){
        float part=i/steps;
        float h=tex2Dgrad(heightMap,uv+extent*part,f.tx,f.ty).b;
        blocked=max(blocked,saturate((h-height-span*part-.012)*16));
    }
    return 1-blocked;
}
