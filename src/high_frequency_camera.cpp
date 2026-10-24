#include "high_frequency_camera.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "logger.h"
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
bool g_loggedActive = false;
volatile LONG g_presentationYawOffset = 0;
volatile LONG g_presentationPitchOffset = 0;
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
    InterlockedExchange(&g_presentationYawOffset, 0);
    InterlockedExchange(&g_presentationPitchOffset, 0);
    InterlockedExchange(&g_cameraPoseValid, 0);
    std::memset(g_simulationCameraPose, 0, sizeof(g_simulationCameraPose));
    std::memset(g_presentationCameraPose, 0, sizeof(g_presentationCameraPose));
    logger::Log("INFO", "HighFrequencyCamera", "feature %s",
                enabled ? "enabled" : "disabled");
}

bool Apply(void* sourceCamera, ApplyResult* result) {
    InterlockedExchange(&g_presentationYawOffset, 0);
    InterlockedExchange(&g_presentationPitchOffset, 0);
    ApplyResult localResult = {};
    ApplyResult& trace = result == nullptr ? localResult : *result;
    trace = {};
    if (!g_enabled) {
        trace.code = ApplyResultCode::kDisabled;
        return false;
    }
    if (sourceCamera == nullptr) {
        trace.code = ApplyResultCode::kNullSource;
        return false;
    }
    trace.cameraMode = Read<int32_t>(kCameraMode);
    if (trace.cameraMode != 0) {
        trace.code = ApplyResultCode::kWrongCameraMode;
        return false;
    }
    if (Read<int32_t>(kMouseEnabled) == 0) {
        trace.code = ApplyResultCode::kMouseDisabled;
        return false;
    }

    const uintptr_t player = Read<uintptr_t>(kLocalPlayer);
    trace.localPlayer = player;
    trace.cameraTarget = Read<uintptr_t>(kCameraTarget);
    if (player == 0) {
        trace.code = ApplyResultCode::kNoLocalPlayer;
        return false;
    }
    if (trace.cameraTarget != player) {
        trace.code = ApplyResultCode::kWrongCameraTarget;
        return false;
    }

    auto* camera = static_cast<unsigned char*>(sourceCamera);
    for (int index = 0; index < 6; ++index) {
        g_simulationCameraPose[index] = reinterpret_cast<int32_t*>(camera)[index + 1];
        g_presentationCameraPose[index] = g_simulationCameraPose[index];
    }
    InterlockedExchange(&g_cameraPoseValid, 1);

    LONG pendingRawX = 0;
    LONG pendingRawY = 0;
    if (!raw_input::GetPendingMovement(&pendingRawX, &pendingRawY)) {
        trace.code = ApplyResultCode::kRawInputUnavailable;
        return false;
    }
    trace.pendingX = pendingRawX;
    trace.pendingY = pendingRawY;

    LONG projectedX = 0;
    LONG projectedY = 0;
    uint32_t projectionPhaseQ16 = 0;
    if (!raw_input::GetProjectedNativeMovement(
            &projectedX, &projectedY, &projectionPhaseQ16)) {
        trace.code = ApplyResultCode::kRawInputUnavailable;
        return false;
    }
    trace.projectedX = projectedX;
    trace.projectedY = projectedY;
    trace.projectionPhaseQ16 = projectionPhaseQ16;
    if (projectedX == 0 && projectedY == 0) {
        trace.code = ApplyResultCode::kNoPendingMovement;
        return false;
    }

    const int16_t yawCommand = static_cast<int16_t>(
        ScaleForCurrentView(projectedX, 0));
    const int32_t nativeY = Read<int32_t>(kInvertMouse) == 0 ? -projectedY : projectedY;
    const int16_t pitchCommand = static_cast<int16_t>(
        ScaleForCurrentView(nativeY, 1));

    const int32_t yawOffset = PendingYawOffset(yawCommand);
    const int32_t requestedPitchOffset = PendingPitchOffset(pitchCommand);
    trace.yawCommand = yawCommand;
    trace.pitchCommand = pitchCommand;
    trace.yawOffset = yawOffset;

    auto* cameraYaw = reinterpret_cast<int32_t*>(camera + kCameraYawOffset);
    auto* cameraPitch = reinterpret_cast<int32_t*>(camera + kCameraPitchOffset);
    *cameraYaw = static_cast<int32_t>(
        static_cast<uint32_t>(*cameraYaw) + static_cast<uint32_t>(yawOffset));
    const int32_t originalPitch = *cameraPitch;
    *cameraPitch = ClampPitch(static_cast<int64_t>(originalPitch) + requestedPitchOffset);
    trace.pitchOffset = *cameraPitch - originalPitch;
    for (int index = 0; index < 6; ++index) {
        g_presentationCameraPose[index] = reinterpret_cast<int32_t*>(camera)[index + 1];
    }
    InterlockedExchange(&g_presentationYawOffset, yawOffset);
    InterlockedExchange(&g_presentationPitchOffset, trace.pitchOffset);

    trace.code = yawOffset != 0 || trace.pitchOffset != 0
                     ? ApplyResultCode::kApplied
                     : ApplyResultCode::kZeroOffset;

    if (!g_loggedActive) {
        g_loggedActive = true;
        logger::Log("INFO", "HighFrequencyCamera",
                    "first pending movement applied dx=%ld dy=%ld yaw_offset=%ld pitch_offset=%ld",
                    pendingRawX, pendingRawY, static_cast<long>(yawOffset),
                    static_cast<long>(trace.pitchOffset));
    }
    return trace.code == ApplyResultCode::kApplied;
}

bool GetPresentationOffset(int32_t* yawOffset, int32_t* pitchOffset) {
    if (yawOffset == nullptr || pitchOffset == nullptr) return false;
    *yawOffset = InterlockedCompareExchange(&g_presentationYawOffset, 0, 0);
    *pitchOffset = InterlockedCompareExchange(&g_presentationPitchOffset, 0, 0);
    return *yawOffset != 0 || *pitchOffset != 0;
}

bool GetPresentationCameraPose(int32_t simulationPose[6], int32_t presentationPose[6]) {
    if (simulationPose == nullptr || presentationPose == nullptr) return false;
    if (InterlockedCompareExchange(&g_cameraPoseValid, 0, 0) == 0) return false;
    std::memcpy(simulationPose, g_simulationCameraPose, sizeof(g_simulationCameraPose));
    std::memcpy(presentationPose, g_presentationCameraPose, sizeof(g_presentationCameraPose));
    return true;
}

} // namespace high_frequency_camera
