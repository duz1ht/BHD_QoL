#pragma once

#include <windows.h>

#include <cstdint>

namespace high_frequency_camera {

enum class ApplyResultCode {
    kApplied,
    kDisabled,
    kNullSource,
    kWrongCameraMode,
    kMouseDisabled,
    kNoLocalPlayer,
    kWrongCameraTarget,
    kRawInputUnavailable,
    kNoPendingMovement,
    kZeroOffset,
};

struct ApplyResult {
    ApplyResultCode code = ApplyResultCode::kDisabled;
    int32_t cameraMode = 0;
    uintptr_t localPlayer = 0;
    uintptr_t cameraTarget = 0;
    LONG pendingX = 0;
    LONG pendingY = 0;
    LONG projectedX = 0;
    LONG projectedY = 0;
    uint32_t projectionPhaseQ16 = 0;
    int16_t yawCommand = 0;
    int16_t pitchCommand = 0;
    int32_t yawOffset = 0;
    int32_t pitchOffset = 0;
};

bool Apply(void* sourceCamera, ApplyResult* result = nullptr);
void SetEnabled(bool enabled);

} // namespace high_frequency_camera
