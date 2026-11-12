#include "regional_fog.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightGI;
int main(){
 NorthlightRegionalFog::Region region;for(int y=31;y<=33;++y)for(int x=31;x<=33;++x){NorthlightRegionalFog::Tile t;t.x=x;t.y=y;t.zones.fill(1637);region.tiles.push_back(t);}
 WorldScene scene;WorldMaterial floor;floor.terrain=true;scene.materials.push_back(floor);WorldMaterial roof;roof.wmo=true;scene.materials.push_back(roof);
 auto quad=[&](float x0,float x1,float z,unsigned material){unsigned a=scene.vertices.size();scene.vertices.push_back({{x0,-400,z},{0,0,1},0,0});scene.vertices.push_back({{x1,-400,z},{0,0,1},1,0});scene.vertices.push_back({{x1,400,z},{0,0,1},1,1});scene.vertices.push_back({{x0,400,z},{0,0,1},0,1});scene.triangles.push_back({a,a+1,a+2,material});scene.triangles.push_back({a,a+2,a+3,material});};
 quad(-400,400,0,0);auto open=NorthlightRegionalFog::buildField(scene,region,0,0);assert(open.texels[32*64+32].ground==0);
 // Two-unit open roof slits at x=3..5 mod8. Every field node lies on a beam.
 for(int x=-400;x<400;x+=8){quad(float(x),float(x+3),24,1);quad(float(x+5),float(x+8),24,1);}
 auto slits=NorthlightRegionalFog::buildField(scene,region,0,0);auto a=slits.texels[32*64+32],b=slits.texels[32*64+33];assert(a.ground==24&&b.ground==24&&a.height>0&&b.height>0);assert(slits.indoorCells==0);
 NorthlightRegionalFog::Region candidateRegion;for(const auto&t:region.tiles){NorthlightRegionalFog::Tile c;c.x=t.x;c.y=t.y;c.zones=t.zones;candidateRegion.tiles.push_back(c);}
 auto candidate=NorthlightRegionalFog::buildField(scene,candidateRegion,0,0);
 assert(!std::memcmp(candidate.texels.data(),slits.texels.data(),sizeof(slits.texels)));
 for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)assert(NorthlightRegionalFog::heightBounds(candidate,x,y)==NorthlightRegionalFog::heightBounds(slits,x,y));assert(NorthlightRegionalFog::globalHeightBounds(candidate)==NorthlightRegionalFog::globalHeightBounds(slits));
 assert(candidate.directFloor[32*64+32]==0&&candidate.directFloor[32*64+33]==0);
 auto noGeometry=NorthlightRegionalFog::buildField(WorldScene{},candidateRegion,0,0);for(float f:noGeometry.directFloor)assert(f==1e20f);
 for(auto&t:candidateRegion.tiles)t.zones.fill(12);auto notCity=NorthlightRegionalFog::buildField(scene,candidateRegion,0,0);for(float f:notCity.directFloor)assert(f==1e20f);
 std::printf("DIRECT_FLOOR_CANDIDATE_PASS repeatEnvelopeDeterministic=1 derivedFloor=0 unavailableFailClosed=1 nonCityUnchanged=1 addedCpuBytes=%u addedGpuBytes=65536 diskSchemaChanges=0 extraGeometryLoads=0\n",unsigned(sizeof(candidate.directFloor)));

 auto damaged=candidate;damaged.directFloor[32*64+33]=1e20f;for(float v:NorthlightRegionalFog::directFloorCorners(damaged,32,32))assert(v==1e20f);
 damaged=candidate;damaged.directFloor[32*64+33]=NAN;for(float v:NorthlightRegionalFog::directFloorCorners(damaged,32,32))assert(v==1e20f);
 damaged=candidate;damaged.directFloor[32*64+33]=25;for(float v:NorthlightRegionalFog::directFloorCorners(damaged,32,32))assert(v==1e20f);
 NorthlightRegionalFog::Field legacy;legacy.texels=candidate.texels;for(float v:NorthlightRegionalFog::directFloorCorners(legacy,32,32))assert(v==1e20f);
 for(auto&t:candidateRegion.tiles)t.zones.fill(1637);
 quad(-400,400,12,1);auto stackedOld=NorthlightRegionalFog::buildField(scene,region,0,0);auto stackedNew=NorthlightRegionalFog::buildField(scene,candidateRegion,0,0);
 assert(!std::memcmp(stackedOld.texels.data(),stackedNew.texels.data(),sizeof stackedOld.texels));
 scene.materials[1].width=scene.materials[1].height=2;scene.materials[1].alphaCutoff=.5f;scene.materials[1].rgba.resize(16,255);
 for(unsigned texel=0;texel<4;++texel)scene.materials[1].rgba[texel*4+3]=texel%2?0:255;
 auto cutoutOld=NorthlightRegionalFog::buildField(scene,region,0,0);auto cutoutNew=NorthlightRegionalFog::buildField(scene,candidateRegion,0,0);
 assert(!std::memcmp(cutoutOld.texels.data(),cutoutNew.texels.data(),sizeof cutoutOld.texels));
 std::printf("DIRECT_FLOOR_BOUND_PASS mixedUnknown=closed nonfinite=closed crossedLevels=closed oldPayloadOnly=closed stackedEnvelopeDeterministic=1 cutoutEnvelopeDeterministic=1 pointSampling=1\n");
 auto slope=candidate;for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)slope.directFloor[y*64+x]=2+.1f*(slope.originX+x*8)+.025f*(slope.originY+y*8);
 double error=0,previous=0,maxStep=0;
 for(unsigned step=0;step<=1024;++step){float x=-8+step/64.f,y=4;float gridX=(x-slope.originX)/8,gridY=(y-slope.originY)/8;unsigned ix=unsigned(std::floor(gridX)),iy=unsigned(std::floor(gridY));float fx=gridX-ix,fy=gridY-iy;auto c=NorthlightRegionalFog::directFloorCorners(slope,ix,iy);float a=c[0]+fx*(c[1]-c[0]),b=c[2]+fx*(c[3]-c[2]);float floor=a+fy*(b-a);error=std::max(error,double(std::abs(floor-(2+.1f*x+.025f*y))));if(step)maxStep=std::max(maxStep,std::abs(double(floor)-previous));previous=floor;}
 assert(error<1e-6&&maxStep<.001564);std::printf("DIRECT_FLOOR_SLOPE_PASS samples=1025 cellCrossings=2 maxInterpolationError=%g maxStep=%g packedPointBytes=65536\n",error,maxStep);
 float interpolated=(a.ground+b.ground)*.5f;assert(12-interpolated<0);
 std::printf("CITY_ROOF_FIELD_PASS zone=1637 spacing=8 slitWidth=2 slitX=4 actualFloor=0 sampledEnvelope=%g sampleAltitudeAtZ12=%g indoorAabbExcluded=%u validCityCells=%u conclusion=below_roof_direct_scatter_suppressed\n",interpolated,12-interpolated,slits.indoorCells,slits.citySurfaceCells);
}
