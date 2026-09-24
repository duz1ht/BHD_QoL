#include "game_mouse_look_predictor.h"

#include <cstdint>

namespace game_mouse_look_predictor {
namespace {
int32_t ArithmeticShiftRight(int32_t value, unsigned bits) {
    const uint32_t shifted = static_cast<uint32_t>(value) >> bits;
    if (value >= 0) return static_cast<int32_t>(shifted);
    return static_cast<int32_t>(shifted | (~uint32_t{0} << (32 - bits)));
}
}  // namespace

int32_t PredictYawControl(int16_t yawInput, int16_t filterState) {
    const int16_t sum = static_cast<int16_t>(
        static_cast<uint16_t>(filterState) + static_cast<uint16_t>(yawInput));
    const int32_t decay = ArithmeticShiftRight(static_cast<int32_t>(sum) + 4, 3);
    const int16_t filtered = static_cast<int16_t>(
        static_cast<uint16_t>(sum) - static_cast<uint16_t>(decay));
    return static_cast<int32_t>(filtered) * 3072;
}

int32_t PredictPitchControl(int16_t pitchInput, bool applyNormalScale) {
    int32_t control = static_cast<int32_t>(
        static_cast<uint32_t>(static_cast<int32_t>(pitchInput)) << 14);
    if (applyNormalScale) {
        const int64_t product = static_cast<int64_t>(control) * 135168;
        control = static_cast<int32_t>(static_cast<uint64_t>(product) >> 16);
    }
    return control;
}

int32_t ControlToStep(int32_t control) {
    const int32_t rounded = static_cast<int32_t>(
        static_cast<uint32_t>(control) + 2u);
    return ArithmeticShiftRight(rounded, 2);
}

}  // namespace game_mouse_look_predictor
