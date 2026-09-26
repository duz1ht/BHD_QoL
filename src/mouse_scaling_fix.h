#pragma once

#include <cstdint>

namespace mouse_scaling_fix {

struct FractionState {
    int32_t scale = 0;
    int64_t remainder[2] = {};
    bool active = false;
};

bool Install(bool enabled);
void Reset();
int32_t EvaluateWithState(int32_t delta, int32_t scale, int32_t baseScale,
                          int axis, FractionState* state);

} // namespace mouse_scaling_fix
