#include "visual_interpolation.h"

#include <windows.h>

#include <cstdint>

#include "logger.h"
#include "raw_input.h"
#include "visual_mouse_late_latch_math.h"

namespace visual_interpolation {
namespace {
bool g_enabled = false;
raw_input::PendingVisualMouse g_frameSample = {};
VisualMouseAngles g_frameAngles = {};
visual_mouse_late_latch_math::ReconcileState g_reconcile = {};
uint64_t g_capturedReports = 0;
uint64_t g_latchedFrames = 0;
uint64_t g_reconciledCounts = 0;
uint64_t g_rebases = 0;
uint64_t g_focusBypass = 0;
uint64_t g_cameraModeBypass = 0;
uint64_t g_unknownScaleBypass = 0;
uint64_t g_maxDivergence = 0;
uint64_t g_lastCaptureSequence = 0;
DWORD g_lastLogTick = 0;

uint64_t Magnitude(int64_t value) {
    return value < 0 ? static_cast<uint64_t>(-(value + 1)) + 1 :
                       static_cast<uint64_t>(value);
}

void LogStats() {
    logger::Log("INFO", "VisualInterpolation.Stats",
                "diagnostic=1 captured_reports=%llu pending_x=%lld pending_y=%lld "
                "late_latched_frames=%llu reconciled_counts=%llu rebases=%llu "
                "bypass_focus=%llu bypass_camera_mode=%llu bypass_unknown_scale=%llu "
                "max_yaw_correction=0 max_pitch_correction=0 max_capture_consumption_divergence=%llu",
                static_cast<unsigned long long>(g_capturedReports),
                static_cast<long long>(g_frameSample.x),
                static_cast<long long>(g_frameSample.y),
                static_cast<unsigned long long>(g_latchedFrames),
                static_cast<unsigned long long>(g_reconciledCounts),
                static_cast<unsigned long long>(g_rebases),
                static_cast<unsigned long long>(g_focusBypass),
                static_cast<unsigned long long>(g_cameraModeBypass),
                static_cast<unsigned long long>(g_unknownScaleBypass),
                static_cast<unsigned long long>(g_maxDivergence));
}
}  // namespace

bool Install(bool enabled) {
    g_enabled = enabled;
    g_lastLogTick = GetTickCount();
    logger::Log("INFO", "VisualMouseLateLatching",
                enabled ? "diagnostic mode enabled; correction is fail-closed pending runtime timing proof"
                        : "feature disabled");
    return true;
}

void BeginVisualFrame() {
    g_frameSample = {};
    g_frameAngles = {};  // Never expose unproved counts as angles.
    if (!g_enabled) return;
    g_frameSample = raw_input::GetPendingVisualMouse(); // exactly one read per frame
    if (g_frameSample.captureSequence >= g_lastCaptureSequence)
        g_capturedReports += g_frameSample.captureSequence - g_lastCaptureSequence;
    else
        ++g_rebases;
    g_lastCaptureSequence = g_frameSample.captureSequence;
    if (!g_frameSample.valid) {
        ++g_focusBypass;
    } else {
        visual_mouse_late_latch_math::CounterSample sample = {};
        // PendingVisualMouse intentionally exposes only the already-computed difference.
        sample.capturedX = g_frameSample.x;
        sample.capturedY = g_frameSample.y;
        sample.captureSequence = g_frameSample.captureSequence;
        sample.consumedSequence = g_frameSample.consumedSequence;
        sample.valid = true;
        const auto result = visual_mouse_late_latch_math::Reconcile(sample, &g_reconcile);
        if (result.rebase) ++g_rebases;
        g_reconciledCounts += Magnitude(result.reconciledX) + Magnitude(result.reconciledY);
        const uint64_t divergence = Magnitude(g_frameSample.x) + Magnitude(g_frameSample.y);
        if (divergence > g_maxDivergence) g_maxDivergence = divergence;
        ++g_unknownScaleBypass;
    }
    const DWORD now = GetTickCount();
    if (now - g_lastLogTick >= 5000) {
        g_lastLogTick = now;
        LogStats();
    }
}

VisualMouseAngles GetVisualMouseAngles() { return g_frameAngles; }

void ApplyCameraCorrection(void*, uint32_t cameraMode, bool primaryCallsite) {
    if (!g_enabled || !primaryCallsite) return;
    if (cameraMode != 0) ++g_cameraModeBypass;
    // Diagnostic mode deliberately leaves the stack-local camera untouched.
}

void ApplyViewmodelCorrection(void*, uintptr_t) {
    // Diagnostic mode deliberately leaves the 0x00488AF6 stack-local matrix untouched.
}

void Reset(const char* reason) {
    g_frameSample = {};
    g_frameAngles = {};
    g_reconcile = {};
    g_lastCaptureSequence = 0;
    ++g_rebases;
    logger::Log("INFO", "VisualMouseLateLatching", "rebase reason=%s", reason);
}
}  // namespace visual_interpolation
