// Post-native material transfer. The patch preserves the original VS, texture
// sampling, material alpha and kills. HDR preserves authored RGB headroom,
// just like the particle emission path; SDR retains the UNORM baseline.
float4 AuroraEmission : register(c0); // additional linear RGB per native luminance; common native intensity
float4 AuroraNative : register(c1); // native HDR display gain (2^legacy EV); SDR output gain = 1
float3 decodeAurora(float3 encoded) {
    encoded=max(encoded,0);
    return lerp(encoded/12.92,pow((encoded+.055)/1.055,2.4),step(.04045,encoded));
}
float3 auroraAdditional(float3 native) {
    return dot(saturate(native),float3(.2126,.7152,.0722))*AuroraEmission.rgb;
}
// TEXCOORD keeps the SM2 compiler from assuming COLOR is already <= 1. The
// bytecode linker replaces this input with the original material's output.
float4 AuroraLinear(float4 native:TEXCOORD0):COLOR0 {
    // Authored additive masks express emitter intensity, including dark ribbons.
    // Keep their RGB ratios instead of treating them as SDR diffuse surfaces.
    // Preserve any authored overshoot with the same decode as HDR particles.
    float3 radiance=max(max(native.rgb,0),decodeAurora(native.rgb))*AuroraEmission.w*AuroraNative.x;
    // The complete native sky already contains its UNORM baseline. Add only
    // the emitter's excess, once. The legacy EV control scales native light;
    // Northlight's separate twilight E keeps its own visible/GI scale.
    float3 headroom=max(radiance-min(decodeAurora(saturate(native.rgb))*AuroraEmission.w,1),0);
    return float4(headroom+auroraAdditional(native.rgb),native.a);
}
float4 AuroraLegacy(float4 native:COLOR0):COLOR0 {
    // Preserve the authored SDR baseline, adding the same linear E as HDR/GI.
    float3 radiance=(decodeAurora(native.rgb)*AuroraEmission.w+auroraAdditional(native.rgb))*AuroraNative.x;
    float3 encoded=lerp(radiance*12.92,1.055*pow(max(radiance,0),1/2.4)-.055,step(.0031308,radiance));
    return float4(encoded,native.a);
}
