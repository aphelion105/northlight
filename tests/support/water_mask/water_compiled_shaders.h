#pragma once
// Test-only stand-in for the generated appearance-pass shaders (removed from the renderer in 0.3.214), so the verbatim
// water_renderer_0_3_140.h still compiles: its render() names them. The fake device ignores the bytes and nothing calls render().
#include "windows.h"
static const DWORD kWaterReflectionShader[] = {0xffff0300,0x0000ffff};
static const DWORD kWaterCompositeShader[] = {0xffff0300,0x0000ffff};
