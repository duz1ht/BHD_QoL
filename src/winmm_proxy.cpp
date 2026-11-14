#include <windows.h>

#include <cstdint>

#include "bhd_patches.h"

static_assert(sizeof(void*) == 4, "The WinMM proxy requires an x86 build.");

namespace {
struct Export {
    WORD ordinal;
    const char* name;
};

constexpr Export kExports[] = {
#define WINMM_EXPORT(ordinal, symbol, name) {ordinal, name},
#include "winmm_exports.inc"
#undef WINMM_EXPORT
};
constexpr size_t kExportCount = sizeof(kExports) / sizeof(kExports[0]);
// Ordinals start at 2 and are contiguous in the x86 WinMM export surface.
static_assert(kExports[0].ordinal == 2);
static_assert(kExports[kExportCount - 1].ordinal == kExportCount + 1);

PVOID volatile g_realWinmm = nullptr;
PVOID volatile g_targets[kExportCount] = {};

[[noreturn]] void MissingExport(DWORD status, WORD ordinal) {
    const ULONG_PTR argument = ordinal;
    RaiseException(status, EXCEPTION_NONCONTINUABLE, 1, &argument);
    TerminateProcess(GetCurrentProcess(), status);
    // RaiseException and TerminateProcess cannot return in normal execution.
    for (;;) {}
}

HMODULE LoadRealWinmm() {
    const auto existing = static_cast<HMODULE>(
        InterlockedCompareExchangePointer(&g_realWinmm, nullptr, nullptr));
    if (existing != nullptr) return existing;

    wchar_t path[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    constexpr wchar_t suffix[] = L"\\winmm.dll";
    if (length == 0 || length + sizeof(suffix) / sizeof(suffix[0]) > MAX_PATH) {
        return nullptr;
    }
    CopyMemory(path + length, suffix, sizeof(suffix));
    // An absolute system path prevents loading the proxy again. WOW64 selects
    // the 32-bit system library for this 32-bit process.
    HMODULE loaded = LoadLibraryW(path);
    if (loaded == nullptr) return nullptr;

    const auto winner = static_cast<HMODULE>(
        InterlockedCompareExchangePointer(&g_realWinmm, loaded, nullptr));
    if (winner != nullptr) {
        FreeLibrary(loaded);  // balance only this competing load
        return winner;
    }
    // Retain the winning module for the process lifetime. There is no detach
    // wait or FreeLibrary call while the loader lock is held.
    return loaded;
}
}  // namespace

extern "C"
#if defined(__GNUC__) && defined(__i386__)
// Win32 callers only guarantee four-byte stack alignment. Realign the C++
// resolver when MinGW uses a stronger alignment internally.
__attribute__((force_align_arg_pointer))
#endif
FARPROC __cdecl ResolveWinmmTarget(unsigned int index, uintptr_t caller) {
    const DWORD savedError = GetLastError();
    if (index >= kExportCount) MissingExport(0xC0000139, 0);
    const Export& entry = kExports[index];
    auto target = reinterpret_cast<FARPROC>(
        InterlockedCompareExchangePointer(&g_targets[index], nullptr, nullptr));
    if (target == nullptr) {
        HMODULE real = LoadRealWinmm();
        if (real == nullptr) MissingExport(0xC0000135, entry.ordinal);
        target = GetProcAddress(real, entry.name != nullptr ? entry.name :
                                MAKEINTRESOURCEA(entry.ordinal));
        if (target == nullptr) MissingExport(0xC0000139, entry.ordinal);
        InterlockedExchangePointer(&g_targets[index], reinterpret_cast<PVOID>(target));
    }
    // These are the APIs used in the game's verified startup call sites.
    // Other exports and calls from other modules remain pure forwarders.
    if (entry.ordinal == 149 || entry.ordinal == 151) {
        bhd_patches::InitializeForWinmmCall(caller);
    }
    SetLastError(savedError);
    return target;
}

// Preserve the original register/flag state and stack, then tail-call the real
// function. This supports every original stdcall signature, callback and return
// type without redeclaring the multimedia API or altering its arguments.
#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void WinmmDispatch() {
    __asm { pushfd }
    __asm { pushad }
    __asm { mov eax, [esp + 36] }
    __asm { mov edx, [esp + 40] }
    __asm { push edx }
    __asm { push eax }
    __asm { call ResolveWinmmTarget }
    __asm { add esp, 8 }
    __asm { mov [esp + 36], eax }
    __asm { popad }
    __asm { popfd }
    __asm { lea esp, [esp + 4] }
    __asm { jmp dword ptr [esp - 4] }
}
#define WINMM_EXPORT(ordinal, symbol, name) \
    extern "C" __declspec(naked) void WinmmProxy_##symbol() { \
        __asm { push (ordinal - 2) } \
        __asm { jmp WinmmDispatch } \
    }
#elif defined(__GNUC__) && defined(__i386__)
extern "C" __attribute__((naked)) void WinmmDispatch() {
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl 36(%esp), %eax\n\t"
        "movl 40(%esp), %edx\n\t"
        "pushl %edx\n\t"
        "pushl %eax\n\t"
        "call _ResolveWinmmTarget\n\t"
        "addl $8, %esp\n\t"
        "movl %eax, 36(%esp)\n\t"
        "popal\n\t"
        "popfl\n\t"
        "leal 4(%esp), %esp\n\t"
        "jmp *-4(%esp)\n\t");
}
#define WINMM_STRINGIFY_INNER(value) #value
#define WINMM_STRINGIFY(value) WINMM_STRINGIFY_INNER(value)
#define WINMM_EXPORT(ordinal, symbol, name) \
    extern "C" __attribute__((naked)) void WinmmProxy_##symbol() { \
        __asm__ __volatile__("pushl $(" WINMM_STRINGIFY(ordinal) " - 2)\n\t" \
                             "jmp _WinmmDispatch\n\t"); \
    }
#else
#error Unsupported compiler: the WinMM forwarding stubs require x86 MSVC or MinGW.
#endif

#include "winmm_exports.inc"
#undef WINMM_EXPORT

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
