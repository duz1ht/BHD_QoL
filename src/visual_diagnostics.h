#pragma once

#include <windows.h>

#include <cstdint>

namespace visual_diagnostics {

constexpr wchar_t kMappingName[] = L"Local\\BHD_QoL_VisualDiagnostics";
constexpr uint32_t kMagic = 0x51444842; // "BHDQ"
constexpr uint32_t kVersion = 3;

enum class CameraSupportReason : LONG {
    Supported = 0,
    FeatureDisabled,
    RawInputInactive,
    CameraMode,
    Paused,
    MouseDisabled,
    InputObjectMissing,
    InputReadyMissing,
    LocalPlayerMissing,
    CameraOwnerMismatch,
    MountedOrVehicle,
};

struct SharedTelemetry {
    uint32_t magic;
    uint32_t version;
    uint32_t processId;
    uint32_t gameWindow;
    volatile LONG rawActive;
    volatile LONG cameraValid;
    volatile LONG rawReports;
    volatile LONG logicPolls;
    volatile LONG renderPolls;
    volatile LONG cameraEvaluations;
    volatile LONG cameraFrames;
    volatile LONG visualYawChanges;
    volatile LONG officialYawChanges;
    volatile LONG rawX;
    volatile LONG rawY;
    volatile LONG logicX;
    volatile LONG logicY;
    volatile LONG renderX;
    volatile LONG renderY;
    volatile LONG pendingX;
    volatile LONG pendingY;
    volatile LONG visualYaw;
    volatile LONG visualPitch;
    volatile LONG officialYaw;
    volatile LONG lastVisualDeltaX;
    volatile LONG lastVisualDeltaY;
    volatile LONG cameraSupportReason;
    volatile LONG cameraMode;
    volatile LONG pauseState;
    volatile LONG mouseEnabled;
    volatile LONG inputObject;
    volatile LONG inputReady;
    volatile LONG localPlayer;
    volatile LONG cameraOwner;
    volatile LONG rideTarget;
    volatile LONG firstStageX;
    volatile LONG firstStageY;
    volatile LONG scaledX;
    volatile LONG scaledY;
    volatile LONG yawAction;
    volatile LONG pitchAction;
    volatile LONG lookBindingMatches;
};

bool Initialize();
void SetGameWindow(HWND window);
void SetRawActive(bool active);
void RecordRawReport(LONG x, LONG y);
void RecordLogicPoll(LONG x, LONG y);
void RecordRenderPoll(LONG x, LONG y);
void RecordCameraEvaluation();
void SetCameraSupport(CameraSupportReason reason, LONG cameraMode, LONG pauseState,
                      LONG mouseEnabled, uintptr_t inputObject, uintptr_t inputReady,
                      uintptr_t localPlayer, uintptr_t cameraOwner, uintptr_t rideTarget);
void RecordCameraFrame(bool valid, bool visualYawChanged, bool officialYawChanged,
                       LONG newRawX, LONG newRawY, LONG pendingX, LONG pendingY,
                       uint32_t visualYaw, uint32_t visualPitch, uint32_t officialYaw,
                       LONG firstStageX, LONG firstStageY, LONG scaledX, LONG scaledY,
                       LONG yawAction, LONG pitchAction, LONG lookBindingMatches);
void InvalidateCamera();

} // namespace visual_diagnostics
