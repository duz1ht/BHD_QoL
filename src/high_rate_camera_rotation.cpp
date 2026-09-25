#include "high_rate_camera_rotation.h"

#include <windows.h>

#include <cstdint>

#include "logger.h"
#include "mouse_scaling_fix.h"
#include "raw_input.h"
#include "visual_camera_math.h"
#include "visual_diagnostics.h"

namespace high_rate_camera_rotation {
namespace {
static_assert(sizeof(void*) == 4, "HighRateCameraRotation requires a 32-bit build");
constexpr uintptr_t kCameraMode = 0x007F2DD0;
constexpr uintptr_t kCameraOwner = 0x007F2DD4;
constexpr uintptr_t kCameraYaw = 0x007F2DE4;
constexpr uintptr_t kCameraPitch = 0x007F2DE8;
constexpr uintptr_t kLocalPlayer = 0x0096C290;
constexpr uintptr_t kInputDivisor = 0x00A02248;
constexpr uintptr_t kPreviousStageX = 0x009F3750;
constexpr uintptr_t kPreviousStageY = 0x009FB280;
constexpr uintptr_t kInvertY = 0x009F20E0;
constexpr uintptr_t kMouseSensitivity = 0x009F20E4;
constexpr uintptr_t kBindingsCount = 0x00872358;
constexpr uintptr_t kBindings = 0x008727F8;
constexpr uintptr_t kBindingDefinitions = 0x00627BF0;
constexpr uintptr_t kMouseEnabled = 0x009F20DC;
constexpr uintptr_t kInputObject = 0x00F65504;
constexpr uintptr_t kInputReady = 0x00655D7C;
constexpr uintptr_t kPauseState = 0x007C72BC;
constexpr uintptr_t kHasZoomCapableWeapon = 0x0052CE00;
constexpr uintptr_t kIsZoomed = 0x00488D90;
constexpr uintptr_t kZoomDivisor = 0x00488260;
constexpr uintptr_t kZoomTransition = 0x0096C4B0;
constexpr size_t kActorRideTargetOffset = 0x13C;
constexpr size_t kBindingDefinitionStride = 100;
constexpr uint32_t kMouseXAxisFlag = 0x00100000;
constexpr uint32_t kMouseYAxisFlag = 0x00200000;
constexpr int16_t kPitchPositive = 259;
constexpr int16_t kPitchNegative = 260;
constexpr int16_t kYawPositive = 263;
constexpr int16_t kYawNegative = 264;
using QueryFn = int(__cdecl*)();

bool g_enabled = false;
bool g_mouseScalingFixEnabled = false;
bool g_valid = false;
uintptr_t g_owner = 0;
uint32_t g_visualYaw = 0;
uint32_t g_visualPitch = 0;
uint32_t g_seenRawX = 0;
uint32_t g_seenRawY = 0;
uint32_t g_seenCommittedX = 0;
uint32_t g_seenCommittedY = 0;
int32_t g_visualPreviousX = 0;
int32_t g_visualPreviousY = 0;
uint32_t g_frameSerial = 0;
uint32_t g_cameraExecutions = 0;
uint32_t g_visualYawChanges = 0;
uint32_t g_officialYawChanges = 0;
uint32_t g_newRawX = 0;
uint32_t g_newRawY = 0;
uint32_t g_newCommittedX = 0;
uint32_t g_newCommittedY = 0;
uint32_t g_lastOfficialYaw = 0;
mouse_scaling_fix::FractionState g_scalingState;
DWORD g_lastStatisticsTick = 0;
DWORD g_statisticsIntervalMs = 5000;
DWORD g_lastSupportedTick = 0;

template <typename T>
T Read(uintptr_t address) {
    return *reinterpret_cast<volatile const T*>(address);
}

bool IsSupported(uintptr_t* owner) {
    const bool rawActive = raw_input::IsActive();
    const LONG cameraMode = Read<LONG>(kCameraMode);
    const LONG pauseState = Read<LONG>(kPauseState);
    const LONG mouseEnabled = Read<LONG>(kMouseEnabled);
    const uintptr_t inputObject = Read<uintptr_t>(kInputObject);
    const uintptr_t inputReady = Read<uintptr_t>(kInputReady);
    const uintptr_t local = Read<uintptr_t>(kLocalPlayer);
    const uintptr_t cameraOwner = Read<uintptr_t>(kCameraOwner);
    const uintptr_t rideTarget = local == 0 ? 0 : Read<uintptr_t>(local + kActorRideTargetOffset);
    auto reason = visual_diagnostics::CameraSupportReason::Supported;
    if (!rawActive) reason = visual_diagnostics::CameraSupportReason::RawInputInactive;
    else if (cameraMode != 0) reason = visual_diagnostics::CameraSupportReason::CameraMode;
    else if (pauseState != 0) reason = visual_diagnostics::CameraSupportReason::Paused;
    else if (mouseEnabled == 0) reason = visual_diagnostics::CameraSupportReason::MouseDisabled;
    else if (inputObject == 0) reason = visual_diagnostics::CameraSupportReason::InputObjectMissing;
    else if (inputReady == 0) reason = visual_diagnostics::CameraSupportReason::InputReadyMissing;
    else if (local == 0) reason = visual_diagnostics::CameraSupportReason::LocalPlayerMissing;
    else if (cameraOwner != local) reason = visual_diagnostics::CameraSupportReason::CameraOwnerMismatch;
    else if (rideTarget != 0) reason = visual_diagnostics::CameraSupportReason::MountedOrVehicle;
    visual_diagnostics::SetCameraSupport(reason, cameraMode, pauseState, mouseEnabled,
                                         inputObject, inputReady, local, cameraOwner, rideTarget);
    if (reason != visual_diagnostics::CameraSupportReason::Supported) return false;
    // A non-null ride target covers the known vehicle/turret ownership cases.
    *owner = local;
    return true;
}

int32_t CurrentScale() {
    int32_t scale = static_cast<int32_t>(
        static_cast<uint32_t>(Read<int32_t>(kMouseSensitivity)) << 11);
    if (reinterpret_cast<QueryFn>(kHasZoomCapableWeapon)() != 0 &&
        reinterpret_cast<QueryFn>(kIsZoomed)() != 0 && Read<uint8_t>(kZoomTransition) == 0) {
        const int32_t divisor = reinterpret_cast<QueryFn>(kZoomDivisor)();
        if (divisor > 0) scale /= divisor;
    }
    return scale;
}

struct LookActions {
    int32_t yaw = 0;
    int32_t pitch = 0;
    int32_t bindingMatches = 0;
};

void AddAction(LookActions* actions, int16_t action, int32_t value) {
    switch (action) {
        case kYawPositive: actions->yaw += value; break;
        case kYawNegative: actions->yaw -= value; break;
        case kPitchPositive: actions->pitch += value; break;
        case kPitchNegative: actions->pitch -= value; break;
        default: break;
    }
}

bool IsLookAction(int16_t action) {
    return action == kPitchPositive || action == kPitchNegative ||
           action == kYawPositive || action == kYawNegative;
}

LookActions ResolveBindings(int32_t mouseX, int32_t mouseY) {
    LookActions result;
    const int32_t count = Read<int32_t>(kBindingsCount);
    if (count <= 0 || count > 512) return result;
    for (int32_t index = 0; index < count; ++index) {
        const int32_t definitionIndex = Read<int32_t>(kBindings + index * sizeof(int32_t));
        if (definitionIndex < 0 || definitionIndex > 4096) continue;
        const uintptr_t definition = kBindingDefinitions +
                                     definitionIndex * kBindingDefinitionStride;
        const int16_t action = Read<int16_t>(definition);
        const uint32_t flags = Read<uint32_t>(definition + 4);
        if ((flags & kMouseXAxisFlag) != 0) {
            AddAction(&result, action, mouseX);
            if (IsLookAction(action)) ++result.bindingMatches;
        }
        if ((flags & kMouseYAxisFlag) != 0) {
            AddAction(&result, action, mouseY);
            if (IsLookAction(action)) ++result.bindingMatches;
        }
    }
    return result;
}

void Rebase(uintptr_t owner, const raw_input::PredictionSnapshot& snapshot) {
    g_owner = owner;
    g_visualYaw = Read<uint32_t>(kCameraYaw);
    g_visualPitch = Read<uint32_t>(kCameraPitch);
    g_seenRawX = snapshot.totalX;
    g_seenRawY = snapshot.totalY;
    g_seenCommittedX = snapshot.committedX;
    g_seenCommittedY = snapshot.committedY;
    // Preserve the still-intentionally-supported first temporal input stage.
    g_visualPreviousX = Read<int32_t>(kPreviousStageX);
    g_visualPreviousY = Read<int32_t>(kPreviousStageY);
    g_lastOfficialYaw = g_visualYaw;
    g_scalingState = {};
    g_valid = true;
    g_lastSupportedTick = GetTickCount();
}

void LogStatistics(const raw_input::PredictionSnapshot& snapshot) {
    const DWORD now = GetTickCount();
    if (now - g_lastStatisticsTick < g_statisticsIntervalMs) return;
    logger::Log("INFO", "HighRateCameraRotation.Stats",
                "interval_ms=%lu camera_frames=%lu visual_yaw_changes=%lu official_yaw_changes=%lu "
                "new_visual_raw_x=%ld new_visual_raw_y=%ld committed_x=%ld committed_y=%ld "
                "pending_x=%ld pending_y=%ld reconciliation_correction=0 first_stage_previous_x=%ld "
                "first_stage_previous_y=%ld",
                g_statisticsIntervalMs, g_cameraExecutions, g_visualYawChanges,
                g_officialYawChanges, static_cast<LONG>(g_newRawX), static_cast<LONG>(g_newRawY),
                static_cast<LONG>(g_newCommittedX), static_cast<LONG>(g_newCommittedY),
                static_cast<LONG>(snapshot.totalX - snapshot.committedX),
                static_cast<LONG>(snapshot.totalY - snapshot.committedY),
                g_visualPreviousX, g_visualPreviousY);
    g_cameraExecutions = g_visualYawChanges = g_officialYawChanges = 0;
    g_newRawX = g_newRawY = g_newCommittedX = g_newCommittedY = 0;
    g_lastStatisticsTick = now;
}
} // namespace

bool Install(bool enabled, bool freeRateMousePollAvailable, bool mouseScalingFixEnabled,
             unsigned long statisticsIntervalMs) {
    g_enabled = enabled && freeRateMousePollAvailable;
    g_mouseScalingFixEnabled = mouseScalingFixEnabled;
    g_statisticsIntervalMs = statisticsIntervalMs;
    g_lastStatisticsTick = GetTickCount();
    logger::Log(g_enabled ? "INFO" : "WARN", "HighRateCameraRotation",
                g_enabled ? "render-only unfiltered visual orientation enabled"
                          : "disabled because configuration or FreeRateMousePoll is unavailable");
    return g_enabled;
}

void EvaluateAndApplyForCurrentFrame() {
    ++g_frameSerial;
    visual_diagnostics::RecordCameraEvaluation();
    if (!g_enabled) {
        visual_diagnostics::SetCameraSupport(
            visual_diagnostics::CameraSupportReason::FeatureDisabled, 0, 0, 0,
            0, 0, 0, 0, 0);
        Invalidate();
        return;
    }
    uintptr_t owner = 0;
    if (!IsSupported(&owner)) {
        // This call site can run for auxiliary/non-local camera calculations
        // between two local first-person calls. Do not destroy persistent
        // visual orientation for those transient calls. A sustained unsupported
        // state still rebases safely when first person returns.
        if (g_valid && GetTickCount() - g_lastSupportedTick > 250) Invalidate();
        return;
    }
    g_lastSupportedTick = GetTickCount();
    const raw_input::PredictionSnapshot snapshot = raw_input::GetPredictionSnapshot();
    if (!g_valid || owner != g_owner) { Rebase(owner, snapshot); return; }

    const int32_t newX = visual_camera_math::NewCumulativeCounts(snapshot.totalX, g_seenRawX);
    const int32_t newY = visual_camera_math::NewCumulativeCounts(snapshot.totalY, g_seenRawY);
    g_newRawX += static_cast<uint32_t>(newX);
    g_newRawY += static_cast<uint32_t>(newY);
    g_newCommittedX += snapshot.committedX - g_seenCommittedX;
    g_newCommittedY += snapshot.committedY - g_seenCommittedY;
    g_seenRawX = snapshot.totalX;
    g_seenRawY = snapshot.totalY;
    g_seenCommittedX = snapshot.committedX;
    g_seenCommittedY = snapshot.committedY;

    const int32_t divisor = Read<int32_t>(kInputDivisor);
    if (divisor <= 0) { Invalidate(); return; }
    const int32_t currentX = static_cast<int32_t>((static_cast<int64_t>(newX) * 4) / divisor);
    const int32_t currentY = static_cast<int32_t>((static_cast<int64_t>(newY) * 4) / divisor);
    int32_t stageX = currentX + g_visualPreviousX;
    int32_t stageY = currentY + g_visualPreviousY;
    g_visualPreviousX = currentX;
    g_visualPreviousY = currentY;
    if (Read<int32_t>(kInvertY) == 0) stageY = -stageY;
    const int32_t scale = CurrentScale();
    const int32_t baseScale = static_cast<int32_t>(
        static_cast<uint32_t>(Read<int32_t>(kMouseSensitivity)) << 11);
    const int32_t predictorBaseScale = g_mouseScalingFixEnabled ? baseScale : scale;
    const int32_t scaledX = mouse_scaling_fix::EvaluateWithState(
        stageX, scale, predictorBaseScale, 0, &g_scalingState);
    const int32_t scaledY = mouse_scaling_fix::EvaluateWithState(
        stageY, scale, predictorBaseScale, 1, &g_scalingState);
    const LookActions actions = ResolveBindings(scaledX, scaledY);

    const uint32_t oldYaw = g_visualYaw;
    // Direct control intentionally has no sub_438150 persistent yaw-filter state.
    g_visualYaw += static_cast<uint32_t>(visual_camera_math::UnfilteredYawStep(actions.yaw));
    g_visualPitch += static_cast<uint32_t>(static_cast<int64_t>(actions.pitch) * 135168 * 16384 /
                                          65536 / 4);
    int64_t signedPitch = static_cast<int32_t>(g_visualPitch);
    if (signedPitch > 0x18e38e20LL) signedPitch = 0x18e38e20LL;
    if (signedPitch < -0x0eeeeee0LL) signedPitch = -0x0eeeeee0LL;
    g_visualPitch = static_cast<uint32_t>(signedPitch);
    *reinterpret_cast<volatile uint32_t*>(kCameraYaw) = g_visualYaw;
    *reinterpret_cast<volatile uint32_t*>(kCameraPitch) = g_visualPitch;
    ++g_cameraExecutions;
    if (g_visualYaw != oldYaw) ++g_visualYawChanges;
    const uint32_t officialYaw = Read<uint32_t>(owner + 20);
    const bool officialYawChanged = officialYaw != g_lastOfficialYaw;
    if (officialYawChanged) ++g_officialYawChanges;
    g_lastOfficialYaw = officialYaw;
    visual_diagnostics::RecordCameraFrame(
        true, g_visualYaw != oldYaw, officialYawChanged,
        newX, newY, static_cast<LONG>(snapshot.totalX - snapshot.committedX),
        static_cast<LONG>(snapshot.totalY - snapshot.committedY),
        g_visualYaw, g_visualPitch, officialYaw, stageX, stageY, scaledX, scaledY,
        actions.yaw, actions.pitch, actions.bindingMatches);
    LogStatistics(snapshot);
}

void Invalidate() {
    g_valid = false;
    g_owner = 0;
    visual_diagnostics::InvalidateCamera();
}

VisualLookOrientation GetVisualLookOrientation() {
    return {g_valid, g_frameSerial, g_visualYaw, g_visualPitch};
}
} // namespace high_rate_camera_rotation
