#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
void* g_denied = nullptr;
BOOL WINAPI TestProtect(LPVOID address, SIZE_T bytes, DWORD protection, PDWORD previous) {
    if (address == g_denied && protection == PAGE_EXECUTE_READWRITE) return FALSE;
    return VirtualProtect(address, bytes, protection, previous);
}
}
#define VirtualProtect TestProtect
#include "../src/allow_high_resolution_fonts.cpp"
#undef VirtualProtect

namespace {
void Require(bool condition, int line) {
    if (!condition) { std::fprintf(stderr, "Dynamic FNT backend regression at line %d\n", line); std::exit(1); }
}
#define Check(condition) Require((condition), __LINE__)
std::vector<unsigned char> Fixture(uint32_t side, uint32_t pages) {
    std::vector<unsigned char> file(fnt_layout::kMetadataBytes + static_cast<size_t>(side) * side * 4 * pages);
    const uint32_t header[] = {0x30544E46, 800, pages, static_cast<uint32_t>(-3)};
    std::memcpy(file.data(), header, sizeof(header));
    const fnt_layout::Glyph glyph{0, 0.125f, 0.25f, 0.1875f, 0.375f};
    std::memcpy(file.data() + 32 + ('A' - 32) * 20, &glyph, 20);
    for (uint32_t p = 0; p < pages; ++p) {
        const uint32_t marker = p ? 0xFF00FF00 : 0xFFFF0000;
        auto* pixels = file.data() + fnt_layout::kMetadataBytes + static_cast<size_t>(p) * side * side * 4;
        for (size_t i = 0; i < static_cast<size_t>(side) * side; ++i) std::memcpy(pixels + i * 4, &marker, 4);
    }
    return file;
}
void* g_fileResult = nullptr;
size_t g_fileBytes = 0;
int g_nativeFonts = 0, g_freed = 0, g_released = 0, g_uploads = 0;
int g_sizes[32]{};
const void* g_sources[32]{};
uint32_t g_formats[32]{};
uint32_t g_textureFields[32][5]{};
bool g_failHd = false;
bool g_nullHd = false;
bool g_failContext = false;
const void* g_lastFree = nullptr;
const unsigned char* g_expectedFile = nullptr;
void* __cdecl LoadFile(const char*) {
    *reinterpret_cast<uint32_t*>(allow_high_resolution_fonts::Address(0x00F65684)) = static_cast<uint32_t>(g_fileBytes);
    return g_fileResult;
}
void __cdecl Free(void* data) { ++g_freed; g_lastFree = data; }
void* __cdecl Texture(const char*, const void* data, int width, int height, int, uint32_t flags) {
    Check(width == height && g_uploads < 32);
    const int index = g_uploads++;
    g_sizes[index] = width;
    g_sources[index] = data;
    g_formats[index] = flags;
    auto* fields = g_textureFields[index];
    if (width > 256 && g_nullHd) return nullptr;
    fields[2] = width > 256 && g_failHd ? 0 : 1;
    fields[3] = fields[4] = width;
    return fields;
}
int __cdecl Release(void*) { ++g_released; return 0; }
int __fastcall NativeFont(void* font, void*, const void* data) {
    using namespace allow_high_resolution_fonts;
    Check(font == reinterpret_cast<void*>(0x12345678));
    ++g_nativeFonts;
    if (g_failContext) throw std::bad_alloc();
    uint32_t pages = 0;
    std::memcpy(&pages, static_cast<const unsigned char*>(data) + 8, 4);
    Check(std::memcmp(data, g_expectedFile, fnt_layout::kMetadataBytes) == 0);
    for (uint32_t p = 0; p < pages; ++p) {
        const void* logical = static_cast<const unsigned char*>(data) + fnt_layout::kMetadataBytes + p * fnt_layout::kLogicalPageBytes;
        // The CPU path's fixed 256 stride sees the correct page even for a
        // two-page HD input. In production the native loader copies this data.
        uint32_t marker;
        std::memcpy(&marker, logical, 4);
        Check(marker == (p ? 0xFF00FF00 : 0xFFFF0000));
        Check(HookTexture("native-font-page", logical, 256, 256, 0, 0x140001) != nullptr);
    }
    return 1234;
}
}

int main() {
    using namespace allow_high_resolution_fonts;
    Check(!Install(false));
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0xC00000,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(image != nullptr);
    g_base = reinterpret_cast<uintptr_t>(image);
    auto hooks = BuildHooks();
    Check(hooks.size() == 4);
    for (const Hook& hook : hooks)
        std::memcpy(reinterpret_cast<void*>(Address(hook.address)), hook.expected.data(), hook.length);
    // Native font stack prologue executes in the real trampoline. Continue by
    // undoing its frame and jumping to the controlled thiscall-compatible stub.
    unsigned char tail[] = {0x81,0xC4,0x0C,0x01,0,0,0xE9,0,0,0,0};
    Relative(tail + 6, Address(0x005ACFF6) + 6, reinterpret_cast<uintptr_t>(&NativeFont), 0xE9);
    std::memcpy(reinterpret_cast<void*>(Address(0x005ACFF6)), tail, sizeof(tail));
    unsigned char freeTail[5];
    Relative(freeTail, Address(0x0056EF26), reinterpret_cast<uintptr_t>(&Free), 0xE9);
    std::memcpy(reinterpret_cast<void*>(Address(0x0056EF26)), freeTail, sizeof(freeTail));
    const std::vector<unsigned char> before(image, image + 0xC00000);
    *reinterpret_cast<unsigned char*>(Address(hooks.back().address)) ^= 1;
    const std::vector<unsigned char> mismatch(image, image + 0xC00000);
    Check(!Apply(hooks));
    Check(std::memcmp(image, mismatch.data(), mismatch.size()) == 0);
    std::memcpy(image, before.data(), before.size());
    DWORD old;
    Check(VirtualProtect(image, 0xC00000, PAGE_EXECUTE_READ, &old));
    g_denied = reinterpret_cast<void*>(Address(hooks.back().address));
    Check(!Apply(hooks));
    Check(std::memcmp(image, before.data(), before.size()) == 0);
    for (const Hook& hook : hooks) {
        MEMORY_BASIC_INFORMATION info{};
        VirtualQuery(reinterpret_cast<void*>(Address(hook.address)), &info, sizeof(info));
        Check(info.Protect == PAGE_EXECUTE_READ);
        Check(hook.trampoline == nullptr);
    }
    g_denied = nullptr;
    Check(Apply(hooks));
    for (const Hook& hook : hooks) {
        const auto* code = reinterpret_cast<const unsigned char*>(Address(hook.address));
        Check(code[0] == (hook.call ? 0xE8 : 0xE9));
        int32_t displacement;
        std::memcpy(&displacement, code + 1, 4);
        Check(Address(hook.address) + 5 + displacement == reinterpret_cast<uintptr_t>(hook.destination));
    }
    // Data globals occupy separate pages from hooks in the synthetic image.
    Check(VirtualProtect(reinterpret_cast<void*>(Address(0x00F65000)), 0x1000, PAGE_READWRITE, &old));
    g_loadFile = LoadFile;
    g_texture = Texture;
    g_release = Release;
    const auto patchedLoad = reinterpret_cast<LoadFontFn>(Address(0x005ACFF0));
    const auto patchedFree = reinterpret_cast<FreeFn>(Address(0x0056EF20));
    void* object = reinterpret_cast<void*>(0x12345678);
    for (uint32_t side : {256u, 1024u, 512u, 256u}) {
        auto file = Fixture(side, 2);
        g_expectedFile = file.data();
        g_fileResult = file.data();
        g_fileBytes = file.size();
        Check(HookLoadFile("Fonts\\Mixed.FNT") == file.data());
        const int first = g_uploads;
        Check(patchedLoad(object, file.data()) == 1234);
        Check(g_upload == nullptr);
        Check(g_uploads == first + 2);
        for (int p = 0; p < 2; ++p) {
            Check(g_sizes[first + p] == static_cast<int>(side));
            Check(g_sources[first + p] == file.data() + fnt_layout::kMetadataBytes + static_cast<size_t>(p) * side * side * 4);
            Check(g_formats[first + p] == (side == 256 ? 0x140001u : 0x240001u));
        }
        // Repeated load uses recorded metadata; release removes it before the
        // allocator can reuse the address for another file size/type.
        size_t recorded = 0;
        bool full = false;
        Check(FileSize(file.data(), recorded, full) && recorded == file.size());
        patchedFree(file.data());
        Check(!FileSize(file.data(), recorded, full));
        Check(g_lastFree == file.data());
    }
    auto file = Fixture(1024, 2);
    g_expectedFile = file.data(); g_fileResult = file.data(); g_fileBytes = file.size();
    HookLoadFile("test.fnt");
    g_failHd = true;
    const int first = g_uploads;
    Check(patchedLoad(object, file.data()) == 1234);
    Check(g_sizes[first] == 1024 && g_sizes[first + 1] == 256);
    Check(g_sizes[first + 2] == 1024 && g_sizes[first + 3] == 256);
    Check(g_released == 2);
    g_failHd = false;
    g_nullHd = true;
    const int nullFirst = g_uploads;
    Check(patchedLoad(object, file.data()) == 1234);
    Check(g_sizes[nullFirst] == 1024 && g_sizes[nullFirst + 1] == 256);
    Check(g_sizes[nullFirst + 2] == 1024 && g_sizes[nullFirst + 3] == 256);
    Check(g_released == 2);  // no release of a null texture
    g_nullHd = false;
    const int loaded = g_nativeFonts;
    g_fileBytes = file.size() - 1;
    HookLoadFile("truncated.fnt");
    Check(patchedLoad(object, file.data()) == -1);
    Check(g_nativeFonts == loaded);
    g_fileBytes = file.size(); HookLoadFile("valid.fnt");
    // RAII restores the upload context even if a C++ allocation exception
    // propagates out of native loading.
    g_failContext = true;
    Check(patchedLoad(object, file.data()) == -1);
    Check(g_upload == nullptr);
    g_failContext = false;
    HookLoadFile("reused-for-another-type.bin");
    size_t recorded;
    bool full;
    Check(!FileSize(file.data(), recorded, full));
    for (size_t i = 0; i < g_files.size(); ++i)
        Check(Track(reinterpret_cast<void*>(i + 1), 0));
    Check(!Track(reinterpret_cast<void*>(0xABCDEF), 123));
    Check(patchedLoad(object, file.data()) == -1);
    for (Hook& hook : hooks) if (hook.trampoline) VirtualFree(hook.trampoline, 0, MEM_RELEASE);
    VirtualFree(image, 0, MEM_RELEASE);
    std::puts("Dynamic FNT backend tests passed.");
}
