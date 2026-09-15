#include "visual_interpolation.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "logger.h"

namespace visual_interpolation {
namespace {
constexpr uintptr_t kClockCaptureRva = 0x000BF08F;
constexpr uintptr_t kFastOItemCaptureRva = 0x000803EF;
constexpr uintptr_t kMItemCaptureRva = 0x00072C2C;
constexpr uintptr_t kSlowOItemCaptureRva = 0x00072E63;
constexpr uintptr_t kRenderActorRva = 0x00121D80;
constexpr uintptr_t kSubmitModelARva = 0x000FFB20;
constexpr uintptr_t kSubmitModelBRva = 0x000FFBA0;
constexpr uintptr_t kViewmodelCallRva = 0x00088AF6;
constexpr uintptr_t kBuildFloatMatrixRva = 0x0015DB60;

constexpr uintptr_t kOItemPoolPointerRva = 0x00315900;
constexpr uintptr_t kMItemPoolPointerRva = 0x00315904;
constexpr uintptr_t kCameraModeRva = 0x003F2DD0;
constexpr uintptr_t kCameraOwnerRva = 0x003F2DD4;
constexpr uintptr_t kGameplayTickRva = 0x005F374C;
constexpr uintptr_t kPauseStateRva = 0x003C72BC;
constexpr uintptr_t kSchedulerSpecialModeRva = 0x006033C0;

constexpr std::size_t kActorSize = 0x29C;
constexpr std::size_t kOItemCapacity = 256;
constexpr std::size_t kMItemCapacity = 1200;
constexpr std::size_t kMaximumMatrices = 51;
constexpr std::size_t kMaximumRenderDepth = 16;
constexpr std::int32_t kTeleportDistanceQ16 = 64 * 65536;

enum class Pool : std::uint32_t { OItem = 0, MItem = 1 };
enum class Cadence : std::uint32_t { Unknown = 0, Hz250 = 1, Hz62_5 = 2 };

struct Snapshot {
    TransformQ16 transform = {};
    std::uint32_t step = 0;
    bool valid = false;
};

struct Track {
    Snapshot previous;
    Snapshot current;
    Cadence cadence = Cadence::Unknown;
    std::uint32_t definitionIndex = 0;
    uintptr_t definition = 0;
    std::uint32_t logicalId = 0;
    uintptr_t rideTarget = 0;
    std::uint32_t generation = 0;
};

struct VisualClock {
    std::uint32_t remainder = 0;
    std::uint32_t phase = 0;
    bool valid = false;
};

using RenderActorFn = void(__cdecl*)(void*, int, int);
using SubmitModelFn = void(__cdecl*)(void*, Matrix4x4*);
using BuildFloatMatrixFn = void(__cdecl*)(Matrix4x4*, const TransformQ16*);

uintptr_t g_moduleBase = 0;
bool g_enabled = false;
bool g_installed = false;
VisualClock g_clock;
std::array<Track, kOItemCapacity> g_oItems;
std::array<Track, kMItemCapacity> g_mItems;
RenderActorFn g_originalRenderActor = nullptr;
SubmitModelFn g_originalSubmitModelA = nullptr;
SubmitModelFn g_originalSubmitModelB = nullptr;

thread_local std::array<void*, kMaximumRenderDepth> g_renderActors = {};
thread_local std::size_t g_renderDepth = 0;
thread_local std::array<Matrix4x4, kMaximumMatrices> g_matrixCopy = {};
volatile LONG g_snapshotCaptures = 0;
volatile LONG g_cameraCorrections = 0;
volatile LONG g_viewmodelCorrections = 0;
volatile LONG g_worldCorrections = 0;
volatile LONG g_nativeNoContext = 0;
volatile LONG g_nativeInvalidCount = 0;
volatile LONG g_nativeNoSnapshot = 0;
volatile LONG g_unobservedChanges = 0;
std::uint32_t g_lastPauseState = 0;
std::uint32_t g_lastSpecialMode = 0;

const unsigned char kClockSignature[] = {0xA1, 0x50, 0x22, 0xA0, 0x00};
const unsigned char kFastOItemSignature[] = {0xA1, 0x10, 0x3F, 0x68, 0x00};
const unsigned char kMItemSignature[] = {0xA1, 0xDC, 0xDD, 0x70, 0x00};
const unsigned char kSlowOItemSignature[] = {0xA1, 0x10, 0x3F, 0x68, 0x00};
const unsigned char kRenderActorSignature[] = {
    0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x08,
};
const unsigned char kSubmitModelASignature[] = {0x55, 0x8B, 0xEC, 0x51, 0x56};
const unsigned char kSubmitModelBSignature[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x04, 0xB5, 0x64, 0x00,
};
const unsigned char kViewmodelCallSignature[] = {0xE8, 0x65, 0x50, 0x0D, 0x00};

template <typename T>
T ReadActorField(const void* actor, std::size_t offset) {
    T result = {};
    std::memcpy(&result, static_cast<const unsigned char*>(actor) + offset, sizeof(result));
    return result;
}

TransformQ16 ReadTransform(const void* actor) {
    TransformQ16 result = {};
    std::memcpy(&result, static_cast<const unsigned char*>(actor) + 0x08, sizeof(result));
    return result;
}

bool ActiveActor(const void* actor) {
    return actor != nullptr && ReadActorField<std::uint32_t>(actor, 0x00) != 0 &&
           ReadActorField<uintptr_t>(actor, 0x24) != 0;
}

std::uint32_t ProducerStep(Cadence cadence) {
    if (cadence == Cadence::Hz250) {
        return *reinterpret_cast<const volatile std::uint32_t*>(g_moduleBase + 0x0060339C);
    }
    return *reinterpret_cast<const volatile std::uint32_t*>(g_moduleBase + kGameplayTickRva);
}

Track* TrackForActor(void* actor, Pool pool) {
    const uintptr_t pointerRva = pool == Pool::OItem ? kOItemPoolPointerRva : kMItemPoolPointerRva;
    const std::size_t capacity = pool == Pool::OItem ? kOItemCapacity : kMItemCapacity;
    const uintptr_t begin = *reinterpret_cast<const volatile uintptr_t*>(g_moduleBase + pointerRva);
    const uintptr_t address = reinterpret_cast<uintptr_t>(actor);
    if (begin == 0 || address < begin) return nullptr;
    const uintptr_t distance = address - begin;
    if (distance % kActorSize != 0 || distance / kActorSize >= capacity) return nullptr;
    const std::size_t index = distance / kActorSize;
    return pool == Pool::OItem ? &g_oItems[index] : &g_mItems[index];
}

Track* FindTrack(void* actor) {
    if (Track* track = TrackForActor(actor, Pool::OItem)) return track;
    return TrackForActor(actor, Pool::MItem);
}

void ResetTrack(Track* track, const TransformQ16& transform, Cadence cadence,
                std::uint32_t step, std::uint32_t definitionIndex,
                uintptr_t definition, std::uint32_t logicalId, uintptr_t rideTarget) {
    ++track->generation;
    track->previous = {transform, step, true};
    track->current = track->previous;
    track->cadence = cadence;
    track->definitionIndex = definitionIndex;
    track->definition = definition;
    track->logicalId = logicalId;
    track->rideTarget = rideTarget;
}

extern "C" void __stdcall CaptureActor(void* actor, std::uint32_t poolValue,
                                       std::uint32_t cadenceValue) {
    if (!g_enabled || actor == nullptr) return;
    const Pool pool = static_cast<Pool>(poolValue);
    const Cadence cadence = static_cast<Cadence>(cadenceValue);
    Track* track = TrackForActor(actor, pool);
    if (track == nullptr) return;
    if (!ActiveActor(actor)) {
        track->previous.valid = false;
        track->current.valid = false;
        track->definitionIndex = 0;
        track->definition = 0;
        track->logicalId = 0;
        track->rideTarget = 0;
        track->cadence = Cadence::Unknown;
        return;
    }
    const uintptr_t fastCallback = ReadActorField<uintptr_t>(actor, 0x228);
    const uintptr_t slowCallback = ReadActorField<uintptr_t>(actor, 0x224);
    constexpr uintptr_t kNullMovementCallback = 0x005382E0;
    if (pool == Pool::OItem && cadence == Cadence::Hz250 &&
        (fastCallback == 0 || fastCallback == kNullMovementCallback)) return;
    if (pool == Pool::OItem && cadence == Cadence::Hz62_5 &&
        (fastCallback != 0 || slowCallback == 0 ||
         slowCallback == kNullMovementCallback)) return;
    const TransformQ16 transform = ReadTransform(actor);
    const std::uint32_t step = ProducerStep(cadence);
    const std::uint32_t definitionIndex = ReadActorField<std::uint32_t>(actor, 0x00);
    const uintptr_t definition = ReadActorField<uintptr_t>(actor, 0x24);
    const std::uint32_t logicalId = ReadActorField<std::uint32_t>(actor, 0x4C);
    const uintptr_t rideTarget = ReadActorField<uintptr_t>(actor, 0x13C);
    const bool identityChanged = !track->current.valid ||
        track->definitionIndex != definitionIndex || track->definition != definition ||
        (track->logicalId != logicalId && (track->logicalId != 0 || logicalId != 0)) ||
        track->rideTarget != rideTarget || track->cadence != cadence;
    if (identityChanged || IsTeleport(track->current.transform, transform,
                                      kTeleportDistanceQ16)) {
        ResetTrack(track, transform, cadence, step, definitionIndex, definition,
                   logicalId, rideTarget);
        return;
    }
    if (track->current.step == step) {
        track->current.transform = transform;
        return;
    }
    track->previous = track->current;
    track->current = {transform, step, true};
    InterlockedIncrement(&g_snapshotCaptures);
}

extern "C" void __stdcall CaptureClock(std::uint32_t remainder) {
    const std::uint32_t pause =
        *reinterpret_cast<const volatile std::uint32_t*>(g_moduleBase + kPauseStateRva);
    const std::uint32_t special =
        *reinterpret_cast<const volatile std::uint32_t*>(g_moduleBase + kSchedulerSpecialModeRva);
    if (pause != g_lastPauseState || special != g_lastSpecialMode) {
        for (Track& track : g_oItems) {
            track.previous.valid = false;
            track.current.valid = false;
        }
        for (Track& track : g_mItems) {
            track.previous.valid = false;
            track.current.valid = false;
        }
        g_lastPauseState = pause;
        g_lastSpecialMode = special;
    }
    g_clock.remainder = remainder;
    g_clock.phase =
        *reinterpret_cast<const volatile std::uint32_t*>(g_moduleBase + 0x0060339C);
    g_clock.valid = g_enabled && remainder < 0x40 && pause == 0 && special == 0;
}

std::uint32_t AlphaFor(Cadence cadence) {
    if (!g_clock.valid) return visual_camera::kInterpolationOne;
    if (cadence == Cadence::Hz250) {
        return AlphaFor250Hz(g_clock.remainder);
    }
    if (cadence == Cadence::Hz62_5) {
        // The 62.5 Hz callback runs while pre-increment phase is zero. At the
        // render boundary the corresponding new snapshot therefore starts at
        // post-increment phase one, not at phase zero.
        return AlphaFor62Hz(g_clock.phase, g_clock.remainder);
    }
    return visual_camera::kInterpolationOne;
}

bool InterpolatedForActor(void* actor, TransformQ16* current,
                          TransformQ16* interpolated) {
    if (!g_enabled || !g_clock.valid || !ActiveActor(actor) || current == nullptr ||
        interpolated == nullptr) return false;
    Track* track = FindTrack(actor);
    if (track == nullptr || !track->previous.valid || !track->current.valid ||
        track->cadence == Cadence::Unknown) return false;
    const TransformQ16 live = ReadTransform(actor);
    if (!SameTransform(live, track->current.transform)) {
        // An unobserved script/network/teleport writer changed this entity.
        track->previous = {live, track->current.step, true};
        track->current = track->previous;
        InterlockedIncrement(&g_unobservedChanges);
        return false;
    }
    *current = track->current.transform;
    *interpolated = InterpolateTransform(track->previous.transform,
                                         track->current.transform,
                                         AlphaFor(track->cadence));
    return true;
}

bool BuildCorrection(void* actor, Matrix4x4* correction) {
    TransformQ16 current = {};
    TransformQ16 interpolated = {};
    if (!InterpolatedForActor(actor, &current, &interpolated)) return false;
    auto build = reinterpret_cast<BuildFloatMatrixFn>(g_moduleBase + kBuildFloatMatrixRva);
    Matrix4x4 currentRoot = {};
    Matrix4x4 interpolatedRoot = {};
    build(&currentRoot, &current);
    build(&interpolatedRoot, &interpolated);
    Matrix4x4 inverse = {};
    if (!InvertAffine(currentRoot, &inverse)) return false;
    *correction = Multiply(inverse, interpolatedRoot);
    return true;
}

void* CurrentRenderActor() {
    return g_renderDepth == 0 ? nullptr : g_renderActors[g_renderDepth - 1];
}

void __cdecl HookRenderActor(void* actor, int visualArg1, int visualArg2) {
    const bool haveRoom = g_renderDepth < g_renderActors.size();
    if (haveRoom) g_renderActors[g_renderDepth++] = actor;
    g_originalRenderActor(actor, visualArg1, visualArg2);
    if (haveRoom) --g_renderDepth;
}

void SubmitWithCorrection(SubmitModelFn original, void* model, Matrix4x4* matrices) {
    void* actor = CurrentRenderActor();
    if (!g_enabled || actor == nullptr || model == nullptr || matrices == nullptr) {
        if (g_enabled) InterlockedIncrement(&g_nativeNoContext);
        original(model, matrices);
        return;
    }
    const std::int32_t count =
        *reinterpret_cast<const std::int32_t*>(static_cast<unsigned char*>(model) + 0x10);
    Matrix4x4 correction = {};
    if (count <= 0 || count > static_cast<std::int32_t>(kMaximumMatrices)) {
        InterlockedIncrement(&g_nativeInvalidCount);
        original(model, matrices);
        return;
    }
    if (!BuildCorrection(actor, &correction)) {
        InterlockedIncrement(&g_nativeNoSnapshot);
        original(model, matrices);
        return;
    }
    for (std::int32_t index = 0; index < count; ++index) {
        g_matrixCopy[index] = Multiply(matrices[index], correction);
    }
    InterlockedIncrement(&g_worldCorrections);
    original(model, g_matrixCopy.data());
}

void __cdecl HookSubmitModelA(void* model, Matrix4x4* matrices) {
    SubmitWithCorrection(g_originalSubmitModelA, model, matrices);
}

void __cdecl HookSubmitModelB(void* model, Matrix4x4* matrices) {
    SubmitWithCorrection(g_originalSubmitModelB, model, matrices);
}

void __cdecl HookViewmodelMatrix(Matrix4x4* destination, const TransformQ16* source) {
    auto original = reinterpret_cast<BuildFloatMatrixFn>(g_moduleBase + kBuildFloatMatrixRva);
    original(destination, source);
    void* owner = *reinterpret_cast<void* const volatile*>(g_moduleBase + kCameraOwnerRva);
    Matrix4x4 correction = {};
    if (destination != nullptr &&
        *reinterpret_cast<const volatile std::int32_t*>(g_moduleBase + kCameraModeRva) == 0 &&
        BuildCorrection(owner, &correction)) {
        *destination = Multiply(*destination, correction);
        InterlockedIncrement(&g_viewmodelCorrections);
    }
}

bool RelativeDisplacement(unsigned char* instruction, const void* target,
                          std::int32_t* displacement) {
    const intptr_t value = reinterpret_cast<intptr_t>(target) -
                           reinterpret_cast<intptr_t>(instruction + 5);
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) return false;
    *displacement = static_cast<std::int32_t>(value);
    return true;
}

bool WriteBranch(unsigned char* output, unsigned char opcode, const void* target) {
    std::int32_t displacement = 0;
    if (!RelativeDisplacement(output, target, &displacement)) return false;
    output[0] = opcode;
    std::memcpy(output + 1, &displacement, sizeof(displacement));
    return true;
}

bool BuildBranchForAddress(unsigned char* replacement, unsigned char* patchAddress,
                           unsigned char opcode, const void* target) {
    const intptr_t value = reinterpret_cast<intptr_t>(target) -
                           reinterpret_cast<intptr_t>(patchAddress + 5);
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) return false;
    replacement[0] = opcode;
    const std::int32_t displacement = static_cast<std::int32_t>(value);
    std::memcpy(replacement + 1, &displacement, sizeof(displacement));
    return true;
}

bool PatchBytes(unsigned char* target, const unsigned char* expected,
                std::size_t size, const unsigned char* replacement) {
    if (std::memcmp(target, expected, size) != 0) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    std::memcpy(target, replacement, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored = 0;
    VirtualProtect(target, size, oldProtect, &ignored);
    return true;
}

template <typename Function>
bool InstallFunctionDetour(uintptr_t rva, const unsigned char* signature,
                           std::size_t length, Function hook, Function* original) {
    auto* target = reinterpret_cast<unsigned char*>(g_moduleBase + rva);
    if (std::memcmp(target, signature, length) != 0) return false;
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) return false;
    std::memcpy(trampoline, target, length);
    if (!WriteBranch(trampoline + length, 0xE9, target + length)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    std::array<unsigned char, 16> replacement = {};
    std::fill(replacement.begin(), replacement.end(), 0x90);
    if (!BuildBranchForAddress(replacement.data(), target, 0xE9,
                               reinterpret_cast<const void*>(hook)) ||
        !PatchBytes(target, signature, length, replacement.data())) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    *original = reinterpret_cast<Function>(trampoline);
    return true;
}

bool InstallInlineCapture(uintptr_t rva, const unsigned char* signature,
                          void* callback, bool clock, Pool pool, Cadence cadence) {
    constexpr std::size_t displacedLength = 5;
    auto* target = reinterpret_cast<unsigned char*>(g_moduleBase + rva);
    if (std::memcmp(target, signature, displacedLength) != 0) return false;
    auto* stub = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (stub == nullptr) return false;
    unsigned char* output = stub;
    *output++ = 0x9C;  // pushfd
    *output++ = 0x60;  // pushad
    if (clock) {
        *output++ = 0x53;  // push ebx
    } else {
        *output++ = 0x68;
        const std::uint32_t cadenceValue = static_cast<std::uint32_t>(cadence);
        std::memcpy(output, &cadenceValue, 4); output += 4;
        *output++ = 0x68;
        const std::uint32_t poolValue = static_cast<std::uint32_t>(pool);
        std::memcpy(output, &poolValue, 4); output += 4;
        *output++ = 0x56;  // push esi (saved value remains unchanged by pushad)
    }
    if (!WriteBranch(output, 0xE8, callback)) {
        VirtualFree(stub, 0, MEM_RELEASE); return false;
    }
    output += 5;
    *output++ = 0x61;  // popad
    *output++ = 0x9D;  // popfd
    std::memcpy(output, signature, displacedLength); output += displacedLength;
    if (!WriteBranch(output, 0xE9, target + displacedLength)) {
        VirtualFree(stub, 0, MEM_RELEASE); return false;
    }
    std::array<unsigned char, displacedLength> replacement = {};
    if (!BuildBranchForAddress(replacement.data(), target, 0xE9, stub) ||
        !PatchBytes(target, signature, displacedLength, replacement.data())) {
        VirtualFree(stub, 0, MEM_RELEASE); return false;
    }
    return true;
}

bool InstallCallReplacement(uintptr_t rva, const unsigned char* signature,
                            const void* replacementFunction) {
    auto* target = reinterpret_cast<unsigned char*>(g_moduleBase + rva);
    unsigned char replacement[5] = {0xE8, 0, 0, 0, 0};
    std::int32_t displacement = 0;
    if (!RelativeDisplacement(target, replacementFunction, &displacement)) return false;
    std::memcpy(replacement + 1, &displacement, sizeof(displacement));
    return PatchBytes(target, signature, sizeof(replacement), replacement);
}

bool InstallAllHooks() {
    if (!InstallInlineCapture(kClockCaptureRva, kClockSignature,
                              reinterpret_cast<void*>(&CaptureClock), true,
                              Pool::OItem, Cadence::Unknown)) return false;
    if (!InstallInlineCapture(kFastOItemCaptureRva, kFastOItemSignature,
                              reinterpret_cast<void*>(&CaptureActor), false,
                              Pool::OItem, Cadence::Hz250)) return false;
    if (!InstallInlineCapture(kMItemCaptureRva, kMItemSignature,
                              reinterpret_cast<void*>(&CaptureActor), false,
                              Pool::MItem, Cadence::Hz62_5)) return false;
    if (!InstallInlineCapture(kSlowOItemCaptureRva, kSlowOItemSignature,
                              reinterpret_cast<void*>(&CaptureActor), false,
                              Pool::OItem, Cadence::Hz62_5)) return false;
    if (!InstallFunctionDetour(kRenderActorRva, kRenderActorSignature,
                               sizeof(kRenderActorSignature), &HookRenderActor,
                               &g_originalRenderActor)) return false;
    if (!InstallFunctionDetour(kSubmitModelARva, kSubmitModelASignature,
                               sizeof(kSubmitModelASignature), &HookSubmitModelA,
                               &g_originalSubmitModelA)) return false;
    if (!InstallFunctionDetour(kSubmitModelBRva, kSubmitModelBSignature,
                               sizeof(kSubmitModelBSignature), &HookSubmitModelB,
                               &g_originalSubmitModelB)) return false;
    return InstallCallReplacement(kViewmodelCallRva, kViewmodelCallSignature,
                                  reinterpret_cast<const void*>(&HookViewmodelMatrix));
}
}  // namespace

bool ApplyCameraCorrection(void* cameraSource, std::uint32_t cameraSourceSize) {
    if (!g_enabled || cameraSource == nullptr || cameraSourceSize < 0x1C ||
        *reinterpret_cast<const volatile std::int32_t*>(g_moduleBase + kCameraModeRva) != 0) {
        return false;
    }
    void* owner = *reinterpret_cast<void* const volatile*>(g_moduleBase + kCameraOwnerRva);
    TransformQ16 current = {};
    TransformQ16 interpolated = {};
    if (!InterpolatedForActor(owner, &current, &interpolated)) return false;
    auto* bytes = static_cast<unsigned char*>(cameraSource);
    constexpr std::size_t positionOffsets[] = {0x04, 0x08, 0x0C};
    const std::int32_t currentPosition[] = {current.x, current.y, current.z};
    const std::int32_t visualPosition[] = {interpolated.x, interpolated.y, interpolated.z};
    for (std::size_t index = 0; index < 3; ++index) {
        std::int32_t value = 0;
        std::memcpy(&value, bytes + positionOffsets[index], 4);
        value += visualPosition[index] - currentPosition[index];
        std::memcpy(bytes + positionOffsets[index], &value, 4);
    }
    constexpr std::size_t angleOffsets[] = {0x10, 0x14, 0x18};
    const std::uint32_t currentAngles[] = {current.yaw, current.pitch, current.roll};
    const std::uint32_t visualAngles[] = {interpolated.yaw, interpolated.pitch, interpolated.roll};
    for (std::size_t index = 0; index < 3; ++index) {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes + angleOffsets[index], 4);
        value += visualAngles[index] - currentAngles[index];
        std::memcpy(bytes + angleOffsets[index], &value, 4);
    }
    InterlockedIncrement(&g_cameraCorrections);
    return true;
}

bool Install(bool enabled) {
    if (!enabled) {
        logger::Log("INFO", "VisualInterpolation", "feature disabled");
        return false;
    }
    if (g_installed) return g_enabled;
#if defined(_WIN32)
    static_assert(sizeof(void*) == 4, "Visual interpolation requires a 32-bit build");
#endif
    g_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!InstallAllHooks()) {
        g_enabled = false;
        logger::Log("ERROR", "VisualInterpolation",
                    "hook validation or installation failed; synchronized path disabled");
        return false;
    }
    g_installed = true;
    g_enabled = true;
    logger::Log("INFO", "VisualInterpolation",
                "guarded synchronized camera/viewmodel/OITEM/MITEM hooks installed");
    return true;
}

bool Enabled() { return g_enabled; }

void LogStatistics() {
    if (!g_enabled) return;
    logger::Log("INFO", "VisualInterpolation.Stats",
                "snapshots=%ld camera=%ld viewmodel=%ld world=%ld "
                "native_no_context=%ld native_invalid_count=%ld "
                "native_no_snapshot=%ld unobserved_changes=%ld clock_valid=%d",
                InterlockedExchange(&g_snapshotCaptures, 0),
                InterlockedExchange(&g_cameraCorrections, 0),
                InterlockedExchange(&g_viewmodelCorrections, 0),
                InterlockedExchange(&g_worldCorrections, 0),
                InterlockedExchange(&g_nativeNoContext, 0),
                InterlockedExchange(&g_nativeInvalidCount, 0),
                InterlockedExchange(&g_nativeNoSnapshot, 0),
                InterlockedExchange(&g_unobservedChanges, 0), g_clock.valid ? 1 : 0);
}

}  // namespace visual_interpolation
