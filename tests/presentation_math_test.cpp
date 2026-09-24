#include "presentation_math.h"

#include <cassert>
#include <cstdint>

int main() {
    using namespace presentation_math;
    assert(CounterDelta(15, 10) == 5);
    assert(CounterDelta(static_cast<int32_t>(0x80000002u),
                        static_cast<int32_t>(0x7FFFFFFEu)) == 4);
    assert(AngleDelta(2u, 0xFFFFFFFEu) == 4);
    assert(AddPredictedAngle(100u, 3, 2.0) == 106u);
    assert(AddPredictedAngle(2u, -2, 2.0) == 0xFFFFFFFEu);
    return 0;
}
