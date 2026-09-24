#pragma once

#include <cstdint>

namespace presentation_math {

// Counter subtraction deliberately uses unsigned arithmetic so it remains
// correct when Windows' 32-bit cumulative counter wraps.
inline int32_t CounterDelta(int32_t current, int32_t previous) {
    return static_cast<int32_t>(static_cast<uint32_t>(current) -
                                static_cast<uint32_t>(previous));
}

inline int32_t AngleDelta(uint32_t current, uint32_t previous) {
    return static_cast<int32_t>(current - previous);
}

inline uint32_t AddPredictedAngle(uint32_t base, int32_t mouseDelta,
                                  double angleUnitsPerCount) {
    const double value = static_cast<double>(mouseDelta) * angleUnitsPerCount;
    const auto step = static_cast<int64_t>(value >= 0.0 ? value + 0.5 : value - 0.5);
    return base + static_cast<uint32_t>(step);
}

}  // namespace presentation_math
