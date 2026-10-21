float4 Projection[4]:register(c0);
float4 ModelView[4]:register(c5);
struct NativeVertex {float4 position:POSITION;float4 color:COLOR0;};
NativeVertex NativeWater(float3 p:POSITION) {
    NativeVertex o;float4 v=ModelView[0]*p.x+ModelView[1]*p.y+ModelView[2]*p.z+ModelView[3];
    o.position=Projection[0]*v.x+Projection[1]*v.y+Projection[2]*v.z+Projection[3]*v.w;
    o.color=float4(.3+abs(p.x)*.02,.8,1,1);return o;
}
NativeVertex OtherWater(float3 p:POSITION) {NativeVertex o=NativeWater(p);o.color=float4(1,.8,abs(p.x)*.01,1);return o;}
float4 NativeColor(float4 color:COLOR0):COLOR0{return color;}
