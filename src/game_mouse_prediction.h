#pragma once

#include <cstdint>

namespace game_mouse_prediction {

struct State {
    int32_t divisor;
    int32_t previousX;
    int32_t previousY;
    int32_t scale;
    bool invertY;
    int32_t zoomDivisor;
    int16_t yawFilter;
};

struct Result {
    int32_t yawControl;
    int32_t pitchControl;
};

Result Simulate(const State& state, int32_t rawX, int32_t rawY);

}  // namespace game_mouse_prediction
