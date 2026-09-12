#include "hud_scaling_math.h"
#include <cassert>

int main() {
    using namespace hud_scaling;
    const float scale = 1.0f;
    assert(ScalePosition(20, 1024, 1024, scale, HorizontalAnchor::Left) == 20);
    // HUDWPNICON uses a left anchor. Treating its small X coordinate as a
    // right-anchored margin would incorrectly move it toward the screen center.
    assert(ScalePosition(64, 1024, 1024, scale, HorizontalAnchor::Left) == 64);
    assert(ScalePosition(1004, 1024, 1024, scale, HorizontalAnchor::Right) == 1004);
    assert(ScalePosition(512, 1024, 1024, scale, HorizontalAnchor::Center) == 512);
    assert(ScaleExtent(32, scale) == 32);

    const float halfScale = 0.5f;
    assert(ScalePosition(64, 1024, 1024, halfScale, HorizontalAnchor::Left) == 32);
    assert(ScalePosition(1004, 1024, 1024, halfScale, HorizontalAnchor::Right) == 1014);
    assert(ScaleExtent(32, halfScale) == 16);
    return 0;
}
