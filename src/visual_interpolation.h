#pragma once

#include <cstdint>

namespace visual_interpolation {

struct VisualMouseAngles {
    uint32_t yawDelta;
    int32_t pitchDelta;
    bool valid;
};

// Enables sequence diagnostics.  Correction deliberately remains fail-closed
// until the authoritative poll-to-camera-root producer step is proven in game.
bool Install(bool visualMouseLateLatching);
void BeginVisualFrame();
VisualMouseAngles GetVisualMouseAngles();
void ApplyCameraCorrection(void* stackLocalCamera, uint32_t cameraMode,
                           bool primaryCallsite);
void ApplyViewmodelCorrection(void* stackLocalMatrix, uintptr_t returnAddress);
void Reset(const char* reason);

}  // namespace visual_interpolation
