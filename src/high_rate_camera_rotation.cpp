#include "high_rate_camera_rotation.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

#include "free_rate_mouse_poll.h"
#include "game_mouse_look_predictor.h"
#include "logger.h"
#include "raw_input.h"

namespace high_rate_camera_rotation {
namespace {
constexpr uintptr_t kCameraMode = 0x007F2DD0;
constexpr uintptr_t kCameraTarget = 0x007F2DD4;
constexpr uintptr_t kCameraYaw = 0x007F2DE4;
constexpr uintptr_t kCameraPitch = 0x007F2DE8;
constexpr uintptr_t kLocalPlayer = 0x0096C290;
constexpr LONG kNormalFirstPersonMode = 0;

bool g_enabled = false;
DWORD g_lastDiagnosticsTick = 0;

int32_t WrappedDifference(LONG total, LONG committed) {
    return static_cast<int32_t>(static_cast<uint32_t>(total) -
                                static_cast<uint32_t>(committed));
}

bool IsReadable(const void* pointer, size_t size) {
    MEMORY_BASIC_INFORMATION info = {};
    if (pointer == nullptr || VirtualQuery(pointer, &info, sizeof(info)) == 0 ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const uintptr_t start = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t end = start + size;
    const uintptr_t regionEnd =
        reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return end >= start && end <= regionEnd;
}
}  // namespace

bool Install(bool enabled) {
    g_enabled = false;
    if (!enabled) {
        logger::Log("INFO", "HighRateCameraRotation", "feature disabled");
        return true;
    }
    if (!raw_input::IsEnabled() || !free_rate_mouse_poll::IsInstalled()) {
        logger::Log("ERROR", "HighRateCameraRotation",
                    "installation skipped; RawMouseInput and FreeRateMousePoll are required");
        return false;
    }
    g_enabled = true;
    g_lastDiagnosticsTick = GetTickCount();
    logger::Log("INFO", "HighRateCameraRotation",
                "feature enabled for normal local-player first person");
    return true;
}

void Apply() {
    if (!g_enabled || !raw_input::IsBackendActive()) return;
    if (*reinterpret_cast<volatile LONG*>(kCameraMode) != kNormalFirstPersonMode)
        return;

    void* const player = *reinterpret_cast<void* volatile*>(kLocalPlayer);
    void* const target = *reinterpret_cast<void* volatile*>(kCameraTarget);
    if (target != player || !IsReadable(player, 568)) return;

    const raw_input::PredictionSnapshot snapshot =
        raw_input::GetPredictionSnapshot();
    const int32_t residualX = WrappedDifference(snapshot.totalX, snapshot.committedX);
    const int32_t residualY = WrappedDifference(snapshot.totalY, snapshot.committedY);

    game_mouse_look_predictor::Result prediction = {};
    if (!game_mouse_look_predictor::Predict(
            player, residualX, residualY, &prediction)) return;

    auto* const cameraYaw = reinterpret_cast<volatile uint32_t*>(kCameraYaw);
    auto* const cameraPitch = reinterpret_cast<volatile uint32_t*>(kCameraPitch);
    *cameraYaw += static_cast<uint32_t>(prediction.yawDelta);
    *cameraPitch += static_cast<uint32_t>(prediction.pitchDelta);

    const DWORD now = GetTickCount();
    if (now - g_lastDiagnosticsTick >= 1000) {
        g_lastDiagnosticsTick = now;
        logger::Log(
            "INFO", "HighRateCameraRotation.Stats",
            "logic_serial=%ld residual_x=%ld residual_y=%ld "
            "first_zero=%ld,%ld first_moved=%ld,%ld "
            "scaled_zero=%ld,%ld scaled_moved=%ld,%ld "
            "actions_zero=%d,%d actions_moved=%d,%d "
            "controls_zero=%ld,%ld controls_moved=%ld,%ld correction=%ld,%ld",
            snapshot.logicPollSerial, residualX, residualY,
            prediction.zero.firstX, prediction.zero.firstY,
            prediction.moved.firstX, prediction.moved.firstY,
            prediction.zero.scaledX, prediction.zero.scaledY,
            prediction.moved.scaledX, prediction.moved.scaledY,
            prediction.zero.actions.yaw, prediction.zero.actions.pitch,
            prediction.moved.actions.yaw, prediction.moved.actions.pitch,
            prediction.zero.yawControl, prediction.zero.pitchControl,
            prediction.moved.yawControl, prediction.moved.pitchControl,
            prediction.yawDelta, prediction.pitchDelta);
    }
}

}  // namespace high_rate_camera_rotation
