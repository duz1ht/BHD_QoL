#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace raw_input {

// GetRawInputData uses the process layout. GetRawInputBuffer uses the native
// layout, including a 24-byte header for a 32-bit process running under WOW64.
inline size_t BufferedHeaderSize(bool wow64) {
    return sizeof(RAWINPUTHEADER) + (wow64 && sizeof(void*) == 4 ? 8 : 0);
}

template <typename MouseHandler>
bool VisitRawInputPackets(const void* buffer, size_t capacity, UINT count,
                         size_t headerSize, size_t alignment, MouseHandler onMouse) {
    if (buffer == nullptr || headerSize < 8 ||
        (alignment != 4 && alignment != 8)) return false;
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    size_t offset = 0;
    for (UINT index = 0; index < count; ++index) {
        if (capacity - offset < headerSize) return false;
        uint32_t type = 0;
        uint32_t size = 0;
        std::memcpy(&type, bytes + offset, sizeof(type));
        std::memcpy(&size, bytes + offset + sizeof(type), sizeof(size));
        if (size < headerSize || size > capacity - offset) return false;
        if (type == RIM_TYPEMOUSE) {
            if (size - headerSize < sizeof(RAWMOUSE)) return false;
            RAWMOUSE mouse = {};
            std::memcpy(&mouse, bytes + offset + headerSize, sizeof(mouse));
            onMouse(mouse);
        }
        if (index + 1 == count) return true;
        const size_t padding = (alignment - size % alignment) % alignment;
        if (padding > capacity - offset - size) return false;
        offset += size + padding;
    }
    return true;
}

}  // namespace raw_input
