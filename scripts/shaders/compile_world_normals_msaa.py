"""Build the experimental fused Vulkan WorldNormals shader from the existing math.

The SM3 source remains the independent image oracle. No runtime compilation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def source():
    text = (fp.SHADERS / 'world_effects.hlsl').read_text(encoding='utf-8')
    def function(name):
        import re
        match = re.search(r'^(?:float\d?|NormalDistanceOutput) ' + name + r'\(', text, re.M)
        assert match, name
        start = text.index('{', match.start()); depth = 1; end = start + 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}'); end += 1
        return text[match.start():end]
    code = '\n'.join(function(name) for name in (
        'depthUV', 'normalizedDepth', 'viewDistance', 'viewPositionDistance',
        'viewPosition', 'wideNeighbour', 'tangentAxis', 'worldNormalValue'))
    code = code.replace('tex2Dlod(Depth,', 'readDepth(').replace('tex2Dlod(Scene,', 'readScene(').replace('tex2Dlod(ModelNormals,', 'readModel(')
    prefix = '''// Generated from the existing WorldNormals functions. See the manifest.
struct Constants {
    float4 imageClip, projection, depthParams;
    float4 inverseView[3];
    float4 screenSize, modelInfo;
};
[[vk::push_constant]] Constants constants;
#define ImageClip constants.imageClip
#define Projection constants.projection
#define DepthParams constants.depthParams
#define InverseView constants.inverseView
#define ScreenSize constants.screenSize
#define ModelNormalInfo constants.modelInfo
[[vk::binding(0,0)]] Texture2DMS<float4> model;
[[vk::binding(1,0)]] Texture2D<float4> scene;
[[vk::binding(2,0)]] Texture2D<float> depth;
[[vk::binding(3,0),vk::image_format("rgba16f")]] RWTexture2D<float4> normalOutput;
[[vk::binding(4,0),vk::image_format("r32f")]] RWTexture2D<float> distanceOutput;
[[vk::binding(5,0)]] SamplerState sceneFilter;
int2 texel(float2 uv) { return clamp(int2(floor(uv*ScreenSize.xy)),int2(0,0),int2(ScreenSize.xy)-1); }
float4 readModel(float4 uv) { return model.Load(texel(uv.xy),0); }
float4 readScene(float4 uv) { return scene.SampleLevel(sceneFilter,uv.xy,0); }
float4 readDepth(float4 uv) { return depth.Load(int3(texel(uv.xy),0)).xxxx; }
'''
    suffix = '''
[numthreads(8,8,1)]
void WorldNormalsMsaa(uint3 pixel:SV_DispatchThreadID) {
    if(any(pixel.xy >= uint2(ScreenSize.zw)))return;
    // The D3D9 triangle spans [-.5, size-.5], with UV [0,1].
    float2 uv=(float2(pixel.xy)+.5)/ScreenSize.zw;
    normalOutput[pixel.xy]=worldNormalValue(uv);
    if(ModelNormalInfo.y>.5) {
        float2 q=(floor(uv*ScreenSize.zw)+.5)/ScreenSize.zw;
        distanceOutput[pixel.xy]=viewDistance(normalizedDepth(depthUV(q)));
    }
}
'''
    return prefix + code + suffix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dxc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    hlsl = args.output / 'world_normals_msaa.hlsl'; hlsl.write_text(source(), encoding='utf-8', newline='\n')
    spv = args.output / 'world_normals_msaa.spv'
    command = [str(args.dxc.resolve()), '-spirv', '-fspv-target-env=vulkan1.2', '-T', 'cs_6_0', '-E', 'WorldNormalsMsaa', '-Gis', '-O3', '-Fo', str(spv), str(hlsl)]
    subprocess.run(command, check=True)
    data = spv.read_bytes(); words = struct.unpack('<%dI' % (len(data)//4), data)
    assert words[0] == 0x07230203
    header = '#pragma once\nstatic constexpr uint32_t kWorldNormalsMsaa[] = {\n'
    header += '\n'.join(','.join('0x%08xu' % w for w in words[i:i+8])+',' for i in range(0,len(words),8)) + '\n};\n'
    (args.output/'world_normals_msaa_spirv.h').write_text(header,encoding='utf-8',newline='\n')
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = {'compiler': str(args.dxc.resolve()), 'compiler_sha256': sha(args.dxc), 'library_sha256': sha(args.dxc.parent/'dxcompiler.dll'), 'command': command,
                'sources': {str(p.relative_to(ROOT)):sha(p) for p in (Path(__file__),fp.SHADERS/'world_effects.hlsl')}, 'hlsl_sha256':sha(hlsl), 'spirv_sha256':sha(spv)}
    (args.output/'shader-build.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(spv,len(data),'bytes')


if __name__ == '__main__':
    main()
