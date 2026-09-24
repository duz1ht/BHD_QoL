#include "high_rate_camera.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "logger.h"
#include "raw_input.h"

namespace high_rate_camera {
namespace {
constexpr uintptr_t kCalculateCameraPositions = 0x0043B5E0;
constexpr uintptr_t kCallSite = 0x004B7297;
constexpr uintptr_t kCameraEntity = 0x007F2DD4;
constexpr uintptr_t kCameraMode = 0x007F2DD0;
constexpr uintptr_t kLocalPlayer = 0x0096C290;
constexpr uintptr_t kRenderYaw = 0x007F2DE4;
constexpr uintptr_t kRenderPitch = 0x007F2DE8;
constexpr unsigned char kExpectedCall[] = {0xE8, 0x44, 0x43, 0xF8, 0xFF};

using CalculateCameraPositionsFn = int(__cdecl*)();

struct AxisCalibration {
    double unitsPerCount;
    bool valid;
};

AxisCalibration g_yaw = {};
AxisCalibration g_pitch = {};
raw_input::PredictionSnapshot g_previousMouse = {};
uint32_t g_previousYaw = 0;
int32_t g_previousPitch = 0;
bool g_havePrevious = false;
LARGE_INTEGER g_frequency = {};
LARGE_INTEGER g_statisticsStart = {};
volatile LONG g_renderCalls = 0;
unsigned long g_statisticsIntervalMs = 5000;

int32_t ModularDelta(uint32_t current, uint32_t previous) {
    return static_cast<int32_t>(current - previous);
}

void Learn(AxisCalibration& calibration, LONG counts, int32_t cameraDelta) {
    if (counts == 0) return;
    const double sample = static_cast<double>(cameraDelta) / counts;
    if (!std::isfinite(sample) || std::fabs(sample) > 10000000.0) return;
    // Reject samples containing non-mouse camera motion once a mapping exists.
    if (calibration.valid && std::fabs(sample - calibration.unitsPerCount) >
                                 std::max(2.0, std::fabs(calibration.unitsPerCount) * 0.25)) {
        return;
    }
    calibration.unitsPerCount = sample;
    calibration.valid = true;
}

void LogStatistics(LONG residualX, LONG residualY) {
    InterlockedIncrement(&g_renderCalls);
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    const LONGLONG ticks = now.QuadPart - g_statisticsStart.QuadPart;
    if (g_frequency.QuadPart == 0 || ticks * 1000 <
            static_cast<LONGLONG>(g_statisticsIntervalMs) * g_frequency.QuadPart) return;
    const LONG calls = InterlockedExchange(&g_renderCalls, 0);
    const double seconds = static_cast<double>(ticks) / g_frequency.QuadPart;
    logger::Log("INFO", "HighRateCamera.Stats",
                "render_calls_hz=%.1f residual_dx=%ld residual_dy=%ld yaw_calibrated=%d "
                "pitch_calibrated=%d yaw_units_per_count=%.3f pitch_units_per_count=%.3f",
                calls / seconds, residualX, residualY, g_yaw.valid, g_pitch.valid,
                g_yaw.unitsPerCount, g_pitch.unitsPerCount);
    g_statisticsStart = now;
}

void ApplyCorrection() {
    const auto mouse = raw_input::GetPredictionSnapshot();
    const uint32_t baseYaw = *reinterpret_cast<volatile uint32_t*>(kRenderYaw);
    const int32_t basePitch = *reinterpret_cast<volatile int32_t*>(kRenderPitch);
    const bool validCamera = raw_input::IsEnabled() &&
        *reinterpret_cast<void* volatile*>(kCameraEntity) != nullptr &&
        *reinterpret_cast<void* volatile*>(kLocalPlayer) != nullptr &&
        *reinterpret_cast<volatile LONG*>(kCameraMode) == 0;

    if (!validCamera) {
        Reset();
        g_previousMouse = mouse;
        g_previousYaw = baseYaw;
        g_previousPitch = basePitch;
        g_havePrevious = true;
        return;
    }

    if (g_havePrevious) {
        const LONG committedX = mouse.committedX - g_previousMouse.committedX;
        const LONG committedY = mouse.committedY - g_previousMouse.committedY;
        Learn(g_yaw, committedX, ModularDelta(baseYaw, g_previousYaw));
        Learn(g_pitch, committedY, basePitch - g_previousPitch);
    }
    g_previousMouse = mouse;
    g_previousYaw = baseYaw;
    g_previousPitch = basePitch;
    g_havePrevious = true;

    const LONG residualX = mouse.totalX - mouse.committedX;
    const LONG residualY = mouse.totalY - mouse.committedY;
    LogStatistics(residualX, residualY);
    if (residualX != 0 && g_yaw.valid) {
        const int64_t delta = static_cast<int64_t>(std::llround(residualX * g_yaw.unitsPerCount));
        *reinterpret_cast<volatile uint32_t*>(kRenderYaw) = baseYaw + static_cast<uint32_t>(delta);
    }
    if (residualY != 0 && g_pitch.valid) {
        const int64_t delta = static_cast<int64_t>(std::llround(residualY * g_pitch.unitsPerCount));
        const int64_t predicted = static_cast<int64_t>(basePitch) + delta;
        // Do not manufacture a pitch range: only apply values representable by the
        // game's signed integer angle storage. The simulation remains authoritative.
        if (predicted >= std::numeric_limits<int32_t>::min() &&
            predicted <= std::numeric_limits<int32_t>::max()) {
            *reinterpret_cast<volatile int32_t*>(kRenderPitch) = static_cast<int32_t>(predicted);
        }
    }
}

extern "C" int __cdecl CalculateCameraPositionsHook() {
    const int result = reinterpret_cast<CalculateCameraPositionsFn>(
        kCalculateCameraPositions)();
    ApplyCorrection();
    return result;
}
}  // namespace

void Reset() {
    g_yaw = {};
    g_pitch = {};
    g_havePrevious = false;
}

bool Install(const Settings& settings) {
    if (!settings.enabled) {
        logger::Log("INFO", "HighRateCamera", "feature disabled (requires RawMouseInput)");
        return false;
    }
    auto* call = reinterpret_cast<unsigned char*>(kCallSite);
    if (std::memcmp(call, kExpectedCall, sizeof(kExpectedCall)) != 0) {
        logger::Log("ERROR", "HighRateCamera", "unexpected call-site bytes at 0x%08lX",
                    static_cast<unsigned long>(kCallSite));
        return false;
    }
    const intptr_t displacement = reinterpret_cast<uintptr_t>(&CalculateCameraPositionsHook) -
                                  (kCallSite + sizeof(kExpectedCall));
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(call, sizeof(kExpectedCall), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        logger::Log("ERROR", "HighRateCamera", "VirtualProtect failed: error=%lu", GetLastError());
        return false;
    }
    call[0] = 0xE8;
    const int32_t relative = static_cast<int32_t>(displacement);
    std::memcpy(call + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), call, sizeof(kExpectedCall));
    DWORD ignored = 0;
    VirtualProtect(call, sizeof(kExpectedCall), oldProtect, &ignored);
    g_statisticsIntervalMs = settings.statisticsIntervalMs;
    QueryPerformanceFrequency(&g_frequency);
    QueryPerformanceCounter(&g_statisticsStart);
    raw_input::ResetPredictionState();
    Reset();
    logger::Log("INFO", "HighRateCamera",
                "render camera hook installed at 0x%08lX; rotation mapping learns from game ticks",
                static_cast<unsigned long>(kCallSite));
    return true;
}

}  // namespace high_rate_camera
