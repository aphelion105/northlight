// Run with an empty scratch directory, the packaged water-core.dll and a DLL without the water ABI.
#include "../tideglass/integrations/host_settings.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const fs::path root = fs::absolute(argv[1]);
    require(fs::is_directory(root) && fs::is_empty(root), "scratch directory must be empty");
    const auto core = root / "water-core.dll";
    auto configure = [&](WaterHostSettings& settings, bool enabled) {
        std::ofstream(root / "water-host.ini") << "[WaterIntegration]\nEnable=" << enabled << "\nCompositeAfterEffects=1\n";
        require(settings.ConfigureRoot(root.wstring()), "read settings");
    };
    WaterHostSettings missing;
    configure(missing, true);
    require(!missing.Open() && !missing.CompositeAfterEffects(), "missing core falls back to native water");
    fs::copy_file(argv[3], core);
    WaterHostSettings incompatible;
    configure(incompatible, true);
    require(!incompatible.Open() && !incompatible.CompositeAfterEffects(), "incompatible core falls back");
    fs::copy_file(argv[2], core, fs::copy_options::overwrite_existing);
    require(!missing.Open() && !incompatible.Open(), "failed loads stay inactive until restart");
    WaterHostSettings off;
    configure(off, false);
    require(!off.Open() && !off.CompositeAfterEffects(), "disabled host retains native water with a valid core");
    WaterHostSettings on;
    configure(on, true);
    bool capture = false, replay = false;
    std::thread recorder([&] { capture = on.Open(); });
    std::thread renderer([&] { replay = on.Open(); });
    recorder.join();
    renderer.join();
    require(capture && replay && on.CompositeAfterEffects(), "capture and replay share a working module");
    WaterHostWorldFrame world{};
    world.mapId=0;world.zoneId=1497;
    world.values.viewport={0,0,800,600,0.f,1.f};
    world.values.cameraRelativeView[0]=world.values.cameraRelativeView[5]=world.values.cameraRelativeView[10]=world.values.cameraRelativeView[15]=1.f;
    world.values.glProjection[0]=world.values.glProjection[5]=1.f;
    world.values.glProjection[10]=1.01f;world.values.glProjection[11]=1.f;world.values.glProjection[14]=-0.2f;
    const auto* api=on.Module().API();
    for(const auto size:{uint32_t(sizeof(world.values)),uint32_t(sizeof(world))}){
        auto* frame=api->copy_frame(&world.values,size);
        require(frame!=nullptr,"both original and extended frame layouts are accepted");
        api->release_frame(frame);
    }
    std::cout << "PASS Tideglass startup and both frame layouts\n";
}
