#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "camera_position_hook.h"

namespace {
void Check(bool valid) { if (!valid) { std::fputs("Camera capture stub regression\n", stderr); std::abort(); } }
uint32_t g_player = 0x12345678, g_counter = 0x76543210;
uint32_t g_calls = 0, g_actor = 0, g_producer = 0;
const float g_float = 3.25f;
void ClobberFloatState() {
#if defined(_MSC_VER)
    __asm { xorps xmm0, xmm0 }
    __asm { fldz }
#else
    __asm__ __volatile__("xorps %%xmm0, %%xmm0\n\tfldz" ::: "xmm0");
#endif
}
void __cdecl Capture(uint32_t actor, uint32_t producer) {
    ++g_calls; g_actor = actor; g_producer = producer;
    ClobberFloatState();
}
void __cdecl Phase(uint32_t remainder) { ++g_calls; g_actor = remainder; ClobberFloatState(); }
struct Result { uint32_t eax, esi, ebx, flags; float x87, xmm; };
}  // namespace

int main() {
    static_assert(sizeof(void*) == 4);
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(memory != nullptr);
    unsigned char original[5] = {0xA1}; // replay mov eax,[counter]
    const uint32_t counter = reinterpret_cast<uintptr_t>(&g_counter);
    std::memcpy(original + 1, &counter, 4);
    memory[120] = 0xC3; // stand-in continuation returns to the test harness
    const size_t size = camera_position::BuildCaptureStub(memory, reinterpret_cast<uintptr_t>(memory),
        reinterpret_cast<uintptr_t>(&Capture), reinterpret_cast<uintptr_t>(memory + 120), original,
        reinterpret_cast<uintptr_t>(&g_player), false, 2);
    Check(size < 120);
    camera_position::BuildCaptureStub(memory + 128, reinterpret_cast<uintptr_t>(memory + 128),
        reinterpret_cast<uintptr_t>(&Phase), reinterpret_cast<uintptr_t>(memory + 120), original, 0, true, 0);

    // cdecl harness(stub, actor, remainder, result): set known register, flag,
    // x87 and SSE values, call the mid-function stub and record the result.
    unsigned char harness[] = {
        0x9C, 0x60,                              // pushfd; pushad
        0xD9, 0xE8,                              // fld1
        0xF3, 0x0F, 0x10, 0x05, 0, 0, 0, 0,   // movss xmm0,[float]
        0x8B, 0x4C, 0x24, 0x28,                 // mov ecx,[esp+40] (stub)
        0x8B, 0x74, 0x24, 0x2C,                 // mov esi,[esp+44] (actor)
        0x8B, 0x5C, 0x24, 0x30,                 // mov ebx,[esp+48] (remainder)
        0xF9, 0xFF, 0xD1,                       // stc; call ecx
        0x8B, 0x54, 0x24, 0x34,                 // mov edx,[esp+52] (result)
        0x89, 0x02, 0x89, 0x72, 0x04, 0x89, 0x5A, 0x08,
        0x9C, 0x58, 0x89, 0x42, 0x0C,           // flags
        0xD9, 0x5A, 0x10,                       // fstp [edx+16]
        0xF3, 0x0F, 0x11, 0x42, 0x14,           // movss [edx+20],xmm0
        0x61, 0x9D, 0xC3,
    };
    const uint32_t value = reinterpret_cast<uintptr_t>(&g_float);
    std::memcpy(harness + 8, &value, 4);
    std::memcpy(memory + 256, harness, sizeof(harness));
    DWORD ignored = 0;
    Check(VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &ignored) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), memory, 4096);
    using Invoke = void(__cdecl*)(void*, uint32_t, uint32_t, Result*);
    const auto invoke = reinterpret_cast<Invoke>(memory + 256);
    Result result = {};
    invoke(memory, g_player, 32, &result);
    Check(g_calls == 1 && g_actor == g_player && g_producer == 2);
    Check(result.eax == g_counter && result.esi == g_player && result.ebx == 32);
    Check((result.flags & 1) != 0 && result.x87 == 1.0f && result.xmm == g_float);
    invoke(memory, g_player + 1, 48, &result);
    Check(g_calls == 1 && result.eax == g_counter); // filtered actor still replays native instruction
    Check((result.flags & 1) != 0 && result.x87 == 1.0f && result.xmm == g_float);
    invoke(memory + 128, g_player, 48, &result);
    Check(g_calls == 2 && g_actor == 48);
    Check(result.eax == g_counter && result.ebx == 48 && result.esi == g_player);
    Check((result.flags & 1) != 0 && result.x87 == 1.0f && result.xmm == g_float);
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
