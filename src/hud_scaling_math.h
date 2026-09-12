#pragma once

#include <cstdint>

namespace hud_scaling {

constexpr int32_t kReferenceWidth = 1024;
constexpr int32_t kReferenceHeight = 768;

float CalculateScale(int32_t width, int32_t height, float multiplier);
int32_t ScaleCoordinate(int32_t coordinate, int32_t referenceExtent,
                        int32_t outputExtent, float scale);

}  // namespace hud_scaling
