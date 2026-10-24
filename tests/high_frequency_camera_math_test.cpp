#include "high_frequency_camera_math.h"

#include <cassert>

int main() {
    using high_frequency_camera::AdvanceYawFilter;
    using high_frequency_camera::PendingPitchOffset;
    using high_frequency_camera::PendingYawOffset;

    assert(AdvanceYawFilter(0, 8) == 7);
    assert(AdvanceYawFilter(80, 0) == 70);
    assert(PendingYawOffset(0, 8) == -7 * 0xC00);
    assert(PendingYawOffset(80, 8) == -(77 - 70) * 0xC00);

    assert(PendingPitchOffset(8, false) == 8 * 0x4000);
    assert(PendingPitchOffset(-8, false) == -8 * 0x4000);
    assert(PendingPitchOffset(8, true) == 8 * 0x8400);
    assert(PendingPitchOffset(-8, true) == -8 * 0x8400);
    return 0;
}
