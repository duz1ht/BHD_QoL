#include "mouse_scaling_math.h"

#include <cassert>
#include <cstdint>

int main() {
    using mouse_scaling_fix::FractionalScaler;
    FractionalScaler scaler;
    constexpr int32_t half = 0x8000;

    assert(scaler.Scale(1, half, 0) == 0);
    assert(scaler.Scale(1, half, 0) == 1);
    assert(scaler.Scale(-1, half, 1) == 0);
    assert(scaler.Scale(-1, half, 1) == -1);

    scaler.Reset();
    int sum = 0;
    for (int i = 0; i < 101; ++i) sum += scaler.Scale(1, 0x4000, 0);
    assert(sum == 25);

    // Axis remainders and scale transitions are independent.
    scaler.Reset();
    assert(scaler.Scale(1, half, 0) == 0);
    assert(scaler.Scale(1, half, 1) == 0);
    assert(scaler.Scale(1, half, 0) == 1);
    assert(scaler.Scale(1, 0x4000, 0) == 0);
    assert(scaler.Scale(1, half, 1) == 1);

    // Integral Q16 scales remain exactly compatible with the original path.
    assert(scaler.Scale(-3, 0x20000, 0) == -6);
    assert(scaler.Scale(3, 0x20000, 1) == 6);
    return 0;
}
