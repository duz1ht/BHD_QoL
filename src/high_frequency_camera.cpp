#include "high_frequency_camera.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "high_frequency_camera_math.h"
#include "mouse_scaling_fix.h"
#include "raw_input.h"

namespace high_frequency_camera {
namespace {
constexpr uintptr_t kLocalPlayer = 0x0096C290;
constexpr uintptr_t kCameraTarget = 0x007F2DD4;
constexpr uintptr_t kCameraMode = 0x007F2DD0;
constexpr uintptr_t kMouseEnabled = 0x009F20DC;
constexpr uintptr_t kInvertMouse = 0x009F20E0;
constexpr uintptr_t kMouseSensitivity = 0x009F20E4;
constexpr uintptr_t kSpecialCameraState = 0x0096C4B0;

constexpr uintptr_t kHasScopedView = 0x0052CE00;
constexpr uintptr_t kHasZoomScale = 0x00488D90;
constexpr uintptr_t kGetZoomScaleDivisor = 0x00488260;

constexpr size_t kCameraYawOffset = 0x10;
constexpr size_t kCameraPitchOffset = 0x14;

using QueryFn = int(__cdecl*)();

bool g_enabled = false;
volatile LONG g_cameraPoseValid = 0;
int32_t g_simulationCameraPose[6] = {};
int32_t g_presentationCameraPose[6] = {};

template <typename T>
T Read(uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

int32_t ScaleForCurrentView(int32_t rawDelta, int axis) {
    int32_t scale = static_cast<int32_t>(
        static_cast<uint32_t>(Read<int32_t>(kMouseSensitivity)) << 11);
    const auto hasScopedView = reinterpret_cast<QueryFn>(kHasScopedView);
    const auto hasZoomScale = reinterpret_cast<QueryFn>(kHasZoomScale);
    const auto getZoomScaleDivisor = reinterpret_cast<QueryFn>(kGetZoomScaleDivisor);
    if (hasScopedView() != 0 && hasZoomScale() != 0 &&
        Read<unsigned char>(kSpecialCameraState) == 0) {
        const int divisor = getZoomScaleDivisor();
        if (divisor > 0) {
            scale /= divisor;
        }
    }
    return mouse_scaling_fix::PreviewScale(rawDelta, scale, axis);
}

} // namespace

void SetEnabled(bool enabled) {
    g_enabled = enabled;
    InterlockedExchange(&g_cameraPoseValid, 0);
    std::memset(g_simulationCameraPose, 0, sizeof(g_simulationCameraPose));
    std::memset(g_presentationCameraPose, 0, sizeof(g_presentationCameraPose));
}

bool Apply(void* sourceCamera) {
    if (!g_enabled) {
        return false;
    }
    if (sourceCamera == nullptr) {
        return false;
    }
    if (Read<int32_t>(kCameraMode) != 0) {
        return false;
    }
    if (Read<int32_t>(kMouseEnabled) == 0) {
        return false;
    }

    const uintptr_t player = Read<uintptr_t>(kLocalPlayer);
    if (player == 0) {
        return false;
    }
    if (Read<uintptr_t>(kCameraTarget) != player) {
        return false;
    }

    auto* camera = static_cast<unsigned char*>(sourceCamera);
    for (int index = 0; index < 6; ++index) {
        g_simulationCameraPose[index] = reinterpret_cast<int32_t*>(camera)[index + 1];
        g_presentationCameraPose[index] = g_simulationCameraPose[index];
    }
    InterlockedExchange(&g_cameraPoseValid, 1);

    LONG projectedX = 0;
    LONG projectedY = 0;
    if (!raw_input::GetProjectedNativeMovement(&projectedX, &projectedY)) {
        return false;
    }
    if (projectedX == 0 && projectedY == 0) {
        return false;
    }

    const int16_t yawCommand = static_cast<int16_t>(
        ScaleForCurrentView(projectedX, 0));
    const int32_t nativeY = Read<int32_t>(kInvertMouse) == 0 ? -projectedY : projectedY;
    const int16_t pitchCommand = static_cast<int16_t>(
        ScaleForCurrentView(nativeY, 1));

    const int32_t yawOffset = PendingYawOffset(yawCommand);
    const int32_t requestedPitchOffset = PendingPitchOffset(pitchCommand);

    auto* cameraYaw = reinterpret_cast<int32_t*>(camera + kCameraYawOffset);
    auto* cameraPitch = reinterpret_cast<int32_t*>(camera + kCameraPitchOffset);
    *cameraYaw = static_cast<int32_t>(
        static_cast<uint32_t>(*cameraYaw) + static_cast<uint32_t>(yawOffset));
    const int32_t originalPitch = *cameraPitch;
    *cameraPitch = ClampPitch(static_cast<int64_t>(originalPitch) + requestedPitchOffset);
    for (int index = 0; index < 6; ++index) {
        g_presentationCameraPose[index] = reinterpret_cast<int32_t*>(camera)[index + 1];
    }
    const int32_t pitchOffset = *cameraPitch - originalPitch;

    return yawOffset != 0 || pitchOffset != 0;
}

bool GetPresentationCameraPose(int32_t simulationPose[6], int32_t presentationPose[6]) {
    if (simulationPose == nullptr || presentationPose == nullptr) return false;
    if (InterlockedCompareExchange(&g_cameraPoseValid, 0, 0) == 0) return false;
    std::memcpy(simulationPose, g_simulationCameraPose, sizeof(g_simulationCameraPose));
    std::memcpy(presentationPose, g_presentationCameraPose, sizeof(g_presentationCameraPose));
    return true;
}

} // namespace high_frequency_camera
