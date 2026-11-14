#include <windows.h>
#include <mmsystem.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "WinMM proxy regression: %s (error %lu)\n", message, GetLastError());
        std::exit(1);
    }
}

template <typename Function>
Function Proc(HMODULE module, const char* name) {
    const auto proc = GetProcAddress(module, name);
    Check(proc != nullptr, name);
    return reinterpret_cast<Function>(proc);
}

DWORD WINAPI ReadClock(void*) {
    DWORD previous = timeGetTime();
    for (unsigned int i = 0; i < 1000; ++i) {
        const DWORD next = timeGetTime();
        if (next - previous > 10000) return 1;
        previous = next;
    }
    return 0;
}

volatile LONG g_timerCallbacks = 0;
void CALLBACK TimerCallback(UINT, UINT, DWORD_PTR user, DWORD_PTR, DWORD_PTR) {
    if (user == 0x1234) InterlockedIncrement(&g_timerCallbacks);
}
}  // namespace

int main(int argc, char** argv) {
    Check(argc == 2, "expected the built proxy path");
    HMODULE proxy = GetModuleHandleW(L"winmm.dll");
    Check(proxy != nullptr, "WinMM must be loaded automatically via static imports");
    char loaded[MAX_PATH] = {};
    Check(GetModuleFileNameA(proxy, loaded, MAX_PATH) != 0, "proxy path");
    char expectedPath[MAX_PATH] = {};
    Check(GetFullPathNameA(argv[1], MAX_PATH, expectedPath, nullptr) != 0, "expected proxy path");
    for (char& ch : loaded) if (ch == '/') ch = '\\';
    for (char& ch : expectedPath) if (ch == '/') ch = '\\';
    if (_stricmp(loaded, expectedPath) != 0) {
        std::fprintf(stderr, "Loaded: %s\nExpected: %s\n", loaded, expectedPath);
        Check(false, "loaded the built proxy from the executable directory");
    }

    // Race the first forwarded calls to exercise thread-safe native loading and
    // cached target resolution. This executable must never trigger game patches.
    HANDLE workers[8] = {};
    for (auto& worker : workers) {
        worker = CreateThread(nullptr, 0, ReadClock, nullptr, 0, nullptr);
        Check(worker != nullptr, "clock worker");
    }
    Check(WaitForMultipleObjects(8, workers, TRUE, 10000) == WAIT_OBJECT_0, "clock workers completed");
    for (HANDLE worker : workers) {
        DWORD result = 1;
        Check(GetExitCodeThread(worker, &result) && result == 0, "forwarded clock progression");
        CloseHandle(worker);
    }

    wchar_t systemPath[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
    Check(length > 0 && length + 11 < MAX_PATH, "system directory");
    std::wcscpy(systemPath + length, L"\\winmm.dll");
    HMODULE system = LoadLibraryW(systemPath);
    Check(system != nullptr && system != proxy, "separate native system WinMM");

    unsigned int exported = 0;
#define WINMM_EXPORT(ordinal, symbol, name) \
    { \
        const char* key = name; \
        const auto byOrdinal = GetProcAddress(proxy, MAKEINTRESOURCEA(ordinal)); \
        Check(byOrdinal != nullptr, "export ordinal " #ordinal); \
        if (key != nullptr) Check(GetProcAddress(proxy, key) == byOrdinal, #symbol); \
        ++exported; \
    }
#include "winmm_exports.inc"
#undef WINMM_EXPORT
    Check(exported == 193, "complete named and ordinal export surface");
    Check(GetProcAddress(proxy, "DirectInput8Create") == nullptr, "proxy does not export DirectInput");

    TIMECAPS actual = {}, expected = {};
    const auto nativeCaps = Proc<decltype(&timeGetDevCaps)>(system, "timeGetDevCaps");
    Check(timeGetDevCaps(&actual, sizeof(actual)) == nativeCaps(&expected, sizeof(expected)), "timer caps result");
    Check(actual.wPeriodMin == expected.wPeriodMin && actual.wPeriodMax == expected.wPeriodMax, "timer caps output arguments");
    const auto nativeTime = Proc<decltype(&timeGetTime)>(system, "timeGetTime");
    const DWORD before = nativeTime();
    const DWORD throughProxy = timeGetTime();
    const DWORD after = nativeTime();
    Check(throughProxy - before <= after - before, "native clock value");

    const MMRESULT period = timeBeginPeriod(actual.wPeriodMin);
    Check(period == TIMERR_NOERROR, "begin timer period");
    const UINT timer = timeSetEvent(5, actual.wPeriodMin, TimerCallback, 0x1234,
                                  TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
    Check(timer != 0, "timer callback and argument forwarding");
    const DWORD deadline = GetTickCount();
    while (InterlockedCompareExchange(&g_timerCallbacks, 0, 0) == 0 &&
           GetTickCount() - deadline < 2000) Sleep(1);
    Check(timeKillEvent(timer) == TIMERR_NOERROR, "stop timer");
    Check(timeEndPeriod(actual.wPeriodMin) == TIMERR_NOERROR, "balance timer period");
    Check(g_timerCallbacks > 0, "native timer called the original callback with user data");

    Check(joyGetNumDevs() == Proc<decltype(&joyGetNumDevs)>(system, "joyGetNumDevs")(), "joystick count");
    JOYCAPSA joyActual = {}, joyExpected = {};
    const auto nativeJoyCaps = Proc<decltype(&joyGetDevCapsA)>(system, "joyGetDevCapsA");
    Check(joyGetDevCapsA(0xFFFF, &joyActual, sizeof(joyActual)) ==
          nativeJoyCaps(0xFFFF, &joyExpected, sizeof(joyExpected)), "joystick caps arguments");
    JOYINFOEX posActual = {}, posExpected = {};
    posActual.dwSize = posExpected.dwSize = sizeof(JOYINFOEX);
    posActual.dwFlags = posExpected.dwFlags = JOY_RETURNALL;
    Check(joyGetPosEx(0xFFFF, &posActual) ==
          Proc<decltype(&joyGetPosEx)>(system, "joyGetPosEx")(0xFFFF, &posExpected), "joystick position arguments");

    // Check APIs used by multimedia dependencies beyond the game's nine imports.
    const auto proxyWaveCount = Proc<decltype(&waveOutGetNumDevs)>(proxy, "waveOutGetNumDevs");
    const auto nativeWaveCount = Proc<decltype(&waveOutGetNumDevs)>(system, "waveOutGetNumDevs");
    // Warm the native device enumeration before comparing last-error behavior;
    // its first internal initialization can itself change the thread's error.
    nativeWaveCount();
    SetLastError(0x12345678);
    const UINT nativeCount = nativeWaveCount();
    const DWORD nativeError = GetLastError();
    SetLastError(0x12345678);
    const UINT proxyCount = proxyWaveCount();
    const DWORD proxyError = GetLastError();
    Check(proxyCount == nativeCount && proxyError == nativeError, "wave output and last-error forwarding");
    Check(Proc<decltype(&waveInGetNumDevs)>(proxy, "waveInGetNumDevs")() ==
          Proc<decltype(&waveInGetNumDevs)>(system, "waveInGetNumDevs")(), "wave input forwarding");
    Check(Proc<decltype(&mmioStringToFOURCCA)>(proxy, "mmioStringToFOURCCA")("WAVE", 0) ==
          Proc<decltype(&mmioStringToFOURCCA)>(system, "mmioStringToFOURCCA")("WAVE", 0), "multimedia file API forwarding");
    FreeLibrary(system);
    std::puts("WinMM automatic loading, 193 exports, concurrent calls and native forwarding passed.");
    return 0;
}
