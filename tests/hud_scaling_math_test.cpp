#include "hud_scaling_math.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace hud_scaling;
    const float scale = CalculateScale(1920, 1080, 1.0f);
    assert(std::fabs(scale - 1.40625f) < 0.0001f);
    assert(ScalePosition(20, 1024, 1920, scale, HorizontalAnchor::Left) == 28);
    // HUDWPNICON uses a left anchor. Treating its small X coordinate as a
    // right-anchored margin would incorrectly move it toward the screen center.
    assert(ScalePosition(64, 1024, 1920, scale, HorizontalAnchor::Left) == 90);
    assert(ScalePosition(64, 1024, 1920, scale, HorizontalAnchor::Right) == 570);
    assert(ScalePosition(1004, 1024, 1920, scale, HorizontalAnchor::Right) == 1892);
    assert(ScalePosition(512, 1024, 1920, scale, HorizontalAnchor::Center) == 960);
    assert(ScaleExtent(32, scale) == 45);
    assert(CalculateScale(0, 1080, 2.0f) == 1.0f);
    return 0;
}
