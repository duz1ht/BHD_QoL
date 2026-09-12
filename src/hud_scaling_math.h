#pragma once

#include <cstdint>

namespace hud_scaling {

constexpr int32_t kReferenceWidth = 1024;
constexpr int32_t kReferenceHeight = 768;

enum class HorizontalAnchor { Left, Center, Right };
enum class VerticalAnchor { Top, Center, Bottom };

float CalculateScale(int32_t width, int32_t height, float multiplier);
int32_t ScalePosition(int32_t coordinate, int32_t referenceExtent, int32_t outputExtent,
                      float scale, HorizontalAnchor anchor);
int32_t ScalePosition(int32_t coordinate, int32_t referenceExtent, int32_t outputExtent,
                      float scale, VerticalAnchor anchor);
int32_t ScaleExtent(int32_t extent, float scale);

}  // namespace hud_scaling
