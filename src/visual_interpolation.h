#pragma once

#include <cstdint>

#include "visual_interpolation_math.h"

namespace visual_interpolation {

bool Install(bool enabled);
bool Enabled();
void LogStatistics();

// Adds the render-only owner-root correction to a stack-local camera source.
// Returns false when the guarded synchronized path is not valid for this frame.
bool ApplyCameraCorrection(void* cameraSource, std::uint32_t cameraSourceSize);

}  // namespace visual_interpolation
