#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace camera_position {
// Builds an x86 mid-function hook. Protect integer registers, flags and the full
// x87/SSE state; align both the FXSAVE area and callback's argument area to 16.
// With a filter, all non-player objects skip the callback and FXSAVE entirely.
inline size_t BuildCaptureStub(unsigned char* output, uintptr_t address,
                               uintptr_t callback, uintptr_t continuation,
                               const unsigned char original[5],
                               uintptr_t playerPointer, bool phase, uint32_t cadence) {
    size_t n = 0;
    const auto byte = [&](unsigned char b) { output[n++] = b; };
    const auto word = [&](uint32_t v) { std::memcpy(output + n, &v, 4); n += 4; };
    byte(0x9C);  // pushfd
    size_t skip = 0;
    if (playerPointer != 0) {
        byte(0x3B); byte(0x35); word(static_cast<uint32_t>(playerPointer)); // cmp esi,[player]
        byte(0x75); skip = n++; // jne restoreFlags
    }
    byte(0x60);                         // pushad
    byte(0x8B); byte(0xEC);              // mov ebp,esp
    byte(0x81); byte(0xEC); word(0x210);  // sub esp,528
    byte(0x83); byte(0xE4); byte(0xF0);  // and esp,-16
    byte(0x0F); byte(0xAE); byte(0x04); byte(0x24); // fxsave [esp]
    byte(0x83); byte(0xEC); byte(0x10);  // sub esp,16 (arguments)
    byte(0x89); byte(phase ? 0x1C : 0x34); byte(0x24); // mov [esp],ebx/esi
    if (!phase) {
        byte(0xC7); byte(0x44); byte(0x24); byte(0x04); word(cadence);
    }
    byte(0xB8); word(static_cast<uint32_t>(callback)); // mov eax,callback
    byte(0xFF); byte(0xD0);              // call eax
    byte(0x83); byte(0xC4); byte(0x10);  // add esp,16
    byte(0x0F); byte(0xAE); byte(0x0C); byte(0x24); // fxrstor [esp]
    byte(0x8B); byte(0xE5);              // mov esp,ebp
    byte(0x61);                         // popad
    if (skip != 0) output[skip] = static_cast<unsigned char>(n - skip - 1);
    byte(0x9D);                         // popfd
    for (size_t i = 0; i < 5; ++i) byte(original[i]);
    byte(0xE9);
    const uint32_t displacement = static_cast<uint32_t>(continuation - (address + n + 4));
    word(displacement);
    return n;
}
}  // namespace camera_position
