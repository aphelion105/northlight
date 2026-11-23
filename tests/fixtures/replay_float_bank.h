#pragma once

// GPU fixtures can be built against the current renderer or an older control.
template<class Replay>
auto replayFixtureFloats(Replay& replay, int) -> decltype(replay.writableConstants()) {
    return replay.writableConstants();
}

template<class Replay>
float* replayFixtureFloats(Replay& replay, long) {
    return replay.constantStorage;
}

template<class Replay>
float* replayFixtureFloats(Replay& replay) {
    return replayFixtureFloats(replay, 0);
}
