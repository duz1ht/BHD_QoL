#include "camera_trace.h"

#include <windows.h>
#include <shlwapi.h>

#include <cstdio>

#include "logger.h"

namespace camera_trace {
namespace {
constexpr unsigned long kMaximumSamples = 250000;
constexpr unsigned long kFlushInterval = 64;
constexpr long long kCaptureDurationUs = 15LL * 1000LL * 1000LL;

HANDLE g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
bool g_lockReady = false;
bool g_enabled = false;
bool g_f10WasDown = false;
LARGE_INTEGER g_frequency = {};
LARGE_INTEGER g_start = {};
unsigned long g_sampleCount = 0;

const char* ResultName(high_frequency_camera::ApplyResultCode code) {
    using Code = high_frequency_camera::ApplyResultCode;
    switch (code) {
        case Code::kApplied: return "applied";
        case Code::kDisabled: return "disabled";
        case Code::kNullSource: return "null_source";
        case Code::kWrongCameraMode: return "wrong_camera_mode";
        case Code::kMouseDisabled: return "mouse_disabled";
        case Code::kNoLocalPlayer: return "no_local_player";
        case Code::kWrongCameraTarget: return "wrong_camera_target";
        case Code::kRawInputUnavailable: return "raw_input_unavailable";
        case Code::kNoPendingMovement: return "no_pending_movement";
        case Code::kZeroOffset: return "zero_offset";
    }
    return "unknown";
}

bool CreateSessionFile() {
    wchar_t directory[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || !PathRemoveFileSpecW(directory)) return false;

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    for (unsigned int suffix = 0; suffix < 100; ++suffix) {
        wchar_t name[160] = {};
        if (suffix == 0) {
            wsprintfW(name, L"BHD_camera_trace_%04u%02u%02u_%02u%02u%02u_%lu.csv",
                      now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                      now.wSecond, GetCurrentProcessId());
        } else {
            wsprintfW(name, L"BHD_camera_trace_%04u%02u%02u_%02u%02u%02u_%lu_%u.csv",
                      now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                      now.wSecond, GetCurrentProcessId(), suffix);
        }
        wchar_t path[MAX_PATH] = {};
        lstrcpynW(path, directory, MAX_PATH);
        if (!PathAppendW(path, name)) return false;
        g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_file != INVALID_HANDLE_VALUE) return true;
        if (GetLastError() != ERROR_FILE_EXISTS) return false;
    }
    return false;
}

void WriteText(const char* text) {
    DWORD written = 0;
    WriteFile(g_file, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
}

long long ElapsedMicroseconds() {
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    return g_frequency.QuadPart == 0 ? 0 :
        (now.QuadPart - g_start.QuadPart) * 1000000LL / g_frequency.QuadPart;
}

void EndCaptureLocked(const char* reason) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    FlushFileBuffers(g_file);
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
    logger::Log("INFO", "HighFrequencyCamera.Trace",
                "camera CSV capture stopped; reason=%s samples=%lu", reason,
                g_sampleCount);
}

bool BeginCaptureLocked() {
    g_sampleCount = 0;
    if (!QueryPerformanceCounter(&g_start) || !CreateSessionFile()) return false;
    WriteText("time_us,render_serial,poll_serial,raw_packet_serial,source_camera,destination_camera,"
              "result,camera_mode,local_player,camera_target,pending_x,pending_y,yaw_command,"
              "pitch_command,yaw_offset,pitch_offset,source_yaw_before,source_pitch_before,"
              "source_yaw_preview,source_pitch_preview,destination_yaw,destination_pitch,"
              "source_yaw_restored,source_pitch_restored,source_restored,destination_received_preview,"
              "consumed_raw_x,consumed_raw_y,camera_yaw_at_poll,camera_pitch_at_poll,"
              "official_yaw_delta_since_poll,official_pitch_delta_since_poll,pending_raw_x,"
              "pending_raw_y,scale_serial,scale_input_x,scale_input_y,scale_output_x,"
              "scale_output_y,mouse_scale,projected_x,projected_y,projection_phase_q16,"
              "raw_direct_serial,raw_buffered_serial,raw_buffer_drain_serial,"
              "raw_buffer_read_failures\r\n");
    FlushFileBuffers(g_file);
    logger::Log("INFO", "HighFrequencyCamera.Trace",
                "F10 capture started; duration_seconds=15 maximum_samples=%lu",
                kMaximumSamples);
    return true;
}
}  // namespace

bool Initialize(bool enabled) {
    if (!enabled) return false;
    InitializeCriticalSection(&g_lock);
    g_lockReady = true;
    if (!QueryPerformanceFrequency(&g_frequency)) {
        DeleteCriticalSection(&g_lock);
        g_lockReady = false;
        return false;
    }
    g_enabled = true;
    logger::Log("INFO", "HighFrequencyCamera.Trace",
                "camera CSV trace armed; press F10 to capture 15 seconds");
    return true;
}

bool IsEnabled() {
    return g_enabled && g_lockReady && g_file != INVALID_HANDLE_VALUE;
}

void UpdateHotkey() {
    if (!g_enabled || !g_lockReady) return;

    EnterCriticalSection(&g_lock);
    const bool f10Down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    const bool f10Pressed = f10Down && !g_f10WasDown;
    g_f10WasDown = f10Down;
    if (g_file != INVALID_HANDLE_VALUE && ElapsedMicroseconds() >= kCaptureDurationUs) {
        EndCaptureLocked("duration_complete");
    }

    if (g_file == INVALID_HANDLE_VALUE && f10Pressed) {
        DWORD foregroundProcess = 0;
        const HWND foreground = GetForegroundWindow();
        if (foreground != nullptr) {
            GetWindowThreadProcessId(foreground, &foregroundProcess);
        }
        if (foregroundProcess == GetCurrentProcessId()) {
            BeginCaptureLocked();
        }
    }
    LeaveCriticalSection(&g_lock);
}

void Record(const Sample& sample) {
    if (!IsEnabled()) return;

    EnterCriticalSection(&g_lock);
    if (g_file == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_lock);
        return;
    }
    const long long timeUs = ElapsedMicroseconds();
    if (timeUs >= kCaptureDurationUs) {
        EndCaptureLocked("duration_complete");
        LeaveCriticalSection(&g_lock);
        return;
    }
    if (g_sampleCount >= kMaximumSamples) {
        EndCaptureLocked("sample_limit");
        LeaveCriticalSection(&g_lock);
        return;
    }

    const bool restored = sample.sourceYawRestored == sample.sourceYawBefore &&
                          sample.sourcePitchRestored == sample.sourcePitchBefore;
    const bool destinationReceivedPreview =
        sample.destinationCamera != nullptr &&
        sample.destinationYaw == sample.sourceYawPreview &&
        sample.destinationPitch == sample.sourcePitchPreview;

    const int32_t officialYawDelta =
        sample.sourceYawBefore - sample.input.cameraYawAtPoll;
    const int32_t officialPitchDelta =
        sample.sourcePitchBefore - sample.input.cameraPitchAtPoll;

    char line[1400] = {};
    _snprintf(line, sizeof(line) - 1,
              "%lld,%lu,%lu,%lu,0x%08lX,0x%08lX,%s,%ld,0x%08lX,0x%08lX,"
              "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%d,%d,"
              "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%lu,%ld,%ld,%ld,%ld,%ld,"
              "%ld,%ld,%lu,%lu,%lu,%lu,%lu\r\n",
              timeUs, sample.renderSerial, sample.pollSerial, sample.rawPacketSerial,
              reinterpret_cast<unsigned long>(sample.sourceCamera),
              reinterpret_cast<unsigned long>(sample.destinationCamera),
              ResultName(sample.apply.code), static_cast<long>(sample.apply.cameraMode),
              static_cast<unsigned long>(sample.apply.localPlayer),
              static_cast<unsigned long>(sample.apply.cameraTarget),
              sample.apply.pendingX, sample.apply.pendingY,
              static_cast<long>(sample.apply.yawCommand),
              static_cast<long>(sample.apply.pitchCommand),
              static_cast<long>(sample.apply.yawOffset),
              static_cast<long>(sample.apply.pitchOffset),
              static_cast<long>(sample.sourceYawBefore),
              static_cast<long>(sample.sourcePitchBefore),
              static_cast<long>(sample.sourceYawPreview),
              static_cast<long>(sample.sourcePitchPreview),
              static_cast<long>(sample.destinationYaw),
              static_cast<long>(sample.destinationPitch),
              static_cast<long>(sample.sourceYawRestored),
              static_cast<long>(sample.sourcePitchRestored), restored,
              destinationReceivedPreview, sample.input.consumedX,
              sample.input.consumedY,
              static_cast<long>(sample.input.cameraYawAtPoll),
              static_cast<long>(sample.input.cameraPitchAtPoll),
              static_cast<long>(officialYawDelta),
              static_cast<long>(officialPitchDelta), sample.pendingRawX,
              sample.pendingRawY, sample.scaling.serial,
              static_cast<long>(sample.scaling.inputX),
              static_cast<long>(sample.scaling.inputY),
              static_cast<long>(sample.scaling.outputX),
              static_cast<long>(sample.scaling.outputY),
              static_cast<long>(sample.scaling.scale),
              sample.apply.projectedX, sample.apply.projectedY,
              static_cast<unsigned long>(sample.apply.projectionPhaseQ16),
              sample.input.directPacketSerial,
              sample.input.bufferedPacketSerial,
              sample.input.bufferDrainSerial,
              sample.input.bufferReadFailures);
    WriteText(line);
    ++g_sampleCount;
    if ((g_sampleCount % kFlushInterval) == 0) FlushFileBuffers(g_file);
    LeaveCriticalSection(&g_lock);
}

}  // namespace camera_trace
