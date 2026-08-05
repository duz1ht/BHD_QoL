#include <windows.h>
#include <shlwapi.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
constexpr uintptr_t kImageBase = 0x400000;
constexpr uintptr_t kPatchAddressMovEax = 0x52A934;
constexpr uintptr_t kPatchAddressWidthStore = 0x52A94C;
constexpr uintptr_t kPatchAddressHeightStore = 0x52A956;
constexpr uintptr_t kPatchAddressLoadingGameDynamicResolution = 0x4BC711;
constexpr uintptr_t kPatchAddressPollMouseInputCursorPosition = 0x567972;
constexpr uintptr_t kPatchAddressInitializeInGameSystemsClipCursor = 0x4623D2;
constexpr uintptr_t kPatchAddressClipCursorToViewPort = 0x4628D3;
constexpr uintptr_t kDynamicResolutionCodeCave = 0x5E4879;

HMODULE g_realDInput8 = nullptr;
bool g_logAppliedPatches = false;

struct PatchConfig {
    bool nvgResolution = true;
    bool dynamicResolution = true;
    bool mouseCursorFix = true;
    bool clipCursorFix = true;
    bool rawInputFix = true;
    bool logAppliedPatches = false;
};

struct ByteSpan {
    const unsigned char* bytes;
    size_t size;
};

struct Patch {
    const char* title;
    const char* description;
    uintptr_t virtualAddress;
    ByteSpan expected;
    ByteSpan replacement;
    bool allowZeroFilledExpected;
};

#define BYTE_SPAN(name) ByteSpan{name, sizeof(name)}

const unsigned char kNvgWidthImmediateExpected[] = {0xB8, 0x00, 0x02, 0x00, 0x00};
const unsigned char kNvgWidthImmediateReplacement[] = {0xB8, 0x00, 0x08, 0x00, 0x00};
const unsigned char kNvgStoredWidthExpected[] = {0xC7, 0x05, 0x8C, 0x49, 0xE0, 0x00, 0x00, 0x02, 0x00, 0x00};
const unsigned char kNvgStoredWidthReplacement[] = {0xC7, 0x05, 0x8C, 0x49, 0xE0, 0x00, 0x00, 0x08, 0x00, 0x00};
const unsigned char kNvgStoredHeightExpected[] = {0xC7, 0x05, 0x90, 0x49, 0xE0, 0x00, 0x00, 0x01, 0x00, 0x00};
const unsigned char kNvgStoredHeightReplacement[] = {0xC7, 0x05, 0x90, 0x49, 0xE0, 0x00, 0x00, 0x04, 0x00, 0x00};

const unsigned char kDynamicResolutionCodeCaveBytes[] = {
    0x89, 0x1D, 0xD8, 0xB3, 0x9F, 0x00, 0x8B, 0x1D, 0xC0, 0x72, 0x9F, 0x00,
    0xD1, 0xFB, 0x89, 0x1D, 0x22, 0xC3, 0x60, 0x00, 0x8B, 0x1D, 0xC4, 0x72,
    0x9F, 0x00, 0xD1, 0xFB, 0x89, 0x1D, 0x26, 0xC3, 0x60, 0x00, 0x8B, 0x1D,
    0xC0, 0x72, 0x9F, 0x00, 0xD1, 0xFB, 0xF7, 0xDB, 0x89, 0x1D, 0x2B, 0xC3,
    0x60, 0x00, 0x8B, 0x1D, 0xC4, 0x72, 0x9F, 0x00, 0xD1, 0xFB, 0xF7, 0xDB,
    0x89, 0x1D, 0x30, 0xC3, 0x60, 0x00, 0xE9, 0x57, 0x7E, 0xED, 0xFF, 0x03,
    0x05, 0x26, 0xC3, 0x60, 0x00, 0x50, 0x03, 0x0D, 0x22, 0xC3, 0x60, 0x00,
    0x51, 0xFF, 0x15, 0xD8, 0x12, 0x61, 0x00, 0x8B, 0x15, 0xE0, 0x55, 0xF6,
    0x00, 0x8B, 0x05, 0xE4, 0x55, 0xF6, 0x00, 0x03, 0x15, 0x2B, 0xC3, 0x60,
    0x00, 0x03, 0x05, 0x30, 0xC3, 0x60, 0x00, 0x89, 0x15, 0xEC, 0x55, 0xF6,
    0x00, 0xA3, 0xF0, 0x55, 0xF6, 0x00, 0x8B, 0x05, 0x22, 0xC3, 0x60, 0x00,
    0xA3, 0xE0, 0x55, 0xF6, 0x00, 0x8B, 0x05, 0x26, 0xC3, 0x60, 0x00, 0xA3,
    0xE4, 0x55, 0xF6, 0x00, 0xE9, 0xA8, 0x30, 0xF8, 0xFF, 0x8B, 0x05, 0xC0,
    0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xF8, 0x8B, 0x05, 0xC4, 0x72, 0x9F,
    0x00, 0x48, 0x89, 0x45, 0xFC, 0xE9, 0xAC, 0xDA, 0xE7, 0xFF, 0x8B, 0x05,
    0xC0, 0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xF8, 0x8B, 0x05, 0xC4, 0x72,
    0x9F, 0x00, 0x48, 0x89, 0x45, 0xFC, 0xE9, 0x9D, 0xDF, 0xE7, 0xFF,
};

const unsigned char kLoadingDynamicResolutionExpected[] = {0x89, 0x1D, 0xD8, 0xB3, 0x9F, 0x00};
const unsigned char kLoadingDynamicResolutionReplacement[] = {0xE9, 0x63, 0x81, 0x12, 0x00, 0x90};
const unsigned char kMouseCursorExpected[] = {0x05, 0xF0, 0x00, 0x00, 0x00};
const unsigned char kMouseCursorReplacement[] = {0xE9, 0x49, 0xCF, 0x07, 0x00};
const unsigned char kInitialClipCursorExpected[] = {0xC7, 0x45, 0xF8, 0x7F, 0x02, 0x00, 0x00};
const unsigned char kInitialClipCursorReplacement[] = {0xE9, 0x3B, 0x25, 0x18, 0x00, 0x90, 0x90};
const unsigned char kClipCursorExpected[] = {0xC7, 0x45, 0xF8, 0x7F, 0x02, 0x00, 0x00};
const unsigned char kClipCursorReplacement[] = {0xE9, 0x53, 0x20, 0x18, 0x00, 0x90, 0x90};

const Patch kNvgPatches[] = {
    {"Increase NVG viewport width immediate", "SetupNVGViewPort(): MOV EAX,0x200 -> MOV EAX,0x800",
     kPatchAddressMovEax, BYTE_SPAN(kNvgWidthImmediateExpected), BYTE_SPAN(kNvgWidthImmediateReplacement), false},
    {"Increase NVG viewport stored width", "SetupNVGViewPort(): MOV [00E0498C],0x200 -> MOV [00E0498C],0x800",
     kPatchAddressWidthStore, BYTE_SPAN(kNvgStoredWidthExpected), BYTE_SPAN(kNvgStoredWidthReplacement), false},
    {"Increase NVG viewport stored height", "SetupNVGViewPort(): MOV [00E04990],0x100 -> MOV [00E04990],0x400",
     kPatchAddressHeightStore, BYTE_SPAN(kNvgStoredHeightExpected), BYTE_SPAN(kNvgStoredHeightReplacement), false},
};

const Patch kDynamicResolutionCodeCavePatch = {
    "Install dynamic resolution code cave",
    "Writes resolution, mouse, and ClipCursor logic before redirecting fixed-resolution instructions.",
    kDynamicResolutionCodeCave,
    {nullptr, sizeof(kDynamicResolutionCodeCaveBytes)},
    BYTE_SPAN(kDynamicResolutionCodeCaveBytes),
    true,
};

const Patch kDynamicResolutionCorePatch = {
    "Compute additional dynamic resolution values",
    "LoadingGame(): MOV DWORD PTR [009FB3D8],EBX -> JMP 005E4879",
    kPatchAddressLoadingGameDynamicResolution,
    BYTE_SPAN(kLoadingDynamicResolutionExpected),
    BYTE_SPAN(kLoadingDynamicResolutionReplacement),
    false,
};

const Patch kMouseCursorPatch = {
    "Use dynamic resolution in SetCursorPosition",
    "PollMouseInput(): ADD EAX,0xF0 -> JMP 005E48C0",
    kPatchAddressPollMouseInputCursorPosition,
    BYTE_SPAN(kMouseCursorExpected),
    BYTE_SPAN(kMouseCursorReplacement),
    false,
};

const Patch kClipCursorPatches[] = {
    {"Use dynamic resolution in initial ClipCursor setup",
     "InitializeInGameSystems(): MOV DWORD PTR [EBP + -0x8],0x27F -> JMP 005E4912",
     kPatchAddressInitializeInGameSystemsClipCursor, BYTE_SPAN(kInitialClipCursorExpected),
     BYTE_SPAN(kInitialClipCursorReplacement), false},
    {"Use dynamic resolution when clipping cursor to viewport",
     "ClipCursorToViewPort(): MOV DWORD PTR [EBP + -0x8],0x27F -> JMP 005E492B",
     kPatchAddressClipCursorToViewPort, BYTE_SPAN(kClipCursorExpected), BYTE_SPAN(kClipCursorReplacement), false},
};

constexpr uintptr_t kFixMouseLogicAddress = 0x005E48C0;
constexpr uintptr_t kFixMouseLogicReturnAddress = 0x005679BA;
constexpr uintptr_t kCursorXAddress = 0x00F655E0;
constexpr uintptr_t kCursorYAddress = 0x00F655E4;
constexpr uintptr_t kRawMouseXAddress = 0x00F655EC;
constexpr uintptr_t kRawMouseYAddress = 0x00F655F0;
constexpr uintptr_t kVideoWidthAddress = 0x009F72C0;
constexpr uintptr_t kVideoHeightAddress = 0x009F72C4;

std::atomic<LONG> g_pendingRawMouseDx{0};
std::atomic<LONG> g_pendingRawMouseDy{0};
std::atomic<HWND> g_rawInputGameWindow{nullptr};
std::atomic<WNDPROC> g_originalWndProc{nullptr};
std::atomic<bool> g_wndProcHooked{false};
std::atomic<bool> g_rawInputRegistered{false};
std::atomic<bool> g_rawInputThreadStop{false};
std::atomic<bool> g_fixMouseLogicHooked{false};

using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
ClipCursorFn g_realClipCursor = nullptr;
std::atomic<bool> g_mouseCaptured{false};

unsigned char g_fixMouseOriginalBytes[6] = {};
void* g_fixMouseTrampoline = nullptr;
using FixMouseTrampolineFn = void(__stdcall*)();
FixMouseTrampolineFn g_fixMouseTrampolineFn = nullptr;

bool BytesEqual(const unsigned char* current, ByteSpan expected) {
    if (expected.bytes == nullptr) {
        return false;
    }
    for (size_t i = 0; i < expected.size; ++i) {
        if (current[i] != expected.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool BytesAreZeroFilled(const unsigned char* current, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (current[i] != 0x00) {
            return false;
        }
    }
    return true;
}

void LogPatchMessage(const char* title, const char* status) {
    if (!g_logAppliedPatches) {
        return;
    }

    char message[512] = {};
    wsprintfA(message, "BHD_QoL: %s: %s\n", status, title);
    OutputDebugStringA(message);
}

bool WriteBytes(uintptr_t virtualAddress, ByteSpan replacement) {
    auto* address = reinterpret_cast<unsigned char*>(virtualAddress);

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, replacement.size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    for (size_t i = 0; i < replacement.size; ++i) {
        address[i] = replacement.bytes[i];
    }
    FlushInstructionCache(GetCurrentProcess(), address, replacement.size);

    DWORD ignored = 0;
    VirtualProtect(address, replacement.size, oldProtect, &ignored);
    return true;
}

bool ApplyPatch(const Patch& patch) {
    auto* address = reinterpret_cast<unsigned char*>(patch.virtualAddress);
    if (BytesEqual(address, patch.replacement)) {
        LogPatchMessage(patch.title, "already applied");
        return true;
    }

    const bool expectedMatches = BytesEqual(address, patch.expected);
    const bool zeroFilledMatches = patch.allowZeroFilledExpected && BytesAreZeroFilled(address, patch.expected.size);
    if (!expectedMatches && !zeroFilledMatches) {
        LogPatchMessage(patch.title, "skipped unexpected bytes");
        return false;
    }

    if (!WriteBytes(patch.virtualAddress, patch.replacement)) {
        LogPatchMessage(patch.title, "failed VirtualProtect/write");
        return false;
    }

    LogPatchMessage(patch.title, "applied");
    return true;
}

template <size_t Count>
bool ApplyPatchGroup(const Patch (&patches)[Count]) {
    bool appliedAll = true;
    for (const auto& patch : patches) {
        appliedAll = ApplyPatch(patch) && appliedAll;
    }
    return appliedAll;
}

LONG ReadGameLong(uintptr_t address) {
    return *reinterpret_cast<volatile LONG*>(address);
}

void WriteGameLong(uintptr_t address, LONG value) {
    *reinterpret_cast<volatile LONG*>(address) = value;
}

bool CenterCursorOnGameClient(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }

    RECT clientRect = {};
    if (!GetClientRect(hwnd, &clientRect)) {
        return false;
    }

    POINT center = {};
    center.x = (clientRect.right - clientRect.left) / 2;
    center.y = (clientRect.bottom - clientRect.top) / 2;
    if (!ClientToScreen(hwnd, &center)) {
        return false;
    }

    return SetCursorPos(center.x, center.y) != FALSE;
}

bool RegisterRawMouseInput(HWND hwnd) {
    RAWINPUTDEVICE rawMouse = {};
    rawMouse.usUsagePage = 0x01;
    rawMouse.usUsage = 0x02;
    rawMouse.dwFlags = RIDEV_INPUTSINK;
    rawMouse.hwndTarget = hwnd;

    return RegisterRawInputDevices(&rawMouse, 1, sizeof(rawMouse)) != FALSE;
}

LRESULT CALLBACK RawInputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INPUT) {
        UINT size = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

        if (size > 0 && size < 4096) {
            std::vector<BYTE> buffer(size);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size,
                                sizeof(RAWINPUTHEADER)) == size) {
                const auto* rawInput = reinterpret_cast<const RAWINPUT*>(buffer.data());
                if (rawInput->header.dwType == RIM_TYPEMOUSE) {
                    const LONG dx = rawInput->data.mouse.lLastX;
                    const LONG dy = rawInput->data.mouse.lLastY;
                    if (dx != 0 || dy != 0) {
                        g_pendingRawMouseDx.fetch_add(dx, std::memory_order_relaxed);
                        g_pendingRawMouseDy.fetch_add(dy, std::memory_order_relaxed);
                    }
                }
            }
        }

        return 0;
    }

    const WNDPROC originalWndProc = g_originalWndProc.load(std::memory_order_relaxed);
    return originalWndProc != nullptr ? CallWindowProcW(originalWndProc, hwnd, message, wParam, lParam)
                                      : DefWindowProcW(hwnd, message, wParam, lParam);
}

struct WindowCandidate {
    HWND hwnd = nullptr;
    int area = 0;
};

BOOL CALLBACK FindGameWindowCallback(HWND hwnd, LPARAM lParam) {
    auto* best = reinterpret_cast<WindowCandidate*>(lParam);

    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(hwnd, &windowProcessId);
    if (windowProcessId != GetCurrentProcessId()) {
        return TRUE;
    }

    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    if ((GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }

    RECT clientRect = {};
    if (!GetClientRect(hwnd, &clientRect)) {
        return TRUE;
    }

    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width < 400 || height < 300) {
        return TRUE;
    }

    const int area = width * height;
    if (area > best->area) {
        best->area = area;
        best->hwnd = hwnd;
    }

    return TRUE;
}

HWND FindBestGameWindow() {
    WindowCandidate best = {};
    EnumWindows(FindGameWindowCallback, reinterpret_cast<LPARAM>(&best));
    return best.hwnd;
}

bool HookGameWindowProcOnce(HWND hwnd) {
    if (hwnd == nullptr || g_wndProcHooked.load(std::memory_order_relaxed)) {
        return false;
    }

    SetLastError(0);
    const LONG_PTR previous = SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RawInputWndProc));
    if (previous == 0 && GetLastError() != 0) {
        return false;
    }

    g_originalWndProc.store(reinterpret_cast<WNDPROC>(previous), std::memory_order_relaxed);
    g_rawInputGameWindow.store(hwnd, std::memory_order_relaxed);
    g_wndProcHooked.store(true, std::memory_order_relaxed);
    return true;
}

bool PatchIatByFunctionName(void* moduleBase, const char* functionName, void* replacement, void** original) {
    if (moduleBase == nullptr || functionName == nullptr || replacement == nullptr) {
        return false;
    }

    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(moduleBase) + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const auto& importDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDirectory.VirtualAddress == 0 || importDirectory.Size == 0) {
        return false;
    }

    auto* importDescriptor =
        reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(reinterpret_cast<BYTE*>(moduleBase) + importDirectory.VirtualAddress);
    for (; importDescriptor->Name != 0; ++importDescriptor) {
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(reinterpret_cast<BYTE*>(moduleBase) + importDescriptor->FirstThunk);
        auto* names = importDescriptor->OriginalFirstThunk != 0
                          ? reinterpret_cast<IMAGE_THUNK_DATA*>(reinterpret_cast<BYTE*>(moduleBase) +
                                                                importDescriptor->OriginalFirstThunk)
                          : iat;

        for (; iat->u1.Function != 0; ++iat, ++names) {
            if ((names->u1.Ordinal & IMAGE_ORDINAL_FLAG) != 0) {
                continue;
            }

            auto* importByName =
                reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(reinterpret_cast<BYTE*>(moduleBase) + names->u1.AddressOfData);
            if (lstrcmpA(reinterpret_cast<const char*>(importByName->Name), functionName) != 0) {
                continue;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                return false;
            }

            void* previous = reinterpret_cast<void*>(static_cast<uintptr_t>(iat->u1.Function));
            iat->u1.Function = reinterpret_cast<uintptr_t>(replacement);

            DWORD ignored = 0;
            VirtualProtect(&iat->u1.Function, sizeof(void*), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &iat->u1.Function, sizeof(void*));

            if (original != nullptr) {
                *original = previous;
            }
            return true;
        }
    }

    return false;
}

BOOL WINAPI HookClipCursor(const RECT* rect) {
    g_mouseCaptured.store(rect != nullptr, std::memory_order_relaxed);
    return g_realClipCursor != nullptr ? g_realClipCursor(rect) : FALSE;
}

bool InstallClipCursorIatHook() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        user32 = LoadLibraryW(L"user32.dll");
    }
    if (user32 == nullptr) {
        return false;
    }

    g_realClipCursor = reinterpret_cast<ClipCursorFn>(GetProcAddress(user32, "ClipCursor"));
    if (g_realClipCursor == nullptr) {
        return false;
    }

    void* original = nullptr;
    const bool patched = PatchIatByFunctionName(GetModuleHandleW(nullptr), "ClipCursor",
                                                reinterpret_cast<void*>(&HookClipCursor), &original);
    if (patched && original != nullptr) {
        g_realClipCursor = reinterpret_cast<ClipCursorFn>(original);
    }
    return patched;
}

void* MakeTrampoline(const unsigned char* originalBytes, size_t originalLength, void* returnAddress) {
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                                           PAGE_EXECUTE_READWRITE));
    if (memory == nullptr) {
        return nullptr;
    }

    std::memcpy(memory, originalBytes, originalLength);
    unsigned char* jump = memory + originalLength;
    jump[0] = 0xE9;
    *reinterpret_cast<int32_t*>(jump + 1) = static_cast<int32_t>(static_cast<unsigned char*>(returnAddress) - (jump + 5));
    FlushInstructionCache(GetCurrentProcess(), memory, 64);
    return memory;
}

bool WriteRelativeJump5(void* address, void* destination, int extraNops) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, 5 + extraNops, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    auto* bytes = static_cast<unsigned char*>(address);
    bytes[0] = 0xE9;
    *reinterpret_cast<int32_t*>(bytes + 1) =
        static_cast<int32_t>(static_cast<unsigned char*>(destination) - (bytes + 5));
    for (int i = 0; i < extraNops; ++i) {
        bytes[5 + i] = 0x90;
    }

    DWORD ignored = 0;
    VirtualProtect(address, 5 + extraNops, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), address, 5 + extraNops);
    return true;
}

bool OnFixMouseLogic() {
    if (!g_mouseCaptured.load(std::memory_order_relaxed)) {
        return false;
    }

    const LONG dx = g_pendingRawMouseDx.exchange(0, std::memory_order_relaxed);
    const LONG dy = g_pendingRawMouseDy.exchange(0, std::memory_order_relaxed);

    CenterCursorOnGameClient(g_rawInputGameWindow.load(std::memory_order_relaxed));

    const LONG halfWidth = ReadGameLong(kVideoWidthAddress) >> 1;
    const LONG halfHeight = ReadGameLong(kVideoHeightAddress) >> 1;
    WriteGameLong(kRawMouseXAddress, dx);
    WriteGameLong(kRawMouseYAddress, dy);
    WriteGameLong(kCursorXAddress, halfWidth);
    WriteGameLong(kCursorYAddress, halfHeight);
    return true;
}

__declspec(naked) void FixMouseLogicDetour() {
    __asm {
        pushad
        pushfd
        call OnFixMouseLogic
        test eax, eax
        jz not_captured

        popfd
        popad
        mov eax, kFixMouseLogicReturnAddress
        jmp eax

    not_captured:
        popfd
        popad
        jmp g_fixMouseTrampolineFn
    }
}

bool FixMouseLogicBytesLookRight() {
    auto* bytes = reinterpret_cast<unsigned char*>(kFixMouseLogicAddress);
    __try {
        return bytes[0] == 0x03 && bytes[1] == 0x05 &&
               *reinterpret_cast<uint32_t*>(bytes + 2) == 0x0060C326;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InstallFixMouseLogicHookOnce() {
    if (g_fixMouseLogicHooked.load(std::memory_order_relaxed)) {
        return true;
    }

    if (!FixMouseLogicBytesLookRight()) {
        return false;
    }

    auto* target = reinterpret_cast<unsigned char*>(kFixMouseLogicAddress);
    std::memcpy(g_fixMouseOriginalBytes, target, sizeof(g_fixMouseOriginalBytes));

    g_fixMouseTrampoline =
        MakeTrampoline(g_fixMouseOriginalBytes, sizeof(g_fixMouseOriginalBytes),
                       reinterpret_cast<void*>(kFixMouseLogicAddress + sizeof(g_fixMouseOriginalBytes)));
    if (g_fixMouseTrampoline == nullptr) {
        return false;
    }

    g_fixMouseTrampolineFn = reinterpret_cast<FixMouseTrampolineFn>(g_fixMouseTrampoline);
    if (!WriteRelativeJump5(target, reinterpret_cast<void*>(&FixMouseLogicDetour), 1)) {
        return false;
    }

    g_fixMouseLogicHooked.store(true, std::memory_order_relaxed);
    return true;
}

DWORD WINAPI RawInputMonitorThread(LPVOID) {
    bool clipCursorHooked = false;

    while (!g_rawInputThreadStop.load(std::memory_order_relaxed)) {
        HWND gameWindow = FindBestGameWindow();
        if (gameWindow != nullptr) {
            g_rawInputGameWindow.store(gameWindow, std::memory_order_relaxed);
            HookGameWindowProcOnce(gameWindow);

            if (!g_rawInputRegistered.load(std::memory_order_relaxed) && RegisterRawMouseInput(gameWindow)) {
                g_rawInputRegistered.store(true, std::memory_order_relaxed);
            }

            if (!clipCursorHooked) {
                clipCursorHooked = InstallClipCursorIatHook();
            }

            InstallFixMouseLogicHookOnce();
        }

        Sleep(200);
    }

    return 0;
}

void StartRawInputFix() {
    g_rawInputThreadStop.store(false, std::memory_order_relaxed);
    HANDLE thread = CreateThread(nullptr, 0, RawInputMonitorThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    }
}

bool BoolFromIni(const wchar_t* path, const wchar_t* key, bool defaultValue) {
    return GetPrivateProfileIntW(L"PatchGroups", key, defaultValue ? 1 : 0, path) != 0;
}

bool DebugBoolFromIni(const wchar_t* path, const wchar_t* key, bool defaultValue) {
    return GetPrivateProfileIntW(L"Debug", key, defaultValue ? 1 : 0, path) != 0;
}

void BuildIniPath(wchar_t* iniPath, DWORD size) {
    iniPath[0] = L'\0';
    const DWORD length = GetModuleFileNameW(nullptr, iniPath, size);
    if (length == 0 || length >= size) {
        lstrcpynW(iniPath, L".\\dinput8.ini", size);
        return;
    }

    PathRemoveFileSpecW(iniPath);
    PathAppendW(iniPath, L"dinput8.ini");
}

PatchConfig LoadPatchConfig() {
    wchar_t iniPath[MAX_PATH] = {};
    BuildIniPath(iniPath, MAX_PATH);

    PatchConfig config = {};
    config.nvgResolution = BoolFromIni(iniPath, L"NVGResolution", config.nvgResolution);
    config.dynamicResolution = BoolFromIni(iniPath, L"DynamicResolution", config.dynamicResolution);
    config.mouseCursorFix = BoolFromIni(iniPath, L"MouseCursorFix", config.mouseCursorFix);
    config.clipCursorFix = BoolFromIni(iniPath, L"ClipCursorFix", config.clipCursorFix);
    config.rawInputFix = BoolFromIni(iniPath, L"RawInputFix", config.rawInputFix);
    config.logAppliedPatches = DebugBoolFromIni(iniPath, L"LogAppliedPatches", config.logAppliedPatches);
    return config;
}

void ApplyBhdPatches() {
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase) {
        return;
    }

    const PatchConfig config = LoadPatchConfig();
    g_logAppliedPatches = config.logAppliedPatches;

    if (config.nvgResolution) {
        ApplyPatchGroup(kNvgPatches);
    }

    const bool needsDynamicResolutionCodeCave =
        config.dynamicResolution || config.mouseCursorFix || config.clipCursorFix || config.rawInputFix;
    if (!needsDynamicResolutionCodeCave) {
        return;
    }

    if (!ApplyPatch(kDynamicResolutionCodeCavePatch)) {
        return;
    }

    if (config.dynamicResolution || config.mouseCursorFix) {
        ApplyPatch(kDynamicResolutionCorePatch);
    }
    if (config.mouseCursorFix || config.rawInputFix) {
        ApplyPatch(kMouseCursorPatch);
    }
    if (config.clipCursorFix) {
        ApplyPatchGroup(kClipCursorPatches);
    }
    if (config.rawInputFix) {
        StartRawInputFix();
    }
}

HMODULE LoadRealDInput8() {
    if (g_realDInput8 != nullptr) {
        return g_realDInput8;
    }

    wchar_t systemPath[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return nullptr;
    }

    if (!PathAppendW(systemPath, L"dinput8.dll")) {
        return nullptr;
    }

    g_realDInput8 = LoadLibraryW(systemPath);
    return g_realDInput8;
}

template <typename Function>
Function GetRealProc(const char* name) {
    HMODULE realDll = LoadRealDInput8();
    if (realDll == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Function>(GetProcAddress(realDll, name));
}
}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riidltf,
                                              LPVOID* out, LPUNKNOWN outer) {
    using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto real = GetRealProc<DirectInput8CreateFn>("DirectInput8Create");
    if (real == nullptr) {
        return E_FAIL;
    }
    return real(hinst, version, riidltf, out, outer);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllCanUnloadNow");
    return real != nullptr ? real() : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* out) {
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    const auto real = GetRealProc<Fn>("DllGetClassObject");
    return real != nullptr ? real(rclsid, riid, out) : E_FAIL;
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllRegisterServer");
    return real != nullptr ? real() : E_FAIL;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllUnregisterServer");
    return real != nullptr ? real() : E_FAIL;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ApplyBhdPatches();
        LoadRealDInput8();
    } else if (reason == DLL_PROCESS_DETACH) {
        g_rawInputThreadStop.store(true, std::memory_order_relaxed);
        if (g_fixMouseTrampoline != nullptr) {
            VirtualFree(g_fixMouseTrampoline, 0, MEM_RELEASE);
            g_fixMouseTrampoline = nullptr;
            g_fixMouseTrampolineFn = nullptr;
        }
        if (g_realDInput8 != nullptr) {
            FreeLibrary(g_realDInput8);
            g_realDInput8 = nullptr;
        }
    }
    return TRUE;
}
