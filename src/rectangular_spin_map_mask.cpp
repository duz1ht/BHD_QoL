#include "rectangular_spin_map_mask.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "logger.h"

namespace rectangular_spin_map_mask {
namespace {
constexpr uintptr_t kGeometryHook = 0x00509903;
constexpr uintptr_t kGeometryReturn = 0x00509961;
constexpr uintptr_t kVertexBufferAddress = 0x00BF57C0;
constexpr uintptr_t kHudSpinMapX1 = 0x00BF6978;
constexpr uintptr_t kHudSpinMapX2 = 0x00BF697C;
constexpr uintptr_t kHudSpinMapY1 = 0x00BF6980;
constexpr uintptr_t kHudSpinMapY2 = 0x00BF6984;
constexpr uintptr_t kIndexCountImmediate = 0x00509962;
constexpr uintptr_t kVertexCountImmediate = 0x0050996B;
constexpr size_t kHookLength = 7;
constexpr uint32_t kMaskZBits = 0x3F7FFFBD;
constexpr uint32_t kReciprocalWBits = 0x3F800000;

// The game's transformed/lit vertex has a 40-byte stride. The mask draw reads
// position, RHW, and diffuse color; the remaining attributes stay zeroed.
struct MaskVertex {
    float x;
    float y;
    float z;
    float reciprocalW;
    uint32_t diffuse;
    uint32_t unused[5];
};

static_assert(sizeof(MaskVertex) == 40, "unexpected Spin Map vertex stride");

const unsigned char kExpectedHook[kHookLength] = {
    0x33, 0xFF,                         // xor edi,edi
    0xBE, 0xEC, 0x57, 0xBF, 0x00,       // mov esi,00BF57EC
};

float FloatFromBits(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void SetVertex(MaskVertex& vertex, int x, int y, uint32_t diffuse) {
    vertex = {};
    vertex.x = static_cast<float>(x);
    vertex.y = static_cast<float>(y);
    vertex.z = FloatFromBits(kMaskZBits);
    vertex.reciprocalW = FloatFromBits(kReciprocalWBits);
    vertex.diffuse = diffuse;
}

extern "C" void __cdecl BuildRectangularSpinMapMask(uintptr_t framePointer,
                                                     uint32_t diffuse) {
    auto* vertices = reinterpret_cast<MaskVertex*>(kVertexBufferAddress);
    const int x1 = *reinterpret_cast<const int*>(kHudSpinMapX1);
    const int x2 = *reinterpret_cast<const int*>(kHudSpinMapX2);
    const int y1 = *reinterpret_cast<const int*>(kHudSpinMapY1);
    const int y2 = *reinterpret_cast<const int*>(kHudSpinMapY2);

    SetVertex(vertices[0], x1, y1, diffuse);
    SetVertex(vertices[1], x2, y1, diffuse);
    SetVertex(vertices[2], x2, y2, diffuse);
    SetVertex(vertices[3], x1, y2, diffuse);

    auto* indices = reinterpret_cast<uint16_t*>(framePointer - 0x12C);
    const uint16_t rectangleIndices[] = {0, 1, 2, 0, 2, 3};
    std::memcpy(indices, rectangleIndices, sizeof(rectangleIndices));
}

bool WriteRelativeJump(unsigned char* destination, const void* target) {
    const intptr_t displacement = reinterpret_cast<const unsigned char*>(target) -
                                  (destination + 5);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        return false;
    }
    destination[0] = 0xE9;
    const int32_t relative = static_cast<int32_t>(displacement);
    std::memcpy(destination + 1, &relative, sizeof(relative));
    return true;
}

unsigned char* BuildHookStub() {
    constexpr size_t kStubSize = 17;
    auto* stub = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, kStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (stub == nullptr) {
        return nullptr;
    }

    unsigned char* output = stub;
    *output++ = 0x60;  // pushad
    *output++ = 0x53;  // push ebx (diffuse color argument)
    *output++ = 0x55;  // push ebp (caller's frame pointer argument)
    if (!WriteRelativeJump(output,
                           reinterpret_cast<const void*>(&BuildRectangularSpinMapMask))) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return nullptr;
    }
    output += 5;
    *output++ = 0x83;  // add esp,8
    *output++ = 0xC4;
    *output++ = 0x08;
    *output++ = 0x61;  // popad
    if (!WriteRelativeJump(output, reinterpret_cast<const void*>(kGeometryReturn))) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return nullptr;
    }
    FlushInstructionCache(GetCurrentProcess(), stub, kStubSize);
    return stub;
}

bool ApplyInstallation(const unsigned char (&detour)[kHookLength]) {
    auto* start = reinterpret_cast<unsigned char*>(kGeometryHook);
    const size_t span = kVertexCountImmediate - kGeometryHook + 1;
    DWORD oldProtect = 0;
    if (!VirtualProtect(start, span, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    std::memcpy(start, detour, sizeof(detour));
    *reinterpret_cast<unsigned char*>(kIndexCountImmediate) = 6;
    *reinterpret_cast<unsigned char*>(kVertexCountImmediate) = 4;
    FlushInstructionCache(GetCurrentProcess(), start, span);

    DWORD ignored = 0;
    if (!VirtualProtect(start, span, oldProtect, &ignored)) {
        logger::Log("WARN", "RectangularSpinMapMask",
                    "hook installed but code protection restore failed: error=%lu",
                    GetLastError());
    }
    return true;
}
}  // namespace

bool Install(bool enabled) {
    if (!enabled) {
        logger::Log("INFO", "RectangularSpinMapMask", "feature disabled");
        return true;
    }

    logger::Log("INFO", "RectangularSpinMapMask", "feature enabled");
    auto* hook = reinterpret_cast<unsigned char*>(kGeometryHook);
    const auto* indexCount = reinterpret_cast<const unsigned char*>(kIndexCountImmediate);
    const auto* vertexCount = reinterpret_cast<const unsigned char*>(kVertexCountImmediate);
    if (std::memcmp(hook, kExpectedHook, sizeof(kExpectedHook)) != 0 ||
        *indexCount != 0x60 || *vertexCount != 0x21) {
        logger::Log("ERROR", "RectangularSpinMapMask",
                    "Spin Map mask signature mismatch; geometry left unchanged");
        return false;
    }

    unsigned char* stub = BuildHookStub();
    if (stub == nullptr) {
        logger::Log("ERROR", "RectangularSpinMapMask", "could not allocate hook stub");
        return false;
    }

    unsigned char detour[kHookLength] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (!WriteRelativeJump(detour, stub)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        logger::Log("ERROR", "RectangularSpinMapMask", "hook target is out of range");
        return false;
    }

    if (!ApplyInstallation(detour)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        logger::Log("ERROR", "RectangularSpinMapMask",
                    "hook installation failed: error=%lu", GetLastError());
        return false;
    }

    logger::Log("INFO", "RectangularSpinMapMask",
                "rectangular depth mask installed address=0x%08lX bounds=HUDSPINMAP",
                static_cast<unsigned long>(kGeometryHook));
    return true;
}

}  // namespace rectangular_spin_map_mask
