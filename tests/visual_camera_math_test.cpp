#include <cassert>
#include <cstdint>

#include "visual_camera_math.h"

int main() {
    using namespace visual_camera;

    assert(InterpolationAlpha(0, 100) == 0);
    assert(InterpolationAlpha(50, 100) == 32768);
    assert(InterpolationAlpha(150, 100) == kInterpolationOne);
    assert(InterpolateLinear(100, 200, 32768) == 150);
    assert(InterpolateLinear(200, 100, 32768) == 150);

    assert(InterpolateAngle(0xfffffff0u, 0x00000010u, 32768) == 0u);
    assert(InterpolateAngle(0x00000010u, 0xfffffff0u, 32768) == 0u);
    assert(InterpolateAngle(123u, 456u, kInterpolationOne) == 456u);
    return 0;
}
