#pragma once

#include <cstdint>

namespace mouse_scaling_fix {

// Stateful Q16 scaling. Keeping the fractional remainder makes a sequence of
// small movements equivalent to scaling their sum instead of discarding the
// fraction from every individual input report.
class FractionalScaler {
public:
    int32_t Scale(int32_t delta, int32_t scale, int axis);
    void Reset();

private:
    struct AxisState {
        int32_t scale = 0;
        int64_t remainder = 0;
        bool active = false;
    };
    AxisState axes_[2] = {};
};

int32_t ScaleQ16Original(int32_t delta, int32_t scale);

}  // namespace mouse_scaling_fix
