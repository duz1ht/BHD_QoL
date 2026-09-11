#include "mouse_scaling_fix.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "logger.h"

namespace mouse_scaling_fix {
namespace {
constexpr uintptr_t kMouseScaleAddress = 0x009F20E4;
constexpr size_t kHookLength = 25;

const unsigned char kScaleBlockSignature[] = {
    0x8B, 0x45, 0xFC,       // mov eax,[ebp-04]
    0xF7, 0x6D, 0xF4,       // imul [ebp-0C]
    0x0F, 0xAC, 0xD0, 0x10, // shrd eax,edx,16
    0x8B, 0xC8,             // mov ecx,eax
    0x89, 0x4D, 0xF8,       // mov [ebp-08],ecx
    0x8B, 0x45, 0xFC,       // mov eax,[ebp-04]
    0xF7, 0x6D, 0xF0,       // imul [ebp-10]
    0x0F, 0xAC, 0xD0, 0x10, // shrd eax,edx,16
};

struct FractionState {
  int32_t scale;
  int64_t remainder[2];
  bool active;
};

FractionState g_state = {};

int32_t OriginalScale(int32_t delta, int32_t scale) {
  const uint64_t product =
      static_cast<uint64_t>(static_cast<int64_t>(delta) * scale);
  return static_cast<int32_t>(product >> 16);
}

extern "C" int32_t __stdcall ScaleWithRemainder(int32_t delta, int32_t scale,
                                                int32_t axis) {
  const int32_t baseScale = static_cast<int32_t>(
      static_cast<uint32_t>(
          *reinterpret_cast<volatile int32_t *>(kMouseScaleAddress))
      << 11);
  const bool needsCorrection = scale != baseScale && (scale & 0xFFFF) != 0;
  if (!needsCorrection) {
    Reset();
    return OriginalScale(delta, scale);
  }

  if (!g_state.active || g_state.scale != scale) {
    g_state.scale = scale;
    g_state.remainder[0] = 0;
    g_state.remainder[1] = 0;
    g_state.active = true;
  }

  const int safeAxis = axis == 0 ? 0 : 1;
  const int64_t value =
      static_cast<int64_t>(delta) * scale + g_state.remainder[safeAxis];
  const int32_t output = static_cast<int32_t>(value / 65536);
  g_state.remainder[safeAxis] = value - static_cast<int64_t>(output) * 65536;
  return output;
}

bool IsExecutableSection(const IMAGE_SECTION_HEADER &section) {
  return (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
}

unsigned char *FindScaleBlock(HMODULE module) {
  auto *base = reinterpret_cast<unsigned char *>(module);
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return nullptr;
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    return nullptr;
  }

  unsigned char *match = nullptr;
  size_t matches = 0;
  const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
  for (unsigned int index = 0; index < nt->FileHeader.NumberOfSections;
       ++index, ++section) {
    if (!IsExecutableSection(*section))
      continue;
    unsigned char *begin = base + section->VirtualAddress;
    const size_t size = section->Misc.VirtualSize;
    if (size < sizeof(kScaleBlockSignature))
      continue;
    for (size_t offset = 0; offset <= size - sizeof(kScaleBlockSignature);
         ++offset) {
      if (memcmp(begin + offset, kScaleBlockSignature,
                 sizeof(kScaleBlockSignature)) == 0) {
        match = begin + offset;
        ++matches;
      }
    }
  }
  return matches == 1 ? match : nullptr;
}

void AppendByte(unsigned char *&output, unsigned char value) {
  *output++ = value;
}

void AppendU32(unsigned char *&output, uint32_t value) {
  memcpy(output, &value, sizeof(value));
  output += sizeof(value);
}

bool AppendRelativeCall(unsigned char *&output, const void *target) {
  AppendByte(output, 0xE8);
  const intptr_t displacement = static_cast<intptr_t>(reinterpret_cast<uintptr_t>(target)) -
                                static_cast<intptr_t>(reinterpret_cast<uintptr_t>(output + 4));
  if (displacement < std::numeric_limits<int32_t>::min() ||
      displacement > std::numeric_limits<int32_t>::max())
    return false;
  AppendU32(output, static_cast<uint32_t>(static_cast<int32_t>(displacement)));
  return true;
}

bool AppendRelativeJump(unsigned char *&output, const void *target) {
  AppendByte(output, 0xE9);
  const intptr_t displacement = static_cast<intptr_t>(reinterpret_cast<uintptr_t>(target)) -
                                static_cast<intptr_t>(reinterpret_cast<uintptr_t>(output + 4));
  if (displacement < std::numeric_limits<int32_t>::min() ||
      displacement > std::numeric_limits<int32_t>::max())
    return false;
  AppendU32(output, static_cast<uint32_t>(static_cast<int32_t>(displacement)));
  return true;
}

bool InstallHook(unsigned char *hook) {
  auto *trampoline = static_cast<unsigned char *>(VirtualAlloc(
      nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (trampoline == nullptr)
    return false;

  unsigned char *output = trampoline;
  const unsigned char firstAxis[] = {0x6A, 0x00, 0xFF, 0x75,
                                     0xFC, 0xFF, 0x75, 0xF4};
  memcpy(output, firstAxis, sizeof(firstAxis));
  output += sizeof(firstAxis);
  if (!AppendRelativeCall(output, reinterpret_cast<const void *>(&ScaleWithRemainder))) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }
  const unsigned char saveX[] = {0x8B, 0xC8, 0x89, 0x4D, 0xF8};
  memcpy(output, saveX, sizeof(saveX));
  output += sizeof(saveX);
  const unsigned char secondAxis[] = {0x51, 0x6A, 0x01, 0xFF, 0x75,
                                      0xFC, 0xFF, 0x75, 0xF0};
  memcpy(output, secondAxis, sizeof(secondAxis));
  output += sizeof(secondAxis);
  if (!AppendRelativeCall(output, reinterpret_cast<const void *>(&ScaleWithRemainder))) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }
  const unsigned char saveY[] = {0x59, 0x89, 0x45, 0xF0};
  memcpy(output, saveY, sizeof(saveY));
  output += sizeof(saveY);
  if (!AppendRelativeJump(output, hook + kHookLength)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }

  DWORD oldProtect = 0;
  if (!VirtualProtect(hook, kHookLength, PAGE_EXECUTE_READWRITE, &oldProtect)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }
  unsigned char *patch = hook;
  const bool jumpWritten = AppendRelativeJump(patch, trampoline);
  if (jumpWritten) {
    while (patch < hook + kHookLength)
      *patch++ = 0x90;
    FlushInstructionCache(GetCurrentProcess(), hook, kHookLength);
  }
  DWORD ignored = 0;
  VirtualProtect(hook, kHookLength, oldProtect, &ignored);
  if (!jumpWritten) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }

  logger::Log("INFO", "MouseScalingFix",
              "hook installed address=0x%08lX trampoline=0x%08lX",
              reinterpret_cast<unsigned long>(hook),
              reinterpret_cast<unsigned long>(trampoline));
  return true;
}
} // namespace

void Reset() {
  g_state.scale = 0;
  g_state.remainder[0] = 0;
  g_state.remainder[1] = 0;
  g_state.active = false;
}

bool Install(bool enabled) {
  if (!enabled) {
    logger::Log("INFO", "MouseScalingFix", "feature disabled");
    return false;
  }
  logger::Log("INFO", "MouseScalingFix", "feature enabled");
  unsigned char *hook = FindScaleBlock(GetModuleHandleW(nullptr));
  if (hook == nullptr) {
    logger::Log("ERROR", "MouseScalingFix",
                "expected a unique scoped scaling signature");
    return false;
  }
  if (!InstallHook(hook)) {
    logger::Log("ERROR", "MouseScalingFix", "hook installation failed: error=%lu",
                GetLastError());
    return false;
  }
  return true;
}

} // namespace mouse_scaling_fix
