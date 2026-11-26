#include "tideglass_x64_inputs.h"
#include <cassert>
#include <limits>

int main(){
    using namespace NorthlightTideglassX64;
    Inputs inputs;
    inputs.materials[0]={1,WaterHostLiquidMagma,-2};
    inputs.materials[1]={2,WaterHostLiquidSlime,-1};
    inputs.materials[2]={3,WaterHostLiquidMagma,-1};
    inputs.materials[3]={3,WaterHostLiquidSlime,-1};
    inputs.materialCount=4;
    assert(inputs.material(1).ramp==-2);
    assert(inputs.material(2).kind==WaterHostLiquidSlime);
    assert(inputs.material(3).kind==WaterHostLiquidUnknown);
    assert(inputs.material(4).kind==WaterHostLiquidUnknown);

    inputs.shipCount=1;
    auto& ship=inputs.ships[0];
    ship={42,{100,50,0},10};
    Wakes wakes;
    auto sample=[&](unsigned now){
        WaterFrameValues frame{};
        wakes.sample(inputs,now,frame);
        return frame;
    };
    assert(sample(1000).boatCount==0);
    ship.position[0]=110;
    auto output=sample(2000);
    assert(output.boatCount==1&&output.boats[0].direction[0]==1);
    const auto& trail=output.boats[0].trail;
    assert(output.boats[0].trailCount==2&&trail[0].x==110&&trail[1].x==100);
    assert(trail[1].age==1.f&&trail[1].speed==10);
    ship.position[0]=300;
    assert(sample(3000).boatCount==0);
    ship.position[0]=310;
    assert(sample(8000).boatCount==0);
    ship.position[0]=900;
    assert(sample(9000).boatCount==0);
    ship.position[0]=320;
    ship.speed=std::numeric_limits<float>::infinity();
    assert(sample(10000).boatCount==0);
    wakes.reset();
    ship.speed=10;
    assert(sample(11000).boatCount==0);
}
