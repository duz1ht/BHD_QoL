#pragma once

#include <windows.h>

#include <cstdint>

namespace visual_diagnostics {

constexpr wchar_t kMappingName[] = L"Local\\BHD_QoL_VisualDiagnostics_v1";
constexpr uint32_t kMagic = 0x51444842; // "BHDQ"
constexpr uint32_t kVersion = 1;

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
};

bool Initialize();
void SetGameWindow(HWND window);
void SetRawActive(bool active);
void RecordRawReport(LONG x, LONG y);
void RecordLogicPoll(LONG x, LONG y);
void RecordRenderPoll(LONG x, LONG y);
void RecordCameraFrame(bool valid, bool visualYawChanged, bool officialYawChanged,
                       LONG newRawX, LONG newRawY, LONG pendingX, LONG pendingY,
                       uint32_t visualYaw, uint32_t visualPitch, uint32_t officialYaw);
void InvalidateCamera();

} // namespace visual_diagnostics
