#include "high_rate_camera.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game_mouse_prediction.h"
#include "logger.h"
#include "raw_input.h"

namespace high_rate_camera {
namespace {
constexpr uintptr_t kCallSite = 0x004B7297;
constexpr uintptr_t kCalculateCameraPositions = 0x0043B5E0;
constexpr uintptr_t kLocalPlayer = 0x0096C290;
constexpr uintptr_t kCameraMode = 0x007F2DD0;
constexpr uintptr_t kCameraYaw = 0x007F2DE4;
constexpr uintptr_t kCameraPitch = 0x007F2DE8;
constexpr uintptr_t kMouseDivisor = 0x00A02248;
constexpr uintptr_t kPreviousMouseX = 0x009F3750;
constexpr uintptr_t kPreviousMouseY = 0x009FB280;
constexpr uintptr_t kInvertY = 0x009F20E0;
constexpr uintptr_t kMouseScale = 0x009F20E4;
constexpr uintptr_t kInputBlocked = 0x009F36BC;
constexpr uintptr_t kInputObject = 0x00F65504;
constexpr uintptr_t kInputSystem = 0x00655D7C;
constexpr uintptr_t kMouseEnabled = 0x009F20DC;
constexpr uintptr_t kZoomState = 0x0096C4B0;
constexpr uintptr_t kIsZoomPossible = 0x0052CE00;
constexpr uintptr_t kIsZoomActive = 0x00488D90;
constexpr uintptr_t kZoomDivisor = 0x00488260;
constexpr uintptr_t kApplyRotation = 0x0055FE00;
constexpr uintptr_t kMatrixToPose = 0x0055E450;

using CalculateFn = int(__cdecl*)();
using PredicateFn = int(__cdecl*)();
using ApplyRotationFn = void(__cdecl*)(void*, int32_t, int32_t, int32_t);
using MatrixToPoseFn = void(__cdecl*)(void*, const void*);

struct Matrix { int32_t value[12]; };
struct Pose { int32_t xyz[3]; int32_t yaw; int32_t pitch; int32_t roll; };

bool NormalInputPath() {
    return *reinterpret_cast<volatile int32_t*>(kInputBlocked) == 0 &&
           *reinterpret_cast<void* volatile*>(kInputObject) != nullptr &&
           *reinterpret_cast<void* volatile*>(kInputSystem) != nullptr &&
           *reinterpret_cast<volatile int32_t*>(kMouseEnabled) != 0;
}

int ZoomScaleDivisor() {
    if (reinterpret_cast<PredicateFn>(kIsZoomPossible)() == 0 ||
        reinterpret_cast<PredicateFn>(kIsZoomActive)() == 0 ||
        *reinterpret_cast<volatile unsigned char*>(kZoomState) != 0) return 1;
    const int divisor = reinterpret_cast<PredicateFn>(kZoomDivisor)();
    return divisor > 0 ? divisor : 0;
}

void ApplyPrediction() {
    if (!raw_input::IsEnabled() || !NormalInputPath() ||
        *reinterpret_cast<volatile int32_t*>(kCameraMode) != 0) return;
    auto* player = *reinterpret_cast<unsigned char* volatile*>(kLocalPlayer);
    if (player == nullptr) return;
    const raw_input::PredictionSnapshot raw = raw_input::GetPredictionSnapshot();
    const int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(raw.totalX) -
                                            static_cast<uint32_t>(raw.committedX));
    const int32_t dy = static_cast<int32_t>(static_cast<uint32_t>(raw.totalY) -
                                            static_cast<uint32_t>(raw.committedY));
    if ((dx | dy) == 0) return;
    const int divisor = *reinterpret_cast<volatile int32_t*>(kMouseDivisor);
    const int zoom = ZoomScaleDivisor();
    if (divisor <= 0 || zoom <= 0) return;
    const game_mouse_prediction::State state = {
        divisor, *reinterpret_cast<volatile int32_t*>(kPreviousMouseX),
        *reinterpret_cast<volatile int32_t*>(kPreviousMouseY),
        static_cast<int32_t>(static_cast<uint32_t>(
            *reinterpret_cast<volatile int32_t*>(kMouseScale)) << 11),
        *reinterpret_cast<volatile int32_t*>(kInvertY) != 0, zoom,
        *reinterpret_cast<volatile int16_t*>(player + 432)};
    const auto zero = game_mouse_prediction::Simulate(state, 0, 0);
    const auto moved = game_mouse_prediction::Simulate(state, dx, dy);
    const int32_t yawStep = (moved.yawControl - zero.yawControl + 2) >> 2;
    const int32_t pitchStep = (moved.pitchControl - zero.pitchControl + 2) >> 2;
    if ((yawStep | pitchStep) == 0) return;
    Matrix matrix;
    Pose pose;
    std::memcpy(&matrix, player + 0x88, sizeof(matrix));
    std::memcpy(&pose, player + 0x08, sizeof(pose));
    reinterpret_cast<ApplyRotationFn>(kApplyRotation)(&matrix, yawStep, pitchStep, 0);
    reinterpret_cast<MatrixToPoseFn>(kMatrixToPose)(&pose, &matrix);
    *reinterpret_cast<volatile int32_t*>(kCameraYaw) = pose.yaw;
    *reinterpret_cast<volatile int32_t*>(kCameraPitch) = pose.pitch;
}

extern "C" int __cdecl CalculateCameraPositionsHook() {
    const int result = reinterpret_cast<CalculateFn>(kCalculateCameraPositions)();
    ApplyPrediction();
    return result;
}
}

bool Install(bool enabled, bool rawInputAvailable) {
    if (!enabled) { logger::Log("INFO", "HighRateCamera", "feature disabled"); return true; }
    if (!rawInputAvailable) {
        logger::Log("WARN", "HighRateCamera", "disabled because Raw Input is unavailable");
        return false;
    }
    const unsigned char expected[] = {0xE8, 0x44, 0x43, 0xF8, 0xFF};
    auto* call = reinterpret_cast<unsigned char*>(kCallSite);
    if (std::memcmp(call, expected, sizeof(expected)) != 0) {
        logger::Log("ERROR", "HighRateCamera", "unexpected render call bytes at 0x%08lX",
                    static_cast<unsigned long>(kCallSite));
        return false;
    }
    unsigned char patch[5] = {0xE8, 0, 0, 0, 0};
    *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&CalculateCameraPositionsHook) - (kCallSite + 5));
    DWORD oldProtect = 0;
    if (!VirtualProtect(call, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    std::memcpy(call, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), call, sizeof(patch));
    DWORD ignored = 0;
    VirtualProtect(call, sizeof(patch), oldProtect, &ignored);
    logger::Log("INFO", "HighRateCamera", "render-camera prediction hook installed");
    return true;
}
}  // namespace high_rate_camera
