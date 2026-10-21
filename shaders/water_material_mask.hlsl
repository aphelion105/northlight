// Northlight-owned geometry mask. Uses the native liquid position constant ABI,
// independently of the shader variant and its per-vertex specular colour.
float4 LiquidProjection[4] : register(c0);
float4 LiquidModelView[4] : register(c5);
struct LiquidVertex {float4 clip:POSITION;float distance:TEXCOORD7;};
LiquidVertex WaterMaterialMaskVS(float3 position:POSITION) {
    float4 view=position.x*LiquidModelView[0]+position.y*LiquidModelView[1]+position.z*LiquidModelView[2]+LiquidModelView[3];
    LiquidVertex output;
    output.clip=view.x*LiquidProjection[0]+view.y*LiquidProjection[1]+view.z*LiquidProjection[2]+view.w*LiquidProjection[3];
    output.distance=abs(output.clip.w);
    return output;
}
float4 WaterMaterialMaskPS(float distance:TEXCOORD7):COLOR0 {return float4(distance,1,0,1);}
