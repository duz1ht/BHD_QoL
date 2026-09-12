#include "hud_scaling_math.h"

#include <algorithm>
#include <cmath>

namespace hud_scaling {

float CalculateScale(int32_t width, int32_t height, float multiplier) {
    if (width <= 0 || height <= 0 || !std::isfinite(multiplier) || multiplier <= 0.0f) {
        return 1.0f;
    }
    return std::min(static_cast<float>(width) / kReferenceWidth,
                    static_cast<float>(height) / kReferenceHeight) * multiplier;
}

int32_t ScaleCoordinate(int32_t coordinate, int32_t referenceExtent,
                        int32_t outputExtent, float scale) {
    if (referenceExtent <= 0 || outputExtent <= 0 || !std::isfinite(scale) || scale <= 0.0f) {
        return coordinate;
    }

    const int32_t quarter = referenceExtent / 4;
    float result = 0.0f;
    if (coordinate < quarter) {
        result = coordinate * scale;
    } else if (coordinate > referenceExtent - quarter) {
        result = outputExtent - (referenceExtent - coordinate) * scale;
    } else {
        result = outputExtent * 0.5f + (coordinate - referenceExtent * 0.5f) * scale;
    }
    return static_cast<int32_t>(std::lround(result));
}

}  // namespace hud_scaling
