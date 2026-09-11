#include "camera_fov_math.h"

#include <cassert>
#include <cmath>

namespace {
double Degrees(int32_t q16) { return static_cast<double>(q16) / 65536.0; }

void ExpectNear(double actual, double expected, double tolerance) {
    assert(std::fabs(actual - expected) <= tolerance);
}
}  // namespace

int main() {
    using camera_fov::CorrectHorizontalFovQ16;
    using camera_fov::kVanillaFovQ16;

    assert(CorrectHorizontalFovQ16(0, 1080) == kVanillaFovQ16);
    assert(CorrectHorizontalFovQ16(1920, 0) == kVanillaFovQ16);
    assert(CorrectHorizontalFovQ16(-1, 1080) == kVanillaFovQ16);
    assert(CorrectHorizontalFovQ16(1024, 768) == kVanillaFovQ16);
    assert(CorrectHorizontalFovQ16(1280, 1024) == kVanillaFovQ16);
    assert(CorrectHorizontalFovQ16(9000, 1000) == kVanillaFovQ16);
    ExpectNear(Degrees(CorrectHorizontalFovQ16(1680, 1050)), 90.39, 0.02);
    ExpectNear(Degrees(CorrectHorizontalFovQ16(1920, 1080)), 96.42, 0.02);
    ExpectNear(Degrees(CorrectHorizontalFovQ16(2560, 1080)), 112.33, 0.02);
    return 0;
}
