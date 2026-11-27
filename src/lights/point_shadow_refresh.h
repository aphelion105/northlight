#pragma once
#include <array>
#include <cstdint>

namespace NorthlightPointShadow {
// Hold at most one due update for a stationary cube covering a quarter of
// the screen or less. Alternate slots to spread work across render frames.
inline bool holdSmallCube(bool previouslyHeld, uint32_t age, uint32_t frame, unsigned slot, long area, uint64_t fullArea) {
    return !previouslyHeld && age < 200 && frame && area > 0 && uint64_t(area) * 4 <= fullArea && ((frame + slot) & 1u);
}

struct CubePose {
    std::array<float, 3> position{};
    float range = 0.0f;

    bool close(const CubePose& other, float yards) const {
        if (range != other.range) return false;
        const float x = position[0] - other.position[0];
        const float y = position[1] - other.position[1];
        const float z = position[2] - other.position[2];
        return x*x + y*y + z*z <= yards*yards;
    }
};

// A partially written cube stays private. Both caster passes and every sample
// use the pose at which that bank was built.
struct MovingCube {
    static constexpr float RefreshDistance = 0.0625f;
    static constexpr float SampleDistance = 0.35f;
    uint64_t id = 0;
    CubePose published{};
    CubePose staging{};
    int front = -1;
    unsigned nextFace = 0;

    bool prepare(uint64_t source, const CubePose& pose) {
        if (id != source) {
            *this = {};
            id = source;
        }
        if (!source) return false;
        if (nextFace && staging.close(pose, SampleDistance)) return true;
        nextFace = 0;
        if (front >= 0 && published.close(pose, RefreshDistance)) return false;
        staging = pose;
        return true;
    }

    int back() const { return front == 0 ? 1 : 0; }

    bool completeFace() {
        if (++nextFace != 6) return false;
        front = back();
        published = staging;
        nextFace = 0;
        return true;
    }

    bool usable(const CubePose& current) const {
        return front >= 0 && published.close(current, SampleDistance);
    }
};
}
