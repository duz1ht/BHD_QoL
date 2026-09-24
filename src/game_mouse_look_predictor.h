#pragma once

#include <cstdint>

namespace game_mouse_look_predictor {

struct LookActions {
    int16_t pitch;
    int16_t yaw;
};

struct BranchResult {
    int32_t firstX;
    int32_t firstY;
    int32_t scaledX;
    int32_t scaledY;
    LookActions actions;
    int32_t yawControl;
    int32_t pitchControl;
    int32_t yawStep;
    int32_t pitchStep;
    uint32_t yaw;
    uint32_t pitch;
};

struct Result {
    BranchResult zero;
    BranchResult moved;
    int32_t yawDelta;
    int32_t pitchDelta;
};

int32_t PredictYawControl(int16_t yawInput, int16_t filterState);
int32_t PredictPitchControl(int16_t pitchInput, bool applyNormalScale);
int32_t ControlToStep(int32_t control);
bool Predict(void* player, int32_t rawDx, int32_t rawDy, Result* result);

}  // namespace game_mouse_look_predictor
