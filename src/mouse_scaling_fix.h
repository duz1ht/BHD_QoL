#pragma once

#include <cstdint>

namespace mouse_scaling_fix {

bool Install(bool enabled);
void Reset();

struct PredictionState {
    int32_t scale;
    int64_t remainder[2];
    bool active;
    bool fixEnabled;
};

PredictionState GetPredictionState();
int32_t ScaleForPrediction(int32_t delta, int32_t scale, int axis,
                           PredictionState* state);

} // namespace mouse_scaling_fix
