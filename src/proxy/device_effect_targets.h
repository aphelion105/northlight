#pragma once

// Effect-owned GPU allocations, shared by the scene copy, AO and the HDR input.
// The Device brackets their lifetime with releaseResources and Reset.
struct NorthlightDeviceEffectTargets {
    IDirect3DTexture9 *scene = nullptr, *depthTex = nullptr, *ao = nullptr, *aoRaw = nullptr;
    IDirect3DSurface9 *sceneSurface = nullptr, *aoSurface = nullptr, *aoRawSurface = nullptr;
    IDirect3DPixelShader9 *aoPS = nullptr, *aoBlurPS = nullptr, *aoBlurNoWaterPS = nullptr, *aoContactBloomPS = nullptr, *aoOnlyPS = nullptr, *contactBloomPS = nullptr, *compositePS = nullptr;
    bool aoBlurNoWaterRefused=false;
    UINT width = 0, height = 0;
    D3DFORMAT sceneFormat = D3DFMT_UNKNOWN, depthSnapshotFormat = D3DFMT_UNKNOWN;
    bool df24Refused = false; // the device refused a DF24 depth snapshot: INTZ for the session
};
