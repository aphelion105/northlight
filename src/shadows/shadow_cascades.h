#pragma once

namespace NorthlightShadowCascades {
inline constexpr float NearRadius=48.f,FarRadius=288.f;
inline constexpr float CacheScale=1.25f;
inline constexpr float StaticRadius=FarRadius*CacheScale;
// Enclose the cached far map, +/-640 light depth, an 80-unit camera orbit
// and the full 160-unit publication hold. GI keeps its local volume.
inline constexpr float TerrainRadius=1120.f;
}
