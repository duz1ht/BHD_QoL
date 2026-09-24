#include "game_mouse_prediction.h"

#include <cstdint>

namespace game_mouse_prediction {
namespace {
int32_t Scale(int32_t value, int32_t scale) {
    const uint64_t product = static_cast<uint64_t>(static_cast<int64_t>(value) * scale);
    return static_cast<int32_t>(product >> 16);
}
}

Result Simulate(const State& state, int32_t rawX, int32_t rawY) {
    if (state.divisor <= 0 || state.zoomDivisor <= 0) return {};
    const int32_t logicX = static_cast<int32_t>(static_cast<uint32_t>(rawX) * 4u) /
                               state.divisor + state.previousX;
    const int32_t logicY = static_cast<int32_t>(static_cast<uint32_t>(rawY) * 4u) /
                               state.divisor + state.previousY;
    const int32_t effectiveScale = state.scale / state.zoomDivisor;
    const int32_t yawInput = Scale(logicX, effectiveScale);
    const int32_t pitchInput = Scale(state.invertY ? logicY : -logicY, effectiveScale);
    const int16_t yawSum = static_cast<int16_t>(yawInput + state.yawFilter);
    const int16_t filtered = static_cast<int16_t>(
        static_cast<int32_t>(yawSum) - ((static_cast<int32_t>(yawSum) + 4) >> 3));
    const int16_t pitch = static_cast<int16_t>(pitchInput);
    const int32_t pitchBase = static_cast<int32_t>(
        static_cast<uint32_t>(static_cast<int32_t>(pitch)) << 14);
    return {3072 * static_cast<int32_t>(filtered),
            static_cast<int32_t>((135168LL * pitchBase) >> 16)};
}

}  // namespace game_mouse_prediction
