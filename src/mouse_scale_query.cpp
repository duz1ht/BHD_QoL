#include "mouse_scale_query.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace mouse_scale_query {
namespace {
constexpr uintptr_t kParserAddress = 0x00460260;
constexpr uintptr_t kMouseScaleAddress = 0x009F20E4;
constexpr size_t kHookLength = 9;
constexpr unsigned char kParserPrologue[kHookLength] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x1C, 0x01, 0x00, 0x00};

using Parser = void(__cdecl *)(char *);
Parser g_originalParser = nullptr;

bool IsSpace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

bool IsBareMouseScale(const char *command) {
  if (command == nullptr)
    return false;
  while (IsSpace(*command))
    ++command;
  constexpr char kName[] = "mousescale";
  for (size_t index = 0; index < sizeof(kName) - 1; ++index) {
    char actual = *command++;
    if (actual >= 'A' && actual <= 'Z')
      actual = static_cast<char>(actual - 'A' + 'a');
    if (actual != kName[index])
      return false;
  }
  while (IsSpace(*command))
    ++command;
  return *command == '\0';
}

void __cdecl ParseCommand(char *command) {
  if (IsBareMouseScale(command)) {
    const int32_t scale =
        *reinterpret_cast<volatile int32_t *>(kMouseScaleAddress);
    if (scale >= 1 && scale <= 511) {
      char query[32] = {};
      std::snprintf(query, sizeof(query), "mousescale %d", scale);
      g_originalParser(query);
      return;
    }
  }
  g_originalParser(command);
}

bool WriteJump(unsigned char *output, const void *source,
               const void *destination) {
  const intptr_t displacement =
      reinterpret_cast<intptr_t>(destination) -
      (reinterpret_cast<intptr_t>(source) + 5);
  if (displacement < std::numeric_limits<int32_t>::min() ||
      displacement > std::numeric_limits<int32_t>::max())
    return false;
  output[0] = 0xE9;
  const int32_t relative = static_cast<int32_t>(displacement);
  memcpy(output + 1, &relative, sizeof(relative));
  return true;
}
} // namespace

bool Install(bool enabled) {
  if (!enabled || g_originalParser != nullptr)
    return false;

  auto *parser = reinterpret_cast<unsigned char *>(kParserAddress);
  if (memcmp(parser, kParserPrologue, kHookLength) != 0)
    return false;

  auto *trampoline = static_cast<unsigned char *>(VirtualAlloc(
      nullptr, kHookLength + 5, MEM_COMMIT | MEM_RESERVE,
      PAGE_EXECUTE_READWRITE));
  if (trampoline == nullptr)
    return false;

  memcpy(trampoline, parser, kHookLength);
  unsigned char jumpToHook[5];
  if (!WriteJump(trampoline + kHookLength, trampoline + kHookLength,
                 parser + kHookLength) ||
      !WriteJump(jumpToHook, parser,
                 reinterpret_cast<const void *>(&ParseCommand))) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }

  DWORD previousProtection = 0;
  if (!VirtualProtect(parser, kHookLength, PAGE_EXECUTE_READWRITE,
                      &previousProtection)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }

  g_originalParser = reinterpret_cast<Parser>(trampoline);
  memcpy(parser, jumpToHook, sizeof(jumpToHook));
  memset(parser + sizeof(jumpToHook), 0x90,
         kHookLength - sizeof(jumpToHook));
  FlushInstructionCache(GetCurrentProcess(), parser, kHookLength);
  DWORD ignored = 0;
  VirtualProtect(parser, kHookLength, previousProtection, &ignored);
  return true;
}
} // namespace mouse_scale_query
