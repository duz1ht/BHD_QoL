#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
HWND g_foreground = reinterpret_cast<HWND>(0x1234);
DWORD g_now = 0;
bool g_failWrite = false;
uintptr_t g_testImage = 0;
uintptr_t Address(uintptr_t native) { return g_testImage + native - 0x00400000; }
HWND WINAPI TestForeground() { return g_foreground; }
HMODULE WINAPI TestModule(LPCWSTR) { return reinterpret_cast<HMODULE>(g_testImage); }
DWORD WINAPI TestTicks() { return g_now; }
BOOL WINAPI TestProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old) {
    if (g_failWrite && reinterpret_cast<uintptr_t>(address) == Address(0x004803EF)) return FALSE;
    return VirtualProtect(address, size, protection, old);
}
}  // namespace

// Compile the actual backend with only its OS environment controlled. This
// tests signature preflight, rollback and callbacks at the supported game RVAs
// in a synthetic image, without adding test exports to the production DLL.
#define GetForegroundWindow TestForeground
#define GetModuleHandleW TestModule
#define GetTickCount TestTicks
#define VirtualProtect TestProtect
#include "../src/camera_position.cpp"
#undef VirtualProtect
#undef GetTickCount
#undef GetModuleHandleW
#undef GetForegroundWindow

namespace {
void Require(bool valid, int line) {
    if (!valid) { std::fprintf(stderr, "Camera position backend regression at line %d\n", line); std::exit(1); }
}
#define Check(condition) Require((condition), __LINE__)
void Write(uintptr_t address, uint32_t value) { std::memcpy(reinterpret_cast<void*>(address), &value, 4); }
void WriteGame(uintptr_t address, uint32_t value) { Write(Address(address), value); }
void Pose(uintptr_t actor, camera_position::Position p) {
    Write(actor + 8, static_cast<uint32_t>(p.x));
    Write(actor + 12, static_cast<uint32_t>(p.y));
    Write(actor + 16, static_cast<uint32_t>(p.z));
}
#if defined(_MSC_VER)
__declspec(naked) void __cdecl Invoke(uintptr_t, uintptr_t, uint32_t) {
    __asm { push esi }
    __asm { push ebx }
    __asm { mov eax, [esp + 12] }
    __asm { mov esi, [esp + 16] }
    __asm { mov ebx, [esp + 20] }
    __asm { call eax }
    __asm { pop ebx }
    __asm { pop esi }
    __asm { ret }
}
#else
__attribute__((naked)) void __cdecl Invoke(uintptr_t, uintptr_t, uint32_t) {
    __asm__ __volatile__("pushl %esi\n\tpushl %ebx\n\tmovl 12(%esp), %eax\n\t"
                         "movl 16(%esp), %esi\n\tmovl 20(%esp), %ebx\n\tcall *%eax\n\t"
                         "popl %ebx\n\tpopl %esi\n\tret");
}
#endif
}  // namespace

int main(int argc, char** argv) {
    using namespace camera_position;
    void* image = VirtualAlloc(nullptr, 0xC00000,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    Check(image != nullptr);
    g_testImage = reinterpret_cast<uintptr_t>(image);
    const uintptr_t addresses[] = {Address(0x004BF08F), Address(0x004803EF), Address(0x00472E63)};
    unsigned char original[][5] = {
        {0xA1, 0x50, 0x22, 0xA0, 0}, {0xA1, 0x10, 0x3F, 0x68, 0}, {0xA1, 0x10, 0x3F, 0x68, 0}};
    for (int i = 0; i < 3; ++i) {
        const uint32_t operand = static_cast<uint32_t>(Address(i == 0 ? 0x00A02250 : 0x00683F10));
        std::memcpy(original[i] + 1, &operand, 4);
        std::memcpy(reinterpret_cast<void*>(addresses[i]), original[i], 5);
        *reinterpret_cast<unsigned char*>(addresses[i] + 5) = 0xC3;
    }
    if (argc == 2) {
        const bool signature = std::strcmp(argv[1], "signature") == 0;
        if (signature) *reinterpret_cast<unsigned char*>(addresses[2]) = 0x90;
        else { Check(std::strcmp(argv[1], "rollback") == 0); g_failWrite = true; }
        Check(!Install(true) && !IsEnabled());
        Check(std::memcmp(reinterpret_cast<void*>(addresses[0]), original[0], 5) == 0);
        Check(std::memcmp(reinterpret_cast<void*>(addresses[1]), original[1], 5) == 0);
        if (!signature) Check(std::memcmp(reinterpret_cast<void*>(addresses[2]), original[2], 5) == 0);
        VirtualFree(image, 0, MEM_RELEASE);
        return 0;
    }
    Check(!Install(false) && !IsEnabled());
    Check(Install(true) && IsEnabled() && Install(true));
    const uintptr_t actor = Address(0x00840000);
    WriteGame(0x0096C290, actor); WriteGame(0x00715900, actor); WriteGame(0x007F2DD4, actor);
    WriteGame(0x009F20DC, 1); WriteGame(0x00F654FC, 0x1234);
    Write(actor, 1); Write(actor + 0x24, 2); Write(actor + 0x4C, 3);
    Write(actor + 0x228, 0x123456); Write(actor + 0x224, 0x654321);
    const auto fast = [&](uint32_t preStep, Position p) {
        Pose(actor, p);
        WriteGame(0x00A0339C, preStep);
        Invoke(Address(0x004803EF), actor, 0);
        // Match the reference EXE: the increment is AFTER the fast producer.
        WriteGame(0x00A0339C, preStep + 1);
        Invoke(Address(0x004BF08F), actor, 32);
    };
    int32_t camera[16] = {777, 310, 420, 530, 17, 18, 19};
    fast(9, {100, 200, 300}); Check(!Apply(camera));
    fast(10, {300, 400, 500});
    unsigned char actorBefore[0x29C] = {};
    std::memcpy(actorBefore, reinterpret_cast<void*>(actor), sizeof(actorBefore));
    Check(Apply(camera));
    Check(camera[0] == 777 && camera[1] == 210 && camera[2] == 320 && camera[3] == 430);
    Check(camera[4] == 17 && camera[5] == 18 && camera[6] == 19);
    Check(std::memcmp(actorBefore, reinterpret_cast<void*>(actor), sizeof(actorBefore)) == 0);

    // A non-player producer must not overwrite the player's history.
    Invoke(Address(0x004803EF), actor + 0x29C, 0);
    camera[1] = 310; camera[2] = 420; camera[3] = 530;
    Check(Apply(camera) && camera[1] == 210);
    WriteGame(0x007F2DD0, 1); Check(!Apply(camera)); WriteGame(0x007F2DD0, 0);
    fast(11, {400, 500, 600}); Check(!Apply(camera));
    fast(12, {500, 600, 700}); Check(Apply(camera));

    g_foreground = nullptr; Invoke(Address(0x004BF08F), actor, 32); Check(!Apply(camera));
    g_foreground = reinterpret_cast<HWND>(0x1234);
    fast(13, {600, 700, 800}); Check(!Apply(camera));
    fast(14, {700, 800, 900}); Check(Apply(camera));
    const uint32_t epoch = GetHistoryEpoch();
    Write(actor + 8, 701); Check(!Apply(camera)); Check(GetHistoryEpoch() != epoch);
    fast(15, {800, 900, 1000}); fast(16, {900, 1000, 1100});
    Write(actor + 0x13C, 1); Check(!Apply(camera)); Write(actor + 0x13C, 0);
    fast(17, {1000, 1100, 1200}); fast(18, {1100, 1200, 1300});
    WriteGame(0x007C72BC, 1); Invoke(Address(0x004BF08F), actor, 32); Check(!Apply(camera));
    WriteGame(0x007C72BC, 0);
    fast(19, {1200, 1300, 1400}); fast(20, {1300, 1400, 1500});
    g_now += 251; Check(!Apply(camera));

    // Switch to a slow producer; its logic counter increments BEFORE capture.
    Write(actor + 0x228, 0);
    WriteGame(0x009F374C, 30); Pose(actor, {1000, 2000, 3000}); Invoke(Address(0x00472E63), actor, 0);
    WriteGame(0x00A0339C, 1); Invoke(Address(0x004BF08F), actor, 32); Check(!Apply(camera));
    WriteGame(0x009F374C, 31); Pose(actor, {2000, 4000, 6000}); Invoke(Address(0x00472E63), actor, 0);
    WriteGame(0x00A0339C, 2); Invoke(Address(0x004BF08F), actor, 32);
    camera[1] = 2010; camera[2] = 4020; camera[3] = 6030;
    Check(Apply(camera) && camera[1] == 1385 && camera[2] == 2770 && camera[3] == 4155);
    Check(GetSimulationSerial() == 31);
    WriteGame(0x0096C290, 0xDEADBEEF); WriteGame(0x007F2DD4, 0xDEADBEEF);
    Check(!Apply(camera)); // must reject before dereferencing an out-of-pool pointer
    VirtualFree(image, 0, MEM_RELEASE);
    return 0;
}
