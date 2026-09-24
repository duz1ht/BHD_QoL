#include "presentation_state.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "logger.h"
#include "presentation_math.h"
#include "raw_input.h"

namespace presentation_state {
namespace {
constexpr uintptr_t kGameTick = 0x004B4970;
constexpr uintptr_t kBuildCamera = 0x0043B5E0;
constexpr uintptr_t kBuildCameraCall = 0x004B7297;
constexpr uintptr_t kCameraYaw = 0x007F2DE4;
constexpr uintptr_t kCameraPitch = 0x007F2DE8;
constexpr size_t kGameTickHookSize = 5;
const unsigned char kGameTickExpected[] = {0xA1, 0x98, 0x33, 0xA0, 0x00};
const unsigned char kCameraCallExpected[] = {0xE8, 0x44, 0x43, 0xF8, 0xFF};

using GameTickFn = int(__cdecl*)();
using BuildCameraFn = void(__cdecl*)();

struct SimulationSnapshot {
    int32_t rawX;
    int32_t rawY;
    unsigned long inputEpoch;
    uint64_t tickIndex;
    int64_t qpcTime;
    bool valid;
};

struct PresentationState {
    SimulationSnapshot previous;
    SimulationSnapshot current;
    uint32_t baseYaw;
    uint32_t basePitch;
    uint32_t previousBaseYaw;
    uint32_t previousBasePitch;
    double yawUnitsPerCount;
    double pitchUnitsPerCount;
    int64_t lastFrameQpc;
    double frameDt;
    uint64_t appliedTick;
    bool cameraValid;
};

Settings g_settings = {};
PresentationState g_state = {};
GameTickFn g_originalGameTick = nullptr;
BuildCameraFn g_originalBuildCamera = reinterpret_cast<BuildCameraFn>(kBuildCamera);
LARGE_INTEGER g_qpcFrequency = {};

void RebaseCamera(uint32_t yaw, uint32_t pitch) {
    g_state.baseYaw = g_state.previousBaseYaw = yaw;
    g_state.basePitch = g_state.previousBasePitch = pitch;
    g_state.yawUnitsPerCount = 0.0;
    g_state.pitchUnitsPerCount = 0.0;
    g_state.cameraValid = true;
}

void LearnScale(uint32_t currentAngle, uint32_t previousAngle, int32_t counts,
                double* scale) {
    if (counts == 0) return;
    const double observed = static_cast<double>(
        presentation_math::AngleDelta(currentAngle, previousAngle)) / counts;
    // Reject camera-mode transitions, scripted turns, and bob/shake outliers.
    constexpr double kMaximumUnitsPerCount = 67108864.0; // 1/64 turn
    if (!std::isfinite(observed) || std::fabs(observed) > kMaximumUnitsPerCount) return;
    *scale = *scale == 0.0 ? observed : (*scale * 0.75 + observed * 0.25);
}

extern "C" int __cdecl PresentationGameTick() {
    const int result = g_originalGameTick();
    g_state.previous = g_state.current;
    const raw_input::MovementSnapshot raw = raw_input::GetMovementSnapshot();
    g_state.current.rawX = static_cast<int32_t>(raw.x);
    g_state.current.rawY = static_cast<int32_t>(raw.y);
    g_state.current.inputEpoch = raw.epoch;
    g_state.current.tickIndex = g_state.previous.tickIndex + 1;
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    g_state.current.qpcTime = now.QuadPart;
    g_state.current.valid = raw.active;
    return result;
}

extern "C" void __cdecl PresentationBuildCamera() {
    g_originalBuildCamera();
    if (!g_settings.enabled || !g_settings.cameraPresentation ||
        !g_settings.renderRateMouse || !g_state.current.valid) return;

    auto* yaw = reinterpret_cast<volatile uint32_t*>(kCameraYaw);
    auto* pitch = reinterpret_cast<volatile uint32_t*>(kCameraPitch);
    const uint32_t originalYaw = *yaw;
    const uint32_t originalPitch = *pitch;
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    if (g_state.lastFrameQpc != 0 && g_qpcFrequency.QuadPart != 0) {
        g_state.frameDt = static_cast<double>(now.QuadPart - g_state.lastFrameQpc) /
                          static_cast<double>(g_qpcFrequency.QuadPart);
        if (g_state.frameDt < 0.0) g_state.frameDt = 0.0;
        if (g_state.frameDt > 0.1) g_state.frameDt = 0.1;
    }
    g_state.lastFrameQpc = now.QuadPart;

    if (!g_state.cameraValid || g_state.appliedTick != g_state.current.tickIndex) {
        if (!g_state.cameraValid || !g_state.previous.valid ||
            g_state.previous.inputEpoch != g_state.current.inputEpoch) {
            RebaseCamera(originalYaw, originalPitch);
        } else {
            const int32_t tickX = presentation_math::CounterDelta(
                g_state.current.rawX, g_state.previous.rawX);
            const int32_t tickY = presentation_math::CounterDelta(
                g_state.current.rawY, g_state.previous.rawY);
            LearnScale(originalYaw, g_state.baseYaw, tickX,
                       &g_state.yawUnitsPerCount);
            LearnScale(originalPitch, g_state.basePitch, tickY,
                       &g_state.pitchUnitsPerCount);
            g_state.previousBaseYaw = g_state.baseYaw;
            g_state.previousBasePitch = g_state.basePitch;
            g_state.baseYaw = originalYaw;
            g_state.basePitch = originalPitch;
        }
        g_state.appliedTick = g_state.current.tickIndex;
    }

    const raw_input::MovementSnapshot raw = raw_input::GetMovementSnapshot();
    if (!raw.active || raw.epoch != g_state.current.inputEpoch) {
        g_state.cameraValid = false;
        return;
    }
    const int32_t pendingX = presentation_math::CounterDelta(
        static_cast<int32_t>(raw.x), g_state.current.rawX);
    const int32_t pendingY = presentation_math::CounterDelta(
        static_cast<int32_t>(raw.y), g_state.current.rawY);
    // Compose the render-rate look offset over this frame's original camera.
    // This retains DFBHD's render-side bob, shake, scripted offsets and roll.
    *yaw = presentation_math::AddPredictedAngle(originalYaw, pendingX,
                                                 g_state.yawUnitsPerCount);
    *pitch = presentation_math::AddPredictedAngle(originalPitch, pendingY,
                                                   g_state.pitchUnitsPerCount);
}

bool WriteRelativeCall(uintptr_t address, const void* target,
                       const unsigned char (&expected)[5]) {
    auto* bytes = reinterpret_cast<unsigned char*>(address);
    if (std::memcmp(bytes, expected, 5) != 0) return false;
    unsigned char patch[5] = {0xE8, 0, 0, 0, 0};
    *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(target) - (address + 5));
    DWORD oldProtect = 0;
    if (!VirtualProtect(bytes, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    std::memcpy(bytes, patch, 5);
    FlushInstructionCache(GetCurrentProcess(), bytes, 5);
    DWORD ignored = 0;
    VirtualProtect(bytes, 5, oldProtect, &ignored);
    return true;
}

bool InstallGameTickHook() {
    auto* target = reinterpret_cast<unsigned char*>(kGameTick);
    if (std::memcmp(target, kGameTickExpected, kGameTickHookSize) != 0) return false;
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) return false;
    std::memcpy(trampoline, target, 5);
    trampoline[5] = 0xE9;
    *reinterpret_cast<int32_t*>(trampoline + 6) = static_cast<int32_t>(
        (kGameTick + 5) - reinterpret_cast<uintptr_t>(trampoline + 10));
    g_originalGameTick = reinterpret_cast<GameTickFn>(trampoline);
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    target[0] = 0xE9;
    *reinterpret_cast<int32_t*>(target + 1) = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&PresentationGameTick) - (kGameTick + 5));
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    DWORD ignored = 0;
    VirtualProtect(target, 5, oldProtect, &ignored);
    return true;
}
}  // namespace

bool Install(const Settings& settings) {
    g_settings = settings;
    if (!settings.enabled) {
        logger::Log("INFO", "Presentation", "feature disabled");
        return true;
    }
    QueryPerformanceFrequency(&g_qpcFrequency);
    if (!InstallGameTickHook()) {
        logger::Log("ERROR", "Presentation", "game tick hook validation failed");
        return false;
    }
    if (!WriteRelativeCall(kBuildCameraCall, reinterpret_cast<const void*>(
            &PresentationBuildCamera), kCameraCallExpected)) {
        logger::Log("ERROR", "Presentation", "camera call hook validation failed");
        return false;
    }
    logger::Log("INFO", "Presentation",
                "installed tick snapshots, QPC frame timing, and render-rate camera mouse");
    return true;
}

}  // namespace presentation_state
