#include "high_frequency_camera.h"

#include <windows.h>

#include <cstdint>

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
constexpr uintptr_t kPitchScaleDisabled = 0x009F2178;
constexpr uintptr_t kSpecialCameraState = 0x0096C4B0;

constexpr uintptr_t kHasScopedView = 0x0052CE00;
constexpr uintptr_t kHasZoomScale = 0x00488D90;
constexpr uintptr_t kGetZoomScaleDivisor = 0x00488260;

constexpr size_t kCameraYawOffset = 0x0C;
constexpr size_t kCameraPitchOffset = 0x10;
constexpr size_t kPlayerDefinitionOffset = 0x24;
constexpr size_t kPlayerHealthOffset = 0xE2;
constexpr size_t kPlayerFlagsOffset = 0x18C;
constexpr size_t kYawFilterOffset = 0x1B0;
constexpr size_t kDefinitionMaxHealthOffset = 0x15C;

using QueryFn = int(__cdecl*)();

bool g_enabled = false;
bool g_loggedActive = false;

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

int16_t YawFilterStart(uintptr_t player) {
    int16_t state = *reinterpret_cast<const volatile int16_t*>(
        player + kYawFilterOffset);
    const uintptr_t definition = *reinterpret_cast<const volatile uintptr_t*>(
        player + kPlayerDefinitionOffset);
    if (definition != 0) {
        const int health = *reinterpret_cast<const volatile int16_t*>(
            player + kPlayerHealthOffset);
        const int maxHealth = *reinterpret_cast<const volatile int16_t*>(
            definition + kDefinitionMaxHealthOffset);
        const uint32_t flags = *reinterpret_cast<const volatile uint32_t*>(
            player + kPlayerFlagsOffset);
        if (health <= (maxHealth * 3 >> 6) && (flags & 0x10000) == 0) {
            state = static_cast<int16_t>(state - 0x3C);
        }
    }
    return state;
}

} // namespace

void SetEnabled(bool enabled) {
    g_enabled = enabled;
    logger::Log("INFO", "HighFrequencyCamera", "feature %s",
                enabled ? "enabled" : "disabled");
}

bool Apply(void* sourceCamera) {
    if (!g_enabled || sourceCamera == nullptr || Read<int32_t>(kCameraMode) != 0 ||
        Read<int32_t>(kMouseEnabled) == 0) {
        return false;
    }

    const uintptr_t player = Read<uintptr_t>(kLocalPlayer);
    if (player == 0 || Read<uintptr_t>(kCameraTarget) != player) {
        return false;
    }

    LONG pendingX = 0;
    LONG pendingY = 0;
    if (!raw_input::GetPendingMovement(&pendingX, &pendingY) ||
        (pendingX == 0 && pendingY == 0)) {
        return false;
    }

    const int16_t yawCommand = static_cast<int16_t>(
        ScaleForCurrentView(pendingX, 0));
    const int32_t nativeY = Read<int32_t>(kInvertMouse) == 0 ? -pendingY : pendingY;
    const int16_t pitchCommand = static_cast<int16_t>(
        ScaleForCurrentView(nativeY, 1));

    const int16_t filterStart = YawFilterStart(player);
    const int32_t yawOffset = PendingYawOffset(filterStart, yawCommand);
    const int32_t pitchOffset = PendingPitchOffset(
        pitchCommand, Read<int32_t>(kPitchScaleDisabled) == 0);

    auto* camera = static_cast<unsigned char*>(sourceCamera);
    *reinterpret_cast<int32_t*>(camera + kCameraYawOffset) += yawOffset;
    *reinterpret_cast<int32_t*>(camera + kCameraPitchOffset) += pitchOffset;

    if (!g_loggedActive) {
        g_loggedActive = true;
        logger::Log("INFO", "HighFrequencyCamera",
                    "first pending movement applied dx=%ld dy=%ld yaw_offset=%ld pitch_offset=%ld",
                    pendingX, pendingY, static_cast<long>(yawOffset),
                    static_cast<long>(pitchOffset));
    }
    return yawOffset != 0 || pitchOffset != 0;
}

} // namespace high_frequency_camera
