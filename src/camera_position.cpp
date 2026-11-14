#include "camera_position.h"

#include <windows.h>

#include <cstring>

#include "camera_position_hook.h"
#include "camera_position_math.h"

namespace camera_position {
namespace {
constexpr uintptr_t kImageBase = 0x00400000;
constexpr uintptr_t kLocalPlayer = 0x0096C290;
constexpr uintptr_t kPool = 0x00715900;
constexpr uintptr_t kCameraTarget = 0x007F2DD4;
constexpr uintptr_t kCameraMode = 0x007F2DD0;
constexpr uintptr_t kMouseEnabled = 0x009F20DC;
constexpr uintptr_t kPaused = 0x007C72BC;
constexpr uintptr_t kSpecialClock = 0x00A033C0;
constexpr uintptr_t kFastStep = 0x00A0339C;
constexpr uintptr_t kSlowStep = 0x009F374C;
constexpr uintptr_t kGameWindow = 0x00F654FC;
constexpr uint32_t kEmptyCallback = 0x005382E0;

uintptr_t g_base = 0;
bool g_enabled = false;
bool g_attempted = false;
PositionHistory g_history;
struct Clock {
    bool valid = false;
    uint32_t remainder = 0, fastStep = 0, slowStep = 0;
    DWORD observedAt = 0;
} g_clock;

template <typename T> T Read(uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}
template <typename T> T ReadGame(uintptr_t nativeAddress) {
    return Read<T>(g_base + nativeAddress - kImageBase);
}
bool EngineAllowed() {
    const HWND window = ReadGame<HWND>(kGameWindow);
    return ReadGame<uint32_t>(kCameraMode) == 0 && ReadGame<uint32_t>(kMouseEnabled) != 0 &&
           ReadGame<uint32_t>(kPaused) == 0 && ReadGame<uint32_t>(kSpecialClock) == 0 &&
           window != nullptr && GetForegroundWindow() == window;
}
bool ReadPlayer(Identity* identity, Position* position) {
    const uintptr_t actor = ReadGame<uintptr_t>(kLocalPlayer);
    const uintptr_t pool = ReadGame<uintptr_t>(kPool);
    // Validate the slot BEFORE dereferencing actor memory. Only the on-foot
    // local OITEM is supported; world actors and vehicle cameras stay native.
    if (actor == 0 || pool == 0 || actor < pool ||
        (actor - pool) % 0x29C != 0 || (actor - pool) / 0x29C >= 256 ||
        ReadGame<uintptr_t>(kCameraTarget) != actor) return false;
    Identity key = {actor, pool, Read<uint32_t>(actor), Read<uint32_t>(actor + 0x24),
                    Read<uint32_t>(actor + 0x4C), Read<uint32_t>(actor + 0x13C),
                    Read<uint32_t>(actor + 0x228), Read<uint32_t>(actor + 0x224)};
    if (key.definitionIndex == 0 || key.definition == 0 || key.mount != 0) return false;
    *identity = key;
    *position = {Read<int32_t>(actor + 8), Read<int32_t>(actor + 12), Read<int32_t>(actor + 16)};
    return true;
}
bool HasCallback(uint32_t callback) { return callback != 0 && callback != g_base + kEmptyCallback - kImageBase; }
void Invalidate() {
    g_clock.valid = false;
    g_history.Reset();
}

void __cdecl CapturePosition(uintptr_t actor, uint32_t producer) {
    if (!g_enabled) return;
    Identity identity;
    Position position;
    if (!EngineAllowed() || !ReadPlayer(&identity, &position)) { Invalidate(); return; }
    if (actor != identity.actor) return;
    const bool fast = HasCallback(identity.fastCallback);
    if (producer == 1 ? !fast : (fast || !HasCallback(identity.slowCallback))) return;
    const Cadence cadence = producer == 1 ? Cadence::Fast : Cadence::Slow;
    // The fast callback runs BEFORE the scheduler increments kFastStep. Slow
    // logic has already incremented kSlowStep before its producer callbacks.
    // Normalize fast samples to the post-increment serial captured at render.
    const uint32_t step = producer == 1 ? ReadGame<uint32_t>(kFastStep) + 1 : ReadGame<uint32_t>(kSlowStep);
    g_history.Observe(identity, position, step, cadence);
}

void __cdecl CapturePhase(uint32_t remainder) {
    if (!g_enabled) return;
    const DWORD now = GetTickCount();
    const uint32_t fast = ReadGame<uint32_t>(kFastStep);
    const uint32_t slow = ReadGame<uint32_t>(kSlowStep);
    Identity identity;
    Position position;
    if (remainder >= 64 || !EngineAllowed() || !ReadPlayer(&identity, &position)) {
        Invalidate();
        return;
    }
    if (g_clock.valid && (now - g_clock.observedAt > 250 ||
                         fast - g_clock.fastStep > 64 || slow - g_clock.slowStep > 16)) {
        g_history.Reset();
    }
    g_clock = {true, remainder, fast, slow, now};
}

struct Hook {
    uintptr_t address;
    unsigned char expected[5];
    uintptr_t callback;
    uint32_t producer;
    bool phase;
    unsigned char replacement[5] = {};
    unsigned char* stub = nullptr;
    bool installed = false;
};

bool WriteCode(uintptr_t address, const unsigned char* code, size_t count) {
    DWORD protection = 0;
    void* target = reinterpret_cast<void*>(address);
    if (!VirtualProtect(target, count, PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(target, code, count);
    FlushInstructionCache(GetCurrentProcess(), target, count);
    DWORD ignored = 0;
    VirtualProtect(target, count, protection, &ignored);
    return true;
}
bool InstallHooks() {
    Hook hooks[] = {
        {0x004BF08F, {0xA1, 0x50, 0x22, 0xA0, 0x00}, reinterpret_cast<uintptr_t>(&CapturePhase), 0, true},
        {0x004803EF, {0xA1, 0x10, 0x3F, 0x68, 0x00}, reinterpret_cast<uintptr_t>(&CapturePosition), 1, false},
        {0x00472E63, {0xA1, 0x10, 0x3F, 0x68, 0x00}, reinterpret_cast<uintptr_t>(&CapturePosition), 2, false},
    };
    for (auto& hook : hooks) {
        hook.address += g_base - kImageBase;
        uint32_t operand = 0;
        std::memcpy(&operand, hook.expected + 1, 4);
        operand += static_cast<uint32_t>(g_base - kImageBase);
        std::memcpy(hook.expected + 1, &operand, 4);
    }
    // Preflight ALL signatures, then prepare ALL stubs before changing the EXE.
    for (const auto& hook : hooks) {
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(reinterpret_cast<const void*>(hook.address), &memory, sizeof(memory)) == 0 ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
            hook.address + 5 > reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize) return false;
        if (std::memcmp(reinterpret_cast<const void*>(hook.address), hook.expected, 5) != 0) return false;
    }
    bool prepared = true;
    for (auto& hook : hooks) {
        hook.stub = static_cast<unsigned char*>(VirtualAlloc(nullptr, 128,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (hook.stub == nullptr) { prepared = false; break; }
        const size_t size = BuildCaptureStub(hook.stub, reinterpret_cast<uintptr_t>(hook.stub),
            hook.callback, hook.address + 5, hook.expected,
            hook.phase ? 0 : g_base + kLocalPlayer - kImageBase, hook.phase, hook.producer);
        DWORD ignored = 0;
        if (!VirtualProtect(hook.stub, 128, PAGE_EXECUTE_READ, &ignored)) { prepared = false; break; }
        FlushInstructionCache(GetCurrentProcess(), hook.stub, size);
        hook.replacement[0] = 0xE9;
        const uint32_t displacement = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(hook.stub) - (hook.address + 5));
        std::memcpy(hook.replacement + 1, &displacement, 4);
    }
    if (!prepared) {
        for (auto& hook : hooks) if (hook.stub != nullptr) VirtualFree(hook.stub, 0, MEM_RELEASE);
        return false;
    }
    for (auto& hook : hooks) {
        if (!WriteCode(hook.address, hook.replacement, 5)) {
            for (auto& installed : hooks) {
                if (installed.installed && std::memcmp(reinterpret_cast<const void*>(installed.address),
                                                       installed.replacement, 5) == 0) {
                    WriteCode(installed.address, installed.expected, 5);
                }
            }
            // Keep prepared executable stubs alive: a callback may already be
            // returning through one. The feature remains disabled on failure.
            return false;
        }
        hook.installed = true;
    }
    return true;
}
}  // namespace

bool Install(bool enabled) {
    if (!enabled) return false;
    if (g_attempted) return g_enabled;
    g_attempted = true;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (g_base == 0) return false;
    g_enabled = InstallHooks();
    return g_enabled;
}
bool IsEnabled() { return g_enabled; }
bool Apply(void* camera) {
    if (!g_enabled || camera == nullptr) return false;
    Identity identity;
    Position live, offset, result;
    if (!g_clock.valid || GetTickCount() - g_clock.observedAt > 250 ||
        !EngineAllowed() || !ReadPlayer(&identity, &live)) { Invalidate(); return false; }
    const uint32_t step = g_history.Producer() == Cadence::Fast ? g_clock.fastStep : g_clock.slowStep;
    if (!g_history.GetOffset(identity, live, step, g_clock.remainder, g_clock.fastStep, &offset)) return false;
    auto* description = static_cast<int32_t*>(camera);
    if (!ApplyOffset({description[1], description[2], description[3]}, offset, &result)) {
        Invalidate();
        return false;
    }
    description[1] = result.x;
    description[2] = result.y;
    description[3] = result.z;
    return true;
}
uint32_t GetSimulationSerial() { return g_clock.slowStep; }
uint32_t GetHistoryEpoch() { return g_history.Epoch(); }
}  // namespace camera_position
