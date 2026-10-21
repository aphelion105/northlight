sampler2D Diffuse:register(s0);
float4 Texel:register(c0); // source texel size, normal strength, unused
float luminance(float2 uv){return dot(tex2Dlod(Diffuse,float4(uv,0,0)).rgb,float3(.299,.587,.114));}
float4 AutoNormal(float2 uv:TEXCOORD0):COLOR0 {
    // Central differences in texture space, independent of the screen footprint.
    float x=luminance(uv+float2(Texel.x,0))-luminance(uv-float2(Texel.x,0));
    float y=luminance(uv+float2(0,Texel.y))-luminance(uv-float2(0,Texel.y));
    float3 normal=normalize(float3(-x*Texel.z,-y*Texel.z,1));
    float center=luminance(uv);
    float2 radius=Texel.xy*4;
    float around=(luminance(uv+float2(radius.x,0))+luminance(uv-float2(radius.x,0))+luminance(uv+float2(0,radius.y))+luminance(uv-float2(0,radius.y)))*.25;
    float cavity=1-saturate((around-center-.01)*3);
    // Positive tangent Z is reconstructed, leaving B for height and A for cavity AO.
    return float4(normal.xy*(127.0/255)+128.0/255,center,cavity);
}
