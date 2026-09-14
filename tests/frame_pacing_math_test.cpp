#include "frame_pacing_math.h"

#include <cassert>

int main() {
    using frame_pacing::AverageOrZero;
    using frame_pacing::IsFrameStall;
    using frame_pacing::RateMilliHz;

    assert(!IsFrameStall(249999));
    assert(IsFrameStall(250000));
    assert(AverageOrZero(100, 4) == 25);
    assert(AverageOrZero(100, 0) == 0);
    assert(RateMilliHz(320, 5000) == 64000);
    assert(RateMilliHz(6515, 5000) == 1303000);
    assert(RateMilliHz(1, 0) == 0);
    return 0;
}
