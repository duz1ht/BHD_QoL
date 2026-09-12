#include "hud_scaling.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>

#include "hud_scaling_math.h"
#include "logger.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#endif

namespace hud_scaling {
namespace {
constexpr uintptr_t kResolutionRva = 0x005F72C0;
constexpr uintptr_t kQuadRva = 0x00102A60;
constexpr uintptr_t kSpinMapRva = 0x001093E0;
constexpr uintptr_t kConvertRva = 0x0012CED0;

using QuadFn = void(__cdecl*)(int32_t, int32_t, int32_t, int32_t,
                              int32_t, int32_t, uintptr_t, uint32_t);
using SpinMapFn = void(__cdecl*)(int32_t, int32_t, int32_t, int32_t, int32_t);
using TextFn = void(__cdecl*)(const void*, int32_t, int32_t, const char*, uint32_t, uint32_t);
using ConvertFn = void(__cdecl*)(const int32_t*, int32_t*, int32_t*);

struct FontDescriptor {
    uintptr_t font;
    float scaleX;
    float scaleY;
};

struct TextCall {
    uintptr_t callRva;
    uintptr_t wrapperRva;
};

constexpr TextCall kTextCalls[] = {
    {0x00101E68, 0x000F6CD0}, {0x00101F38, 0x000F6CD0},
    {0x00101FB2, 0x000F6CD0}, {0x001025D9, 0x000F6CD0},
    {0x00102472, 0x000F6EB0}, {0x00102507, 0x000F6EB0},
    {0x00102783, 0x000F6EB0}, {0x00102817, 0x000F6EB0},
    {0x001033A0, 0x000F6CD0}, {0x00103D5C, 0x000F6EB0},
    {0x00103DB6, 0x000F6CD0}, {0x00104016, 0x000F6EB0},
    {0x001040B6, 0x000F6EB0}, {0x001041F4, 0x000F6EB0},
    {0x0010421B, 0x000F6CD0}, {0x0010A116, 0x000F6DC0},
    {0x0010A17E, 0x000F6CD0}, {0x0010A33E, 0x000F6CD0},
    {0x0010AC4A, 0x000F6CD0}, {0x0010ACB1, 0x000F6CD0},
    {0x0010AD9C, 0x000F6D50}, {0x0010AE27, 0x000F6D50},
};

constexpr uintptr_t kBottomRightQuadCalls[] = {
    0x0010A46B,
    0x0010A4E2,
};

constexpr uintptr_t kBottomLeftQuadCalls[] = {
    0x0010A819,
    0x0010A8C6,
    0x0010A985,
};

constexpr uintptr_t kStatusConversionCalls[] = {
    0x0010A046,
    0x0010A058,
    0x0010A139,
    0x0010A263,
    0x0010A275,
    0x0010A2F4,
};

uintptr_t g_base = 0;
float g_graphicsMultiplier = 1.0f;
float g_textMultiplier = 1.0f;
QuadFn g_quad = nullptr;
SpinMapFn g_spinMap = nullptr;
ConvertFn g_convert = nullptr;

void TransformRect(int32_t* x, int32_t* y, int32_t* width, int32_t* height,
                   HorizontalAnchor horizontal, VerticalAnchor vertical) {
    // Transform in the game's 1024x768 coordinate space. The original renderer
    // performs its normal resolution conversion afterwards, so multiplier 1.0
    // is exactly the vanilla path rather than an additional automatic scale.
    *x = ScalePosition(*x, kReferenceWidth, kReferenceWidth,
                       g_graphicsMultiplier, horizontal);
    *y = ScalePosition(*y, kReferenceHeight, kReferenceHeight,
                       g_graphicsMultiplier, vertical);
    *width = ScaleExtent(*width, g_graphicsMultiplier);
    *height = ScaleExtent(*height, g_graphicsMultiplier);
}

void DrawQuad(int32_t x, int32_t y, int32_t sourceWidth, int32_t sourceHeight,
              int32_t width, int32_t height, uintptr_t texture, uint32_t color,
              HorizontalAnchor horizontal, VerticalAnchor vertical) {
    TransformRect(&x, &y, &width, &height, horizontal, vertical);
    g_quad(x, y, sourceWidth, sourceHeight, width, height, texture, color);
}

void __cdecl DrawBottomRight(int32_t x, int32_t y, int32_t sw, int32_t sh,
                             int32_t w, int32_t h, uintptr_t texture, uint32_t color) {
    DrawQuad(x, y, sw, sh, w, h, texture, color, HorizontalAnchor::Right, VerticalAnchor::Bottom);
}
void __cdecl DrawBottomLeft(int32_t x, int32_t y, int32_t sw, int32_t sh,
                            int32_t w, int32_t h, uintptr_t texture, uint32_t color) {
    DrawQuad(x, y, sw, sh, w, h, texture, color, HorizontalAnchor::Left, VerticalAnchor::Bottom);
}

void __cdecl DrawSpinMap(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t mode) {
    int32_t width = x2 - x1;
    int32_t height = y2 - y1;
    TransformRect(&x1, &y1, &width, &height, HorizontalAnchor::Right, VerticalAnchor::Top);
    g_spinMap(x1, y1, x1 + width, y1 + height, mode);
}

void __cdecl DrawHudText(const void* descriptor, int32_t x, int32_t y, const char* text,
                         uint32_t color, uint32_t flags) {
#if defined(_MSC_VER)
    const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
#else
    const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
#endif
    uintptr_t wrapperRva = 0;
    for (const auto& call : kTextCalls) {
        if (returnAddress == g_base + call.callRva + 5) { wrapperRva = call.wrapperRva; break; }
    }
    if (wrapperRva == 0 || descriptor == nullptr) return;
    FontDescriptor copy = *static_cast<const FontDescriptor*>(descriptor);
    copy.scaleX *= g_textMultiplier;
    copy.scaleY *= g_textMultiplier;
    reinterpret_cast<TextFn>(g_base + wrapperRva)(&copy, x, y, text, color, flags);
}

void __cdecl ConvertStatus(const int32_t* resolution, int32_t* x, int32_t* y) {
#if defined(_MSC_VER)
    const uintptr_t returnRva = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
#else
    const uintptr_t returnRva = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_base;
#endif
    const bool extent = returnRva == 0x0010A05D || returnRva == 0x0010A27A;
    if (extent) {
        *x = ScaleExtent(*x, g_graphicsMultiplier);
        *y = ScaleExtent(*y, g_graphicsMultiplier);
    } else {
        *x = ScalePosition(*x, kReferenceWidth, kReferenceWidth,
                           g_graphicsMultiplier, HorizontalAnchor::Left);
        *y = ScalePosition(*y, kReferenceHeight, kReferenceHeight,
                           g_graphicsMultiplier, VerticalAnchor::Bottom);
    }
    g_convert(resolution, x, y);
}

int32_t __cdecl TextSpacingReferenceWidth() {
    // The original routine derives both chat and system-message spacing as
    // width * 12 / 1280. Returning the real width at multiplier 1.0 preserves
    // vanilla spacing; other values apply the same multiplier used for glyphs.
    const auto* resolution = reinterpret_cast<const volatile LONG*>(g_base + kResolutionRva);
    return ScaleExtent(resolution[0], g_textMultiplier);
}

bool RedirectCall(uintptr_t callRva, const void* target) {
    auto* call = reinterpret_cast<unsigned char*>(g_base + callRva);
    const intptr_t displacement = reinterpret_cast<intptr_t>(target) -
                                  reinterpret_cast<intptr_t>(call + 5);
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    const int32_t relative = static_cast<int32_t>(displacement);
    call[0] = 0xE8;
    memcpy(call + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    DWORD ignored = 0;
    VirtualProtect(call, 5, oldProtect, &ignored);
    return true;
}
}  // namespace

bool Install(bool graphicsEnabled, float graphicsMultiplier,
             bool textEnabled, float textMultiplier) {
    if (!graphicsEnabled && !textEnabled) {
        logger::Log("INFO", "HUDScaling", "graphics and text scaling disabled");
        return false;
    }
    static_assert(sizeof(void*) == 4, "HUDScaling requires a 32-bit build");
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_graphicsMultiplier = graphicsMultiplier;
    g_textMultiplier = textMultiplier;
    g_quad = reinterpret_cast<QuadFn>(g_base + kQuadRva);
    g_spinMap = reinterpret_cast<SpinMapFn>(g_base + kSpinMapRva);
    g_convert = reinterpret_cast<ConvertFn>(g_base + kConvertRva);

    bool ok = true;
    if (graphicsEnabled) {
        ok = RedirectCall(0x0010266C, reinterpret_cast<const void*>(&DrawSpinMap)) && ok;
        for (const uintptr_t rva : kBottomRightQuadCalls)
            ok = RedirectCall(rva, reinterpret_cast<const void*>(&DrawBottomRight)) && ok;
        for (const uintptr_t rva : kBottomLeftQuadCalls)
            ok = RedirectCall(rva, reinterpret_cast<const void*>(&DrawBottomLeft)) && ok;
        for (const uintptr_t rva : kStatusConversionCalls)
            ok = RedirectCall(rva, reinterpret_cast<const void*>(&ConvertStatus)) && ok;
    }
    if (textEnabled) {
        for (const auto& call : kTextCalls)
            ok = RedirectCall(call.callRva, reinterpret_cast<const void*>(&DrawHudText)) && ok;
        ok = RedirectCall(0x0010ACE6,
                          reinterpret_cast<const void*>(&TextSpacingReferenceWidth)) && ok;
    }

    logger::Log(ok ? "INFO" : "ERROR", "HUDScaling",
                "selective hooks %s graphics_enabled=%d graphics_multiplier=%.3f "
                "text_enabled=%d text_multiplier=%.3f",
                ok ? "installed" : "incomplete", graphicsEnabled,
                static_cast<double>(graphicsMultiplier), textEnabled,
                static_cast<double>(textMultiplier));
    return ok;
}
}  // namespace hud_scaling
