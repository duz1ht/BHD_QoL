#include "visual_camera_math.h"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace visual_camera_math;

int main() {
    assert(Alpha250(0) == 0);
    assert(Alpha250(32) == 32768);
    assert(Alpha250(64) == kAlphaOne);
    assert(Alpha250(100) == kAlphaOne);

    assert(Alpha62(0, 1) == 0);
    assert(Alpha62(0, 2) == 16384);
    assert(Alpha62(0, 3) == 32768);
    assert(Alpha62(0, 0) == 49152);

    assert(LerpQ16(-65536, 65536, 32768) == 0);
    assert(LerpQ16(10, 20, 0) == 10);
    assert(LerpQ16(10, 20, kAlphaOne) == 20);

    assert(LerpAngle(0xfffffff0u, 0x10u, 32768) == 0u);
    assert(LerpAngle(0x10u, 0xfffffff0u, 32768) == 0u);
    assert(LerpAngle(123u, 456u, 0) == 123u);
    assert(LerpAngle(123u, 456u, kAlphaOne) == 456u);

    TransformQ16 a = {0, 10, -20, 0xfffffff0u, 0, 100};
    TransformQ16 b = {65536, 20, 20, 0x10u, 200, 300};
    const TransformQ16 middle = Interpolate(a, b, 32768);
    assert(middle.x == 32768 && middle.y == 15 && middle.z == 0);
    assert(middle.yaw == 0 && middle.pitch == 100 && middle.roll == 200);

    assert(!IsTeleport(a, b));
    b.x = 65 * 65536;
    assert(IsTeleport(a, b));
    return 0;
}
