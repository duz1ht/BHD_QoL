#include "high_frequency_camera_math.h"

#include <cassert>

int main() {
    using high_frequency_camera::ClampPitch;
    using high_frequency_camera::PendingPitchOffset;
    using high_frequency_camera::PendingYawOffset;

    assert(PendingYawOffset(8) == -8 * 0x10000);
    assert(PendingYawOffset(-8) == 8 * 0x10000);
    assert(PendingPitchOffset(8) == 8 * 0x10000);
    assert(PendingPitchOffset(-8) == -8 * 0x10000);
    assert(ClampPitch(0) == 0);
    assert(ClampPitch(0x38E38E00LL + 1) == 0x38E38E00);
    assert(ClampPitch(-0x38E38E00LL - 1) == -0x38E38E00);
    return 0;
}
