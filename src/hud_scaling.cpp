#include "hud_scaling.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>

#include "hud_scaling_math.h"
#include "logger.h"

namespace hud_scaling {
namespace {
constexpr uintptr_t kCoordinateConversionRva = 0x0012CED0;
constexpr uintptr_t kRenderResolutionRva = 0x005F72C0;
// This is the HUD renderer region in the supported executable. Calls outside it
// keep using the game's original independent X/Y conversion.
constexpr uintptr_t kHudCodeBeginRva = 0x000F0000;
constexpr uintptr_t kHudCodeEndRva = 0x0010C000;
constexpr unsigned char kCallOpcode = 0xE8;
constexpr unsigned int kExpectedHudCalls = 53;
const unsigned char kCoordinateConversionSignature[] = {0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x08};

using ConvertCoordinatesFn = void(__cdecl *)(const int32_t*, int32_t*, int32_t*);
ConvertCoordinatesFn g_originalConvert = nullptr;
uintptr_t g_moduleBase = 0;
float g_multiplier = 1.0f;
LONG g_cachedWidth = -1;
LONG g_cachedHeight = -1;
float g_cachedScale = 1.0f;

float CurrentScale(int32_t width, int32_t height) {
    if (width != g_cachedWidth || height != g_cachedHeight) {
        g_cachedWidth = width;
        g_cachedHeight = height;
        g_cachedScale = CalculateScale(width, height, g_multiplier);
        logger::Log("INFO", "HUDScale", "render_resolution=%ldx%ld scale=%.4f multiplier=%.3f",
                    width, height, static_cast<double>(g_cachedScale),
                    static_cast<double>(g_multiplier));
    }
    return g_cachedScale;
}

void __cdecl ConvertHudCoordinates(const int32_t* resolution, int32_t* x, int32_t* y) {
    const auto* activeResolution =
        reinterpret_cast<const int32_t*>(g_moduleBase + kRenderResolutionRva);
    if (resolution != activeResolution || x == nullptr || y == nullptr) {
        g_originalConvert(resolution, x, y);
        return;
    }

    const int32_t width = activeResolution[0];
    const int32_t height = activeResolution[1];
    const float scale = CurrentScale(width, height);
    *x = ScaleCoordinate(*x, kReferenceWidth, width, scale);
    *y = ScaleCoordinate(*y, kReferenceHeight, height, scale);
}

bool RedirectCall(unsigned char* call, const void* target) {
    const intptr_t displacement = reinterpret_cast<intptr_t>(target) -
                                  reinterpret_cast<intptr_t>(call + 5);
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    const int32_t relative = static_cast<int32_t>(displacement);
    memcpy(call + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    DWORD ignored = 0;
    VirtualProtect(call, 5, oldProtect, &ignored);
    return true;
}
}  // namespace

bool Install(bool enabled, float multiplier) {
    if (!enabled) {
        logger::Log("INFO", "HUDScale", "feature disabled; vanilla coordinate conversion retained");
        return false;
    }
    static_assert(sizeof(void*) == 4, "HUDScale requires a 32-bit build");
    g_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_originalConvert = reinterpret_cast<ConvertCoordinatesFn>(
        g_moduleBase + kCoordinateConversionRva);
    g_multiplier = multiplier;

    if (memcmp(reinterpret_cast<const void*>(g_originalConvert),
               kCoordinateConversionSignature,
               sizeof(kCoordinateConversionSignature)) != 0) {
        logger::Log("ERROR", "HUDScale", "coordinate conversion signature mismatch");
        return false;
    }

    unsigned int matches = 0;
    auto* begin = reinterpret_cast<unsigned char*>(g_moduleBase + kHudCodeBeginRva);
    auto* end = reinterpret_cast<unsigned char*>(g_moduleBase + kHudCodeEndRva);
    const uintptr_t conversion = g_moduleBase + kCoordinateConversionRva;
    for (auto* cursor = begin; cursor + 5 <= end; ++cursor) {
        if (*cursor != kCallOpcode) continue;
        int32_t relative = 0;
        memcpy(&relative, cursor + 1, sizeof(relative));
        if (reinterpret_cast<uintptr_t>(cursor + 5) + relative != conversion) continue;
        ++matches;
        cursor += 4;
    }
    if (matches != kExpectedHudCalls) {
        logger::Log("ERROR", "HUDScale", "HUD call signature mismatch expected=%u actual=%u",
                    kExpectedHudCalls, matches);
        return false;
    }

    unsigned int patched = 0;
    for (auto* cursor = begin; cursor + 5 <= end; ++cursor) {
        if (*cursor != kCallOpcode) continue;
        int32_t relative = 0;
        memcpy(&relative, cursor + 1, sizeof(relative));
        if (reinterpret_cast<uintptr_t>(cursor + 5) + relative != conversion) continue;
        if (!RedirectCall(cursor, reinterpret_cast<const void*>(&ConvertHudCoordinates))) {
            logger::Log("ERROR", "HUDScale", "could not redirect HUD call address=0x%08lX",
                        reinterpret_cast<unsigned long>(cursor));
            return false;
        }
        ++patched;
        cursor += 4;
    }
    if (patched == 0) {
        logger::Log("ERROR", "HUDScale", "no supported HUD coordinate calls found");
        return false;
    }
    logger::Log("INFO", "HUDScale", "feature enabled redirected_calls=%u", patched);
    return true;
}

}  // namespace hud_scaling
