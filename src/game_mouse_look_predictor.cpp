#include "game_mouse_look_predictor.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "mouse_scaling_fix.h"

namespace game_mouse_look_predictor {
namespace {
constexpr uintptr_t kMouseDivisor = 0x00A02248;
constexpr uintptr_t kPreviousX = 0x009F3750;
constexpr uintptr_t kPreviousY = 0x009FB280;
constexpr uintptr_t kInvertY = 0x009F20E0;
constexpr uintptr_t kMouseScale = 0x009F20E4;
constexpr uintptr_t kSpecialPitchMode = 0x009F2178;
constexpr uintptr_t kBindingCount = 0x00872358;
constexpr uintptr_t kBindingIndices = 0x008727F8;
constexpr uintptr_t kBindingContext = 0x00872398;
constexpr uintptr_t kBindings = 0x00627BF0;
constexpr uintptr_t kKeyState = 0x00F64ECC;
constexpr uintptr_t kMouseState = 0x00F655E8;
constexpr uintptr_t kZoomState = 0x0096C4B0;
constexpr uintptr_t kIsPlayerActive = 0x0052CE00;
constexpr uintptr_t kIsZoomed = 0x00488D90;
constexpr uintptr_t kZoomDivisor = 0x00488260;
constexpr uintptr_t kRotateMatrix = 0x0055FE00;
constexpr uintptr_t kMatrixToPose = 0x0055E450;

constexpr size_t kYawFilterOffset = 432;
constexpr size_t kMatrixOffset = 136;
constexpr size_t kBindingSize = 100;
constexpr uint32_t kXAxisBinding = 0x00100000;
constexpr uint32_t kYAxisBinding = 0x00200000;

using QueryFn = int(__cdecl*)();
using RotateMatrixFn = void(__cdecl*)(void*, int32_t, int32_t, int32_t);
using MatrixToPoseFn = void(__cdecl*)(void*, const void*);

struct Matrix {
    int32_t values[12];
};

struct Pose {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t yaw;
    uint32_t pitch;
    uint32_t roll;
};

struct InputState {
    int32_t divisor;
    int32_t previousX;
    int32_t previousY;
    int32_t scale;
    bool invertY;
    bool applyNormalPitchScale;
    int16_t yawFilter;
    Matrix matrix;
    mouse_scaling_fix::PredictionState scaling;
};

int32_t ReadI32(uintptr_t address) {
    return *reinterpret_cast<volatile int32_t*>(address);
}

int32_t FirstStage(int32_t raw, int32_t divisor, int32_t previous) {
    const int32_t multiplied =
        static_cast<int32_t>(static_cast<uint32_t>(raw) * 4u);
    return static_cast<int32_t>(
        static_cast<uint32_t>(multiplied / divisor) +
        static_cast<uint32_t>(previous));
}

void CaptureLookAction(int action, int32_t value, LookActions* actions) {
    switch (action) {
        case 259: actions->pitch = static_cast<int16_t>(value); break;
        case 260: actions->pitch = static_cast<int16_t>(-value); break;
        case 263: actions->yaw = static_cast<int16_t>(value); break;
        case 264: actions->yaw = static_cast<int16_t>(-value); break;
        default: break;
    }
}

bool BindingIsActive(const unsigned char* binding, uint8_t context) {
    const uint8_t bindingContext = binding[8];
    if (context != 0 && bindingContext != context) return false;
    if ((bindingContext == 8 || bindingContext == 12) &&
        bindingContext != context) return false;

    int16_t key = 0;
    int16_t button = 0;
    std::memcpy(&key, binding + 22, sizeof(key));
    std::memcpy(&button, binding + 24, sizeof(button));
    if (key != 0 && *(reinterpret_cast<volatile uint8_t*>(kKeyState) + key) != 0)
        return true;
    if (button != 0 && (ReadI32(kMouseState) & button) != 0) return true;
    return key == 0 && button == 0;
}

LookActions ResolveLookActions(int32_t x, int32_t y) {
    LookActions actions = {};
    const int32_t count = ReadI32(kBindingCount);
    if (count <= 0 || count > 1024) return actions;
    const auto* indices = reinterpret_cast<volatile int32_t*>(kBindingIndices);
    const uint8_t context = *reinterpret_cast<volatile uint8_t*>(kBindingContext);
    for (int32_t i = 0; i < count; ++i) {
        const int32_t index = indices[i];
        if (index < 0 || index >= 4096) continue;
        const auto* binding = reinterpret_cast<const unsigned char*>(
            kBindings + static_cast<uintptr_t>(index) * kBindingSize);
        if (!BindingIsActive(binding, context)) continue;
        int16_t action = 0;
        uint32_t flags = 0;
        std::memcpy(&action, binding, sizeof(action));
        std::memcpy(&flags, binding + 4, sizeof(flags));
        if ((flags & kYAxisBinding) != 0) {
            CaptureLookAction(action, (flags & kXAxisBinding) != 0 ? x : y,
                              &actions);
        }
        if ((flags & kXAxisBinding) != 0)
            CaptureLookAction(action, x, &actions);
    }
    return actions;
}

int32_t EffectiveScale() {
    const int32_t base = static_cast<int32_t>(
        static_cast<uint32_t>(ReadI32(kMouseScale)) << 11);
    if (reinterpret_cast<QueryFn>(kIsPlayerActive)() == 0 ||
        reinterpret_cast<QueryFn>(kIsZoomed)() == 0 ||
        *reinterpret_cast<volatile uint8_t*>(kZoomState) != 0) {
        return base;
    }
    const int32_t divisor = reinterpret_cast<QueryFn>(kZoomDivisor)();
    return divisor != 0 ? base / divisor : base;
}

BranchResult RunBranch(const InputState& input, int32_t rawX, int32_t rawY) {
    BranchResult result = {};
    result.firstX = FirstStage(rawX, input.divisor, input.previousX);
    result.firstY = FirstStage(rawY, input.divisor, input.previousY);
    if (!input.invertY)
        result.firstY = static_cast<int32_t>(0u -
            static_cast<uint32_t>(result.firstY));

    auto scaling = input.scaling;
    result.scaledX = mouse_scaling_fix::ScaleForPrediction(
        result.firstX, input.scale, 0, &scaling);
    result.scaledY = mouse_scaling_fix::ScaleForPrediction(
        result.firstY, input.scale, 1, &scaling);
    result.actions = ResolveLookActions(result.scaledX, result.scaledY);
    result.yawControl = PredictYawControl(result.actions.yaw, input.yawFilter);
    result.pitchControl =
        PredictPitchControl(result.actions.pitch, input.applyNormalPitchScale);
    result.yawStep = ControlToStep(result.yawControl);
    result.pitchStep = ControlToStep(result.pitchControl);

    Matrix matrix = input.matrix;
    reinterpret_cast<RotateMatrixFn>(kRotateMatrix)(
        &matrix, result.yawStep, result.pitchStep, 0);
    Pose pose = {};
    reinterpret_cast<MatrixToPoseFn>(kMatrixToPose)(&pose, &matrix);
    result.yaw = pose.yaw;
    result.pitch = pose.pitch;
    return result;
}
}  // namespace

bool Predict(void* player, int32_t rawDx, int32_t rawDy, Result* result) {
    if (player == nullptr || result == nullptr) return false;
    const int32_t divisor = ReadI32(kMouseDivisor);
    if (divisor <= 0 || ReadI32(kSpecialPitchMode) != 0) return false;

    InputState input = {};
    input.divisor = divisor;
    input.previousX = ReadI32(kPreviousX);
    input.previousY = ReadI32(kPreviousY);
    input.scale = EffectiveScale();
    input.invertY = ReadI32(kInvertY) != 0;
    input.applyNormalPitchScale = true;
    input.yawFilter = *reinterpret_cast<volatile int16_t*>(
        static_cast<unsigned char*>(player) + kYawFilterOffset);
    std::memcpy(&input.matrix,
                static_cast<unsigned char*>(player) + kMatrixOffset,
                sizeof(input.matrix));
    input.scaling = mouse_scaling_fix::GetPredictionState();

    result->zero = RunBranch(input, 0, 0);
    result->moved = RunBranch(input, rawDx, rawDy);
    result->yawDelta = static_cast<int32_t>(result->moved.yaw - result->zero.yaw);
    result->pitchDelta =
        static_cast<int32_t>(result->moved.pitch - result->zero.pitch);
    return true;
}

}  // namespace game_mouse_look_predictor
