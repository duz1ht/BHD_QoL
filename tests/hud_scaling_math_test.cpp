#include "hud_scaling_math.h"

#include <cassert>
#include <cmath>

int main() {
    using namespace hud_scaling;
    assert(std::fabs(CalculateScale(1920, 1080, 1.0f) - 1.40625f) < 0.0001f);
    assert(std::fabs(CalculateScale(2560, 1440, 1.0f) - 1.875f) < 0.0001f);
    assert(CalculateScale(0, 1080, 1.0f) == 1.0f);

    const float scale = CalculateScale(1920, 1080, 1.0f);
    assert(ScaleCoordinate(0, 1024, 1920, scale) == 0);
    assert(ScaleCoordinate(1024, 1024, 1920, scale) == 1920);
    assert(ScaleCoordinate(512, 1024, 1920, scale) == 960);
    assert(ScaleCoordinate(768, 768, 1080, scale) == 1080);
    assert(ScaleCoordinate(384, 768, 1080, scale) == 540);
    return 0;
}
