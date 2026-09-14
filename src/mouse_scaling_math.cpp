#include "mouse_scaling_math.h"

namespace mouse_scaling_fix {

int32_t ScaleQ16Original(int32_t delta, int32_t scale) {
    const uint64_t product =
        static_cast<uint64_t>(static_cast<int64_t>(delta) * scale);
    return static_cast<int32_t>(product >> 16);
}

int32_t FractionalScaler::Scale(int32_t delta, int32_t scale, int axis) {
    if ((scale & 0xFFFF) == 0) return ScaleQ16Original(delta, scale);

    AxisState& state = axes_[axis == 0 ? 0 : 1];
    if (!state.active || state.scale != scale) {
        state.scale = scale;
        state.remainder = 0;
        state.active = true;
    }
    const int64_t value = static_cast<int64_t>(delta) * scale + state.remainder;
    const int32_t output = static_cast<int32_t>(value / 65536);
    state.remainder = value - static_cast<int64_t>(output) * 65536;
    return output;
}

void FractionalScaler::Reset() { axes_[0] = {}; axes_[1] = {}; }

}  // namespace mouse_scaling_fix
