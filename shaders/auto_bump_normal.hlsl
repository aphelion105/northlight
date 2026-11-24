// Relative normal perturbations; the octahedron also marks the reference hemisphere.
// Material detail uses a fixed world frame; positive depth supports old captures.
// Rebase detail onto the current scene normal; neutral detail leaves it unchanged.
float3 autoBumpNormal(float2 uv,float3 position,float3 normal,float distance,float3 row0,float3 row1,float3 row2) {
    if(BumpControl.x<=0)return normal;
    [branch]if(distance>=BumpControl.y)return normal;
    float4 detail=tex2Dlod(BumpMaterial,float4(uv,0,0));
    float magnitudeSquared=dot(normal,normal);
    float3 neutral=normal*(sqrt(magnitudeSquared)*rsqrt(max(magnitudeSquared,1e-8)));
    float cover=saturate(detail.a);
    [branch]if(cover<=.02)return neutral;
    float inverseCover=1/max(cover,.001);
    float capturedZ=abs(detail.b)*inverseCover;
    [branch]if(abs(capturedZ-distance)>=max(.025,distance*.0015))return neutral;
    float2 oct=abs(detail.rg)*inverseCover*2-1;
    float3 view=float3(oct,1-abs(oct.x)-abs(oct.y));
    float fold=saturate(-view.z);view.xy+=float2(view.x>=0?-fold:fold,view.y>=0?-fold:fold);
    view*=rsqrt(max(dot(view,view),1e-8));
    bool world=detail.b<0;
    float3 reference=world?normal:float3(dot(normal,row0),dot(normal,row1),dot(normal,row2));
    reference*=rsqrt(max(dot(reference,reference),1e-8));
    float hemisphere=world&&view.z<0?-1:1;
    reference*=hemisphere;view.z=abs(view.z);
    float projected=dot(reference.xy,view.xy);
    float3 rebased=float3(view.xy+reference.xy*(view.z-projected/max(1+reference.z,1e-4)),reference.z*view.z-projected);
    float3 mapped=world?rebased*hemisphere:rebased.x*row0+rebased.y*row1+rebased.z*row2;
    float fade=saturate((BumpControl.y-distance)/max(BumpControl.y*.25,1));
    // The depth normal can have a small magnitude at a silhouette. Preserve it:
    // enabling neutral detail must not turn that uncertain normal into full light.
    float magnitude=sqrt(magnitudeSquared);
    float3 blended=lerp(normal,mapped*magnitude,BumpControl.x*fade);
    return blended*(magnitude*rsqrt(max(dot(blended,blended),1e-8)));
}

// Only identified solid receivers contribute to reflected local light.
bool materialDepthMatches(float4 capture,float distance){
    float cover=saturate(capture.a);
    return cover>.02&&abs(abs(capture.b)-distance*cover)<max(.025,distance*.0015)*cover;
}
float materialLampResponse(float2 uv,float z,float3 eye,float3 normal,float3 light,bool rim=false){
    if(BumpControl.x<=0||z>=BumpControl.y)return 0;
    float4 capture=tex2Dlod(BumpMaterial,float4(uv,0,0));
    if((capture.r<0)==(capture.g<0)||!materialDepthMatches(capture,z))return 0;
    float3 halfVector=normalize(light+eye);
    float alignment=saturate(dot(normal,halfVector));
    float facing=saturate(4*dot(normal,light));
    float lobe=pow(max(alignment,.00001),38)*facing;
    // Broad spill from the blue water onto rough stone, including the grazing faces of the rim.
    if(rim&&capture.g<0)lobe=max(lobe,(.20+.5*alignment*alignment)*(.45+.55*facing));
    return lobe*saturate((BumpControl.y-z)/12);
}
