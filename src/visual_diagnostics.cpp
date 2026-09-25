#include "visual_diagnostics.h"

#include <cstring>

#include "logger.h"

namespace visual_diagnostics {
namespace {
HANDLE g_mapping = nullptr;
SharedTelemetry* g_shared = nullptr;

void Add(volatile LONG* value, LONG amount = 1) {
    if (g_shared != nullptr) InterlockedExchangeAdd(value, amount);
}
} // namespace

bool Initialize() {
    if (g_shared != nullptr) return true;
    SECURITY_DESCRIPTOR descriptor = {};
    SECURITY_ATTRIBUTES security = {};
    SECURITY_ATTRIBUTES* securityPointer = nullptr;
    if (InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&descriptor, TRUE, nullptr, FALSE)) {
        security.nLength = sizeof(security);
        security.lpSecurityDescriptor = &descriptor;
        security.bInheritHandle = FALSE;
        securityPointer = &security;
    }
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, securityPointer, PAGE_READWRITE, 0,
                                   sizeof(SharedTelemetry), kMappingName);
    if (g_mapping == nullptr) {
        logger::Log("ERROR", "VisualDiagnostics", "CreateFileMapping failed error=%lu",
                    GetLastError());
        return false;
    }
    g_shared = static_cast<SharedTelemetry*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedTelemetry)));
    if (g_shared == nullptr) {
        logger::Log("ERROR", "VisualDiagnostics", "MapViewOfFile failed error=%lu",
                    GetLastError());
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }
    std::memset(g_shared, 0, sizeof(*g_shared));
    g_shared->magic = kMagic;
    g_shared->version = kVersion;
    g_shared->processId = GetCurrentProcessId();
    return true;
}

void SetGameWindow(HWND window) {
    if (g_shared != nullptr) g_shared->gameWindow = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window));
}

void SetRawActive(bool active) {
    if (g_shared != nullptr) InterlockedExchange(&g_shared->rawActive, active ? 1 : 0);
}

void RecordRawReport(LONG x, LONG y) {
    if (g_shared == nullptr) return;
    Add(&g_shared->rawReports);
    Add(&g_shared->rawX, x);
    Add(&g_shared->rawY, y);
}

void RecordLogicPoll(LONG x, LONG y) {
    if (g_shared == nullptr) return;
    Add(&g_shared->logicPolls);
    Add(&g_shared->logicX, x);
    Add(&g_shared->logicY, y);
}

void RecordRenderPoll(LONG x, LONG y) {
    if (g_shared == nullptr) return;
    Add(&g_shared->renderPolls);
    Add(&g_shared->renderX, x);
    Add(&g_shared->renderY, y);
}

void RecordCameraEvaluation() {
    if (g_shared != nullptr) Add(&g_shared->cameraEvaluations);
}

void SetCameraSupport(CameraSupportReason reason, LONG cameraMode, LONG pauseState,
                      LONG mouseEnabled, uintptr_t inputObject, uintptr_t inputReady,
                      uintptr_t localPlayer, uintptr_t cameraOwner, uintptr_t rideTarget) {
    if (g_shared == nullptr) return;
    InterlockedExchange(&g_shared->cameraSupportReason, static_cast<LONG>(reason));
    InterlockedExchange(&g_shared->cameraMode, cameraMode);
    InterlockedExchange(&g_shared->pauseState, pauseState);
    InterlockedExchange(&g_shared->mouseEnabled, mouseEnabled);
    InterlockedExchange(&g_shared->inputObject, static_cast<LONG>(inputObject));
    InterlockedExchange(&g_shared->inputReady, static_cast<LONG>(inputReady));
    InterlockedExchange(&g_shared->localPlayer, static_cast<LONG>(localPlayer));
    InterlockedExchange(&g_shared->cameraOwner, static_cast<LONG>(cameraOwner));
    InterlockedExchange(&g_shared->rideTarget, static_cast<LONG>(rideTarget));
}

void RecordCameraFrame(bool valid, bool visualYawChanged, bool officialYawChanged,
                       LONG newRawX, LONG newRawY, LONG pendingX, LONG pendingY,
                       uint32_t visualYaw, uint32_t visualPitch, uint32_t officialYaw) {
    if (g_shared == nullptr) return;
    InterlockedExchange(&g_shared->cameraValid, valid ? 1 : 0);
    Add(&g_shared->cameraFrames);
    if (visualYawChanged) Add(&g_shared->visualYawChanges);
    if (officialYawChanged) Add(&g_shared->officialYawChanges);
    InterlockedExchange(&g_shared->lastVisualDeltaX, newRawX);
    InterlockedExchange(&g_shared->lastVisualDeltaY, newRawY);
    InterlockedExchange(&g_shared->pendingX, pendingX);
    InterlockedExchange(&g_shared->pendingY, pendingY);
    InterlockedExchange(&g_shared->visualYaw, static_cast<LONG>(visualYaw));
    InterlockedExchange(&g_shared->visualPitch, static_cast<LONG>(visualPitch));
    InterlockedExchange(&g_shared->officialYaw, static_cast<LONG>(officialYaw));
}

void InvalidateCamera() {
    if (g_shared != nullptr) InterlockedExchange(&g_shared->cameraValid, 0);
}
} // namespace visual_diagnostics
