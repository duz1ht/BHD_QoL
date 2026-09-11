#include "oval_spin_map.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "logger.h"

namespace oval_spin_map {
namespace {
// RenderSpinMap() builds the single rotating map quad from HUDSPINMAPX1/X2/Y1/Y2
// and submits it at VA 0x005035E2 in the supported executable.
constexpr uintptr_t kSpinMapDrawCallRva = 0x001035E2;
constexpr uintptr_t kGameDrawPrimitiveRva = 0x001AA0D0;
constexpr int kLineStrip = 3;
constexpr int kTriangleStrip = 5;
constexpr int kTriangleFan = 6;
constexpr size_t kSegmentCount = 32;
constexpr size_t kVertexCount = kSegmentCount + 2;

// DFBHD's transformed HUD vertex (FVF 0x2C4) has a 40-byte stride.
struct HudVertex {
    float x;
    float y;
    float z;
    float rhw;
    uint32_t diffuse;
    uint32_t specular;
    float u;
    float v;
    float u2;
    float v2;
};
static_assert(sizeof(HudVertex) == 40, "unexpected DFBHD HUD vertex size");

using DrawPrimitiveFn = int(__cdecl *)(int, const HudVertex*, int);
DrawPrimitiveFn g_gameDrawPrimitive = nullptr;
bool g_installed = false;

const HudVertex& ClosestCorner(const HudVertex* vertices, float x, float y) {
    const HudVertex* closest = vertices;
    float closestDistance = std::numeric_limits<float>::max();
    for (size_t index = 0; index < 4; ++index) {
        const float dx = vertices[index].x - x;
        const float dy = vertices[index].y - y;
        const float distance = dx * dx + dy * dy;
        if (distance < closestDistance) {
            closest = vertices + index;
            closestDistance = distance;
        }
    }
    return *closest;
}

float Bilinear(float topLeft, float topRight, float bottomLeft, float bottomRight,
               float x, float y) {
    const float top = topLeft + (topRight - topLeft) * x;
    const float bottom = bottomLeft + (bottomRight - bottomLeft) * x;
    return top + (bottom - top) * y;
}

int __cdecl DrawOvalSpinMap(int primitive, const HudVertex* vertices, int vertexCount) {
    if (primitive != kTriangleStrip || vertices == nullptr || vertexCount != 4) {
        return g_gameDrawPrimitive(primitive, vertices, vertexCount);
    }

    float left = vertices[0].x;
    float right = vertices[0].x;
    float top = vertices[0].y;
    float bottom = vertices[0].y;
    for (size_t index = 1; index < 4; ++index) {
        left = std::min(left, vertices[index].x);
        right = std::max(right, vertices[index].x);
        top = std::min(top, vertices[index].y);
        bottom = std::max(bottom, vertices[index].y);
    }

    const float centerX = (left + right) * 0.5f;
    const float centerY = (top + bottom) * 0.5f;
    const float radiusX = (right - left) * 0.5f;
    const float radiusY = (bottom - top) * 0.5f;
    if (!(radiusX > 0.0f) || !(radiusY > 0.0f)) {
        return g_gameDrawPrimitive(primitive, vertices, vertexCount);
    }

    const HudVertex& topLeft = ClosestCorner(vertices, left, top);
    const HudVertex& topRight = ClosestCorner(vertices, right, top);
    const HudVertex& bottomLeft = ClosestCorner(vertices, left, bottom);
    const HudVertex& bottomRight = ClosestCorner(vertices, right, bottom);

    HudVertex fan[kVertexCount] = {};
    fan[0] = vertices[0];
    fan[0].x = centerX;
    fan[0].y = centerY;
    fan[0].u = Bilinear(topLeft.u, topRight.u, bottomLeft.u, bottomRight.u, 0.5f, 0.5f);
    fan[0].v = Bilinear(topLeft.v, topRight.v, bottomLeft.v, bottomRight.v, 0.5f, 0.5f);
    fan[0].u2 = Bilinear(topLeft.u2, topRight.u2, bottomLeft.u2, bottomRight.u2, 0.5f, 0.5f);
    fan[0].v2 = Bilinear(topLeft.v2, topRight.v2, bottomLeft.v2, bottomRight.v2, 0.5f, 0.5f);

    constexpr float kTwoPi = 6.28318530717958647692f;
    for (size_t segment = 0; segment <= kSegmentCount; ++segment) {
        const float angle = kTwoPi * static_cast<float>(segment) /
                            static_cast<float>(kSegmentCount);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float normalizedX = 0.5f + cosine * 0.5f;
        const float normalizedY = 0.5f + sine * 0.5f;

        HudVertex& vertex = fan[segment + 1];
        vertex = vertices[0];
        vertex.x = centerX + cosine * radiusX;
        vertex.y = centerY + sine * radiusY;
        // Mapping through the original corners retains the game's rotating UV basis.
        vertex.u = Bilinear(topLeft.u, topRight.u, bottomLeft.u, bottomRight.u,
                            normalizedX, normalizedY);
        vertex.v = Bilinear(topLeft.v, topRight.v, bottomLeft.v, bottomRight.v,
                            normalizedX, normalizedY);
        vertex.u2 = Bilinear(topLeft.u2, topRight.u2, bottomLeft.u2, bottomRight.u2,
                             normalizedX, normalizedY);
        vertex.v2 = Bilinear(topLeft.v2, topRight.v2, bottomLeft.v2, bottomRight.v2,
                             normalizedX, normalizedY);
    }

    const int result = g_gameDrawPrimitive(kTriangleFan, fan,
                                           static_cast<int>(kVertexCount));

    HudVertex outline[kSegmentCount + 1] = {};
    for (size_t index = 0; index <= kSegmentCount; ++index) {
        outline[index] = fan[index + 1];
        outline[index].diffuse = 0xFFFF0000;
        outline[index].specular = 0;
        // Sampling the map center avoids transparent texels along its original edges.
        outline[index].u = fan[0].u;
        outline[index].v = fan[0].v;
        outline[index].u2 = fan[0].u2;
        outline[index].v2 = fan[0].v2;
    }
    g_gameDrawPrimitive(kLineStrip, outline,
                        static_cast<int>(kSegmentCount + 1));
    return result;
}

bool WriteRelativeCall(unsigned char* callSite, const void* target) {
    const intptr_t displacement = reinterpret_cast<intptr_t>(target) -
                                  reinterpret_cast<intptr_t>(callSite + 5);
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) return false;
    const int32_t relative = static_cast<int32_t>(displacement);
    DWORD oldProtect = 0;
    if (!VirtualProtect(callSite, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    callSite[0] = 0xE8;
    memcpy(callSite + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), callSite, 5);
    DWORD ignored = 0;
    VirtualProtect(callSite, 5, oldProtect, &ignored);
    return true;
}
} // namespace

bool Install(bool enabled) {
    if (!enabled) {
        logger::Log("INFO", "OvalSpinMap", "feature disabled");
        return false;
    }
    if (g_installed) return true;

    static_assert(sizeof(void*) == 4, "OvalSpinMap requires a 32-bit build");
    auto* module = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* callSite = module + kSpinMapDrawCallRva;
    const unsigned char expected[] = {0xE8, 0xE9, 0x6A, 0x0A, 0x00};
    if (memcmp(callSite, expected, sizeof(expected)) != 0) {
        logger::Log("ERROR", "OvalSpinMap", "draw call signature mismatch address=0x%08lX",
                    reinterpret_cast<unsigned long>(callSite));
        return false;
    }

    g_gameDrawPrimitive = reinterpret_cast<DrawPrimitiveFn>(module + kGameDrawPrimitiveRva);
    if (!WriteRelativeCall(callSite, reinterpret_cast<const void*>(&DrawOvalSpinMap))) {
        g_gameDrawPrimitive = nullptr;
        logger::Log("ERROR", "OvalSpinMap", "hook installation failed: error=%lu",
                    GetLastError());
        return false;
    }
    g_installed = true;
    logger::Log("INFO", "OvalSpinMap",
                "32-segment triangle fan hook installed with red diagnostic outline");
    return true;
}

} // namespace oval_spin_map
