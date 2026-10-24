#include "high_frequency_viewmodel.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#include "high_frequency_camera.h"
#include "high_frequency_viewmodel_math.h"
#include "raw_input.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#endif

namespace high_frequency_viewmodel {
namespace {
constexpr uintptr_t kRenderModelRva = 0x000FFB20;
constexpr uintptr_t kRenderAlternateModelRva = 0x000FFBA0;
constexpr uintptr_t kBuildPoseMatrixRva = 0x0015DB60;
constexpr size_t kRenderModelHookLength = 5;
constexpr size_t kRenderAlternateHookLength = 8;
constexpr int kMaxViewmodelBones = 49;
constexpr int kHistorySlots = 16;

const unsigned char kRenderModelSignature[kRenderModelHookLength] = {
    0x55, 0x8B, 0xEC, 0x51, 0x56,
};
const unsigned char kRenderAlternateSignature[kRenderAlternateHookLength] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x04, 0xB5, 0x64, 0x00,
};

using RenderModelFn = void(__cdecl*)(void*, const float*);
using BuildPoseMatrixFn = void(__cdecl*)(float*, int32_t*);

struct PaletteHistory {
    const void* model = nullptr;
    bool ready = false;
    unsigned long simulationSerial = 0;
    LARGE_INTEGER tickStart = {};
    int boneCount = 0;
    float previousRootRelative[16] = {};
    float currentRootRelative[16] = {};
    float previousLocal[kMaxViewmodelBones * 16] = {};
    float currentLocal[kMaxViewmodelBones * 16] = {};
};

RenderModelFn g_originalRenderModel = nullptr;
RenderModelFn g_originalRenderAlternate = nullptr;
uintptr_t g_moduleBase = 0;
bool g_enabled = false;
bool g_installed = false;
LARGE_INTEGER g_clockFrequency = {};
PaletteHistory g_history[kHistorySlots] = {};

bool IsPrimaryViewmodelCaller(uintptr_t caller) {
    const uintptr_t rva = caller - g_moduleBase;
    return rva == 0x00088BD1 || rva == 0x00088C1D ||
           rva == 0x00088C6F || rva == 0x00088CA7;
}

bool IsAlternateViewmodelCaller(uintptr_t caller) {
    const uintptr_t rva = caller - g_moduleBase;
    return rva == 0x00088BC2 || rva == 0x00088BFC ||
           rva == 0x00088C60 || rva == 0x00088C98;
}

bool WriteRelativeJump(unsigned char* output, const void* target) {
    const intptr_t displacement = reinterpret_cast<intptr_t>(target) -
                                  reinterpret_cast<intptr_t>(output + 5);
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) return false;
    output[0] = 0xE9;
    const int32_t relative = static_cast<int32_t>(displacement);
    std::memcpy(output + 1, &relative, sizeof(relative));
    return true;
}

bool InstallHook(unsigned char* target, size_t length, const void* detour,
                 RenderModelFn* original) {
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) return false;
    std::memcpy(trampoline, target, length);
    if (!WriteRelativeJump(trampoline + length, target + length)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    *original = reinterpret_cast<RenderModelFn>(trampoline);
    const bool written = WriteRelativeJump(target, detour);
    if (written) {
        std::memset(target + 5, 0x90, length - 5);
        FlushInstructionCache(GetCurrentProcess(), target, length);
    }
    DWORD ignored = 0;
    VirtualProtect(target, length, oldProtect, &ignored);
    if (!written) {
        *original = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
    }
    return written;
}

PaletteHistory* FindHistory(const void* model) {
    for (auto& slot : g_history) {
        if (slot.model == model) return &slot;
    }
    for (auto& slot : g_history) {
        if (slot.model == nullptr) {
            slot.model = model;
            return &slot;
        }
    }
    g_history[0] = {};
    g_history[0].model = model;
    return &g_history[0];
}

void ConvertToLocalPalette(const float* worldPalette, int boneCount,
                           float* localPalette) {
    float inverseRoot[16] = {};
    InvertRigidTransform(worldPalette, inverseRoot);
    for (int bone = 0; bone < boneCount; ++bone) {
        MultiplyMatrix(inverseRoot, worldPalette + bone * 16,
                       localPalette + bone * 16);
    }
}

double UpdateHistory(PaletteHistory* history, const float* palette,
                     const float rootRelative[16], int boneCount) {
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    if (g_clockFrequency.QuadPart == 0) QueryPerformanceFrequency(&g_clockFrequency);
    const unsigned long serial = raw_input::GetSimulationSerial();
    float captured[kMaxViewmodelBones * 16] = {};
    ConvertToLocalPalette(palette, boneCount, captured);
    const size_t paletteBytes = static_cast<size_t>(boneCount) * 16 * sizeof(float);

    if (!history->ready || history->boneCount != boneCount) {
        std::memcpy(history->previousRootRelative, rootRelative, sizeof(history->previousRootRelative));
        std::memcpy(history->currentRootRelative, rootRelative, sizeof(history->currentRootRelative));
        std::memcpy(history->previousLocal, captured, paletteBytes);
        std::memcpy(history->currentLocal, captured, paletteBytes);
        history->simulationSerial = serial;
        history->tickStart = now;
        history->boneCount = boneCount;
        history->ready = true;
        return 1.0;
    }
    if (serial != history->simulationSerial) {
        std::memcpy(history->previousRootRelative, history->currentRootRelative,
                    sizeof(history->previousRootRelative));
        std::memcpy(history->currentRootRelative, rootRelative,
                    sizeof(history->currentRootRelative));
        std::memcpy(history->previousLocal, history->currentLocal, paletteBytes);
        std::memcpy(history->currentLocal, captured, paletteBytes);
        history->simulationSerial = serial;
        history->tickStart = now;
    }
    const double elapsed = static_cast<double>(now.QuadPart - history->tickStart.QuadPart) /
                           static_cast<double>(g_clockFrequency.QuadPart);
    return std::clamp(elapsed * 62.5, 0.0, 1.0);
}

bool GetCurrentCameraMatrices(float simulationCamera[16],
                              float presentationCamera[16]) {
    int32_t simulationPose[6] = {};
    int32_t presentationPose[6] = {};
    if (!high_frequency_camera::GetPresentationCameraPose(simulationPose,
                                                          presentationPose)) return false;
    const auto buildPose = reinterpret_cast<BuildPoseMatrixFn>(
        g_moduleBase + kBuildPoseMatrixRva);
    buildPose(simulationCamera, simulationPose);
    buildPose(presentationCamera, presentationPose);
    return true;
}

bool BuildPrivatePalette(void* model, const float* source, float* output) {
    if (model == nullptr || source == nullptr || output == nullptr) return false;
    const int boneCount = *reinterpret_cast<const int*>(
        static_cast<const unsigned char*>(model) + 0x10);
    if (boneCount <= 0 || boneCount > kMaxViewmodelBones) return false;

    float simulationCamera[16] = {};
    float presentationCamera[16] = {};
    if (!GetCurrentCameraMatrices(simulationCamera, presentationCamera)) return false;

    // The incoming root contains both the simulation camera and the weapon's
    // own offset/bob.  Keep the camera out of the history so mouse rotation can
    // remain current while only the weapon-relative motion is interpolated.
    float inverseSimulationCamera[16] = {};
    float rootRelative[16] = {};
    InvertRigidTransform(simulationCamera, inverseSimulationCamera);
    MultiplyMatrix(inverseSimulationCamera, source, rootRelative);

    PaletteHistory* history = FindHistory(model);
    const double alpha = UpdateHistory(history, source, rootRelative, boneCount);
    float interpolatedRootRelative[16] = {};
    InterpolateRigidTransform(history->previousRootRelative,
                              history->currentRootRelative,
                              static_cast<float>(alpha),
                              interpolatedRootRelative);
    float presentationRoot[16] = {};
    MultiplyMatrix(presentationCamera, interpolatedRootRelative,
                   presentationRoot);

    for (int bone = 0; bone < boneCount; ++bone) {
        float local[16] = {};
        InterpolateRigidTransform(history->previousLocal + bone * 16,
                                  history->currentLocal + bone * 16,
                                  static_cast<float>(alpha), local);
        MultiplyMatrix(presentationRoot, local, output + bone * 16);
    }
    return true;
}

void DispatchViewmodel(RenderModelFn original, void* model, const float* palette) {
    float privatePalette[kMaxViewmodelBones * 16] = {};
    if (g_enabled && BuildPrivatePalette(model, palette, privatePalette)) {
        original(model, privatePalette);
        return;
    }
    original(model, palette);
}

void __cdecl HookRenderModel(void* model, const float* palette) {
#if defined(_MSC_VER)
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
#else
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
#endif
    if (IsPrimaryViewmodelCaller(caller))
        DispatchViewmodel(g_originalRenderModel, model, palette);
    else
        g_originalRenderModel(model, palette);
}

void __cdecl HookRenderAlternate(void* model, const float* palette) {
#if defined(_MSC_VER)
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
#else
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
#endif
    if (IsAlternateViewmodelCaller(caller))
        DispatchViewmodel(g_originalRenderAlternate, model, palette);
    else
        g_originalRenderAlternate(model, palette);
}
}  // namespace

bool Install(bool enabled) {
    g_enabled = enabled;
    if (!enabled) {
        return true;
    }
    if (g_installed) return true;
    static_assert(sizeof(void*) == 4,
                  "HighFrequencyViewmodel requires a 32-bit build");
    g_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* renderModel = reinterpret_cast<unsigned char*>(g_moduleBase + kRenderModelRva);
    auto* renderAlternate = reinterpret_cast<unsigned char*>(
        g_moduleBase + kRenderAlternateModelRva);
    if (std::memcmp(renderModel, kRenderModelSignature,
                    sizeof(kRenderModelSignature)) != 0 ||
        std::memcmp(renderAlternate, kRenderAlternateSignature,
                    sizeof(kRenderAlternateSignature)) != 0) {
        return false;
    }
    if (!InstallHook(renderModel, kRenderModelHookLength, &HookRenderModel,
                     &g_originalRenderModel) ||
        !InstallHook(renderAlternate, kRenderAlternateHookLength,
                     &HookRenderAlternate, &g_originalRenderAlternate)) {
        return false;
    }
    g_installed = true;
    return true;
}

}  // namespace high_frequency_viewmodel
