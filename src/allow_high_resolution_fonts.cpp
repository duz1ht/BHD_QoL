#include "allow_high_resolution_fonts.h"
#include "fnt_layout.h"

#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace allow_high_resolution_fonts {
namespace {
constexpr uintptr_t kImageBase = 0x00400000;
uintptr_t g_base = 0;
bool g_installed = false;
uintptr_t Address(uintptr_t va) { return g_base + va - kImageBase; }

using LoadFileFn = void*(__cdecl*)(const char*);
using FreeFn = void(__cdecl*)(void*);
using LoadFontFn = int(__thiscall*)(void*, const void*);
using TextureFn = void*(__cdecl*)(const char*, const void*, int, int, int, uint32_t);
using ReleaseFn = int(__cdecl*)(void*);
LoadFileFn g_loadFile = nullptr;
FreeFn g_free = nullptr;
LoadFontFn g_loadFont = nullptr;
TextureFn g_texture = nullptr;
ReleaseFn g_release = nullptr;

// Only exact lengths returned by the native file loader are retained. Its
// allocator rounds allocations, so allocator/VirtualQuery capacities are not
// file lengths. Fixed storage also avoids allocating in the game-free hook.
struct FileRecord { const void* data = nullptr; size_t bytes = 0; };
std::array<FileRecord, 256> g_files{};
SRWLOCK g_filesLock = SRWLOCK_INIT;
std::atomic<unsigned int> g_fileCount{0};
bool g_trackingFull = false;

bool Readable(const void* pointer, size_t bytes) {
    uintptr_t current = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t end = current + bytes;
    if (!current || end < current) return false;
    while (current < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(current), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= current) return false;
        current = next;
    }
    return true;
}
bool IsFontName(const char* name) {
    if (!name) return false;
    const char* extension = std::strrchr(name, '.');
    return extension && _stricmp(extension, ".fnt") == 0;
}
void Forget(const void* pointer) {
    if (!pointer || g_fileCount.load(std::memory_order_relaxed) == 0) return;
    AcquireSRWLockExclusive(&g_filesLock);
    for (FileRecord& record : g_files) if (record.data == pointer) {
        record = {};
        g_fileCount.fetch_sub(1, std::memory_order_relaxed);
        break;
    }
    ReleaseSRWLockExclusive(&g_filesLock);
}
bool Track(const void* pointer, size_t bytes) {
    AcquireSRWLockExclusive(&g_filesLock);
    FileRecord* available = nullptr;
    for (FileRecord& record : g_files) {
        if (record.data == pointer) { available = &record; break; }
        if (!record.data && !available) available = &record;
    }
    if (available) {
        if (!available->data) g_fileCount.fetch_add(1, std::memory_order_relaxed);
        *available = {pointer, bytes};
    }
    else g_trackingFull = true;
    ReleaseSRWLockExclusive(&g_filesLock);
    return available != nullptr;
}
bool FileSize(const void* pointer, size_t& bytes, bool& full) {
    bool found = false;
    AcquireSRWLockShared(&g_filesLock);
    full = g_trackingFull;
    for (const FileRecord& record : g_files) if (record.data == pointer && pointer) {
        bytes = record.bytes;
        found = true;
        break;
    }
    ReleaseSRWLockShared(&g_filesLock);
    return found;
}
void* __cdecl HookLoadFile(const char* name) {
    void* data = g_loadFile(name);
    // This global is set to the exact loaded byte count for loose files and PFF
    // entries (including decryption). Read it before calling anything else.
    const uint32_t bytes = *reinterpret_cast<const uint32_t*>(Address(0x00F65684));
    if (!data || data == reinterpret_cast<void*>(static_cast<uintptr_t>(-1))) return data;
    Forget(data);
    if (IsFontName(name) && !Track(data, bytes))
        OutputDebugStringA("[BHD Fonts] File tracking full; untracked fonts will be rejected.\n");
    return data;
}
void __cdecl HookFree(void* data) {
    Forget(data);
    g_free(data);
}

struct UploadContext {
    const unsigned char* original;
    const unsigned char* logical;
    fnt_layout::Layout layout;
};
thread_local const UploadContext* g_upload = nullptr;
struct UploadScope {
    const UploadContext* previous = g_upload;
    explicit UploadScope(const UploadContext* current) { g_upload = current; }
    ~UploadScope() { g_upload = previous; }
};

void* __cdecl HookTexture(const char* name, const void* data, int width, int height,
                          int unused, uint32_t flags) {
    const UploadContext* context = g_upload;
    if (context && width == 256 && height == 256) {
        for (uint32_t page = 0; page < context->layout.pages; ++page) {
            if (data != context->logical + fnt_layout::kMetadataBytes + page * fnt_layout::kLogicalPageBytes) continue;
            const int side = static_cast<int>(context->layout.side);
            const void* physicalPixels = context->original + context->layout.PageOffset(page);
            // The native texture path still owns/cache-manages the resource.
            // Preserve the external page's 32-bit alpha instead of quantizing
            // new HD assets to the stock A4R4G4B4 texture format.
            void* texture = g_texture(name, physicalPixels, side, side, unused,
                                      (flags & ~0x100000u) | 0x200000u);
            if (texture) {
                const auto* fields = static_cast<const uint32_t*>(texture);
                if (fields[2]) return texture;  // native managed D3D texture
                g_release(texture);
            }
            OutputDebugStringA("[BHD Fonts] HD texture unavailable; using logical 256x256 mirror.\n");
            break;
        }
    }
    return g_texture(name, data, width, height, unused, flags);
}

int __fastcall HookLoadFont(void* font, void*, const void* data) {
    size_t bytes = 0;
    bool full = false;
    if (!FileSize(data, bytes, full)) {
        if (full) return -1;
        // Preserve the native contract for in-memory fonts that did not pass
        // through LoadFile. No size is guessed for these opaque buffers.
        UploadScope scope(nullptr);
        return g_loadFont(font, data);
    }
    fnt_layout::Layout layout;
    if (!Readable(data, bytes) || !fnt_layout::Detect(data, bytes, layout)) {
        OutputDebugStringA("[BHD Fonts] Rejected FNT0: invalid length, pages, dimensions or glyph table.\n");
        return -1;
    }
    if (layout.side == 256) {
        UploadScope scope(nullptr);
        return g_loadFont(font, data);  // identical stock path, no staging copy
    }
    try {
        std::vector<unsigned char> logical;
        if (!fnt_layout::MakeLogicalCopy(data, bytes, layout, logical)) return -1;
        const UploadContext context{static_cast<const unsigned char*>(data), logical.data(), layout};
        UploadScope scope(&context);
        // The native loader copies the unchanged metadata, creates materials,
        // and copies any requested CPU pages from the logical staging file.
        // Only its GPU upload receives the original physical-sized pages.
        return g_loadFont(font, logical.data());
    } catch (const std::bad_alloc&) {
        OutputDebugStringA("[BHD Fonts] Not enough staging memory; FNT0 load rejected.\n");
        return -1;
    }
}

struct Hook {
    uintptr_t address;
    std::array<unsigned char, 6> expected;
    size_t length;
    const void* destination;
    bool call;
    unsigned char* trampoline = nullptr;
    DWORD protection = 0;
};
void Relative(unsigned char* output, uintptr_t origin, uintptr_t target, unsigned char opcode) {
    output[0] = opcode;
    const uint32_t displacement = static_cast<uint32_t>(target - origin - 5);
    std::memcpy(output + 1, &displacement, 4);
}
bool Matches(uintptr_t address, const unsigned char* signature, size_t length) {
    return Readable(reinterpret_cast<void*>(address), length) &&
           std::memcmp(reinterpret_cast<void*>(address), signature, length) == 0;
}
bool ValidateNative() {
    const unsigned char texture[] = {0x64,0xA1,0,0,0,0,0x6A,0xFF,0x68,0x6B,0x32,0x5E,0};
    const unsigned char release[] = {0x56,0x8B,0x74,0x24,0x08,0x85,0xF6};
    return Matches(Address(0x005A9940), texture, sizeof(texture)) &&
           Matches(Address(0x005A9A90), release, sizeof(release));
}
std::vector<Hook> BuildHooks() {
    // Whole non-relative entry instructions, verified in the reference x86 EXE.
    return {
        {0x00564890, {0xA1,0x94,0x56,0xF6,0}, 5, reinterpret_cast<const void*>(&HookLoadFile), false},
        {0x0056EF20, {0x8B,0x44,0x24,0x04,0x85,0xC0}, 6, reinterpret_cast<const void*>(&HookFree), false},
        {0x005ACFF0, {0x81,0xEC,0x0C,0x01,0,0}, 6, reinterpret_cast<const void*>(&HookLoadFont), false},
        {0x005AD0C2, {0xE8,0x79,0xC8,0xFF,0xFF}, 5, reinterpret_cast<const void*>(&HookTexture), true},
    };
}
bool Apply(std::vector<Hook>& hooks) {
    for (const Hook& hook : hooks)
        if (!Matches(Address(hook.address), hook.expected.data(), hook.length)) return false;
    for (Hook& hook : hooks) {
        if (hook.call) continue;
        hook.trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, hook.length + 5,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!hook.trampoline) goto fail;
        std::memcpy(hook.trampoline, hook.expected.data(), hook.length);
        Relative(hook.trampoline + hook.length, reinterpret_cast<uintptr_t>(hook.trampoline + hook.length),
                 Address(hook.address) + hook.length, 0xE9);
        DWORD previous;
        if (!VirtualProtect(hook.trampoline, hook.length + 5, PAGE_EXECUTE_READ, &previous)) goto fail;
        FlushInstructionCache(GetCurrentProcess(), hook.trampoline, hook.length + 5);
    }
    // Obtain all permissions before changing code, including hooks sharing a
    // page. Restore in reverse acquisition order on success and on failure.
    for (Hook& hook : hooks)
        if (!VirtualProtect(reinterpret_cast<void*>(Address(hook.address)), hook.length,
                            PAGE_EXECUTE_READWRITE, &hook.protection)) goto fail;
    for (Hook& hook : hooks) {
        if (hook.address == 0x00564890) g_loadFile = reinterpret_cast<LoadFileFn>(hook.trampoline);
        if (hook.address == 0x0056EF20) g_free = reinterpret_cast<FreeFn>(hook.trampoline);
        if (hook.address == 0x005ACFF0) g_loadFont = reinterpret_cast<LoadFontFn>(hook.trampoline);
    }
    for (Hook& hook : hooks) {
        auto* target = reinterpret_cast<unsigned char*>(Address(hook.address));
        Relative(target, Address(hook.address), reinterpret_cast<uintptr_t>(hook.destination), hook.call ? 0xE8 : 0xE9);
        if (hook.length > 5) target[5] = 0x90;
        FlushInstructionCache(GetCurrentProcess(), target, hook.length);
    }
    for (auto it = hooks.rbegin(); it != hooks.rend(); ++it) {
        DWORD ignored;
        VirtualProtect(reinterpret_cast<void*>(Address(it->address)), it->length, it->protection, &ignored);
    }
    return true;
fail:
    for (auto it = hooks.rbegin(); it != hooks.rend(); ++it) {
        if (it->protection) {
            DWORD ignored;
            VirtualProtect(reinterpret_cast<void*>(Address(it->address)), it->length, it->protection, &ignored);
            it->protection = 0;
        }
        if (it->trampoline) { VirtualFree(it->trampoline, 0, MEM_RELEASE); it->trampoline = nullptr; }
    }
    return false;
}
}  // namespace

bool Install(bool enabled) {
    if (!enabled) return false;
    if (g_installed) return true;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (g_base != kImageBase || !ValidateNative()) return false;
    g_texture = reinterpret_cast<TextureFn>(Address(0x005A9940));
    g_release = reinterpret_cast<ReleaseFn>(Address(0x005A9A90));
    auto hooks = BuildHooks();
    if (!Apply(hooks)) {
        OutputDebugStringA("[BHD Fonts] Signature/protection check failed; dynamic FNT loading disabled.\n");
        return false;
    }
    g_installed = true;
    OutputDebugStringA("[BHD Fonts] Dynamic FNT0 atlas dimensions enabled.\n");
    return true;
}
}  // namespace allow_high_resolution_fonts
