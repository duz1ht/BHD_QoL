#include "camera_fov_math.h"

#include <cmath>
#include <limits>

namespace camera_fov {
namespace {
constexpr double kReferenceAspect = 4.0 / 3.0;
constexpr double kMaximumAspect = 8.0;
constexpr double kReferenceHorizontalDegrees = 80.0;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;
constexpr double kQ16Scale = 65536.0;
}  // namespace

int32_t CorrectHorizontalFovQ16(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return kVanillaFovQ16;

    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    if (!std::isfinite(aspect) || aspect <= kReferenceAspect || aspect > kMaximumAspect) {
        return kVanillaFovQ16;
    }

    const double referenceHorizontal = kReferenceHorizontalDegrees * kDegreesToRadians;
    const double referenceVertical =
        2.0 * std::atan(std::tan(referenceHorizontal / 2.0) / kReferenceAspect);
    const double correctedHorizontal =
        2.0 * std::atan(std::tan(referenceVertical / 2.0) * aspect);
    const double q16 = correctedHorizontal * kRadiansToDegrees * kQ16Scale;
    if (!std::isfinite(q16) || q16 < 0.0 ||
        q16 > static_cast<double>(std::numeric_limits<int32_t>::max())) {
        return kVanillaFovQ16;
    }
    return static_cast<int32_t>(std::lround(q16));
}

}  // namespace camera_fov
