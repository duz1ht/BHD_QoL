#include "raw_input_packets.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
void Check(bool condition) {
    if (!condition) {
        std::fputs("Raw Input packet regression failed\n", stderr);
        std::abort();
    }
}

void Store32(unsigned char* bytes, size_t offset, uint32_t value) {
    std::memcpy(bytes + offset, &value, sizeof(value));
}

void WriteMouse(unsigned char* bytes, size_t headerSize, LONG x, LONG y,
                USHORT buttons = 0, SHORT wheel = 0) {
    Store32(bytes, 0, RIM_TYPEMOUSE);
    Store32(bytes, 4, static_cast<uint32_t>(headerSize + sizeof(RAWMOUSE)));
    RAWMOUSE mouse = {};
    mouse.usButtonFlags = buttons;
    mouse.usButtonData = static_cast<USHORT>(wheel);
    mouse.lLastX = x;
    mouse.lLastY = y;
    std::memcpy(bytes + headerSize, &mouse, sizeof(mouse));
}

void CheckLayout(size_t headerSize, size_t alignment) {
    const size_t stride = headerSize + sizeof(RAWMOUSE);
    std::array<unsigned char, 192> buffer = {};
    WriteMouse(buffer.data(), headerSize, 123, -456, RI_MOUSE_LEFT_BUTTON_DOWN);
    WriteMouse(buffer.data() + stride, headerSize, -7, 8, RI_MOUSE_LEFT_BUTTON_UP);
    WriteMouse(buffer.data() + 2 * stride, headerSize, 0, 0, RI_MOUSE_WHEEL, -120);
    std::vector<RAWMOUSE> mice;
    Check(raw_input::VisitRawInputPackets(buffer.data(), 3 * stride, 3,
        headerSize, alignment, [&](const RAWMOUSE& mouse) { mice.push_back(mouse); }));
    Check(mice.size() == 3);
    Check(mice[0].lLastX == 123 && mice[0].lLastY == -456);
    Check(mice[0].usButtonFlags == RI_MOUSE_LEFT_BUTTON_DOWN);
    Check(mice[1].lLastX == -7 && mice[1].lLastY == 8);
    Check(mice[1].usButtonFlags == RI_MOUSE_LEFT_BUTTON_UP);
    Check(mice[2].usButtonFlags == RI_MOUSE_WHEEL);
    Check(static_cast<SHORT>(mice[2].usButtonData) == -120);

    // A movement-only packet must never acquire button or wheel bits from the
    // header/padding. This reproduces the WOW64 offset bug in the old reader.
    buffer.fill(0);
    Store32(buffer.data(), 20, 0xFFFF0401);
    WriteMouse(buffer.data(), 24, 4097, -8193);
    Check(raw_input::VisitRawInputPackets(buffer.data(), 48, 1, 24, 8,
        [](const RAWMOUSE& mouse) {
            Check(mouse.usButtonFlags == 0 && mouse.usButtonData == 0);
            Check(mouse.lLastX == 4097 && mouse.lLastY == -8193);
        }));
}
}  // namespace

int main() {
    CheckLayout(16, 4);  // native Win32 / individual GetRawInputData reports
    CheckLayout(24, 8);  // WOW64 buffered reports / native Win64
    Check(raw_input::BufferedHeaderSize(false) == sizeof(RAWINPUTHEADER));
    Check(raw_input::BufferedHeaderSize(true) == 24);

    std::array<unsigned char, 128> bytes = {};
    unsigned int visited = 0;
    const auto visit = [&](const RAWMOUSE&) { ++visited; };
    WriteMouse(bytes.data(), 24, 1, 2);
    Check(!raw_input::VisitRawInputPackets(bytes.data(), 47, 1, 24, 8, visit));
    Check(visited == 0);
    Store32(bytes.data(), 4, 0);
    Check(!raw_input::VisitRawInputPackets(bytes.data(), bytes.size(), 1, 24, 8, visit));
    Store32(bytes.data(), 4, UINT32_MAX);
    Check(!raw_input::VisitRawInputPackets(bytes.data(), bytes.size(), 1, 24, 8, visit));
    Store32(bytes.data(), 4, 24);  // header without a mouse payload
    Check(!raw_input::VisitRawInputPackets(bytes.data(), bytes.size(), 1, 24, 8, visit));
    Check(visited == 0);
    Check(!raw_input::VisitRawInputPackets(nullptr, 0, 1, 24, 8, visit));
    Check(!raw_input::VisitRawInputPackets(bytes.data(), 7, 1, 24, 8, visit));
    Check(!raw_input::VisitRawInputPackets(bytes.data(), bytes.size(), 1, 0, 8, visit));
    Check(!raw_input::VisitRawInputPackets(bytes.data(), bytes.size(), 1, 24, 0, visit));

    // Skip variable-sized non-mouse records, including their alignment padding.
    Store32(bytes.data(), 0, RIM_TYPEHID);
    Store32(bytes.data(), 4, 33);
    WriteMouse(bytes.data() + 40, 24, 9, -9);
    Check(raw_input::VisitRawInputPackets(bytes.data(), 88, 2, 24, 8, visit));
    Check(visited == 1);
    Check(!raw_input::VisitRawInputPackets(bytes.data(), 33, 2, 24, 8, visit));
    Check(!raw_input::VisitRawInputPackets(bytes.data(), 88, 3, 24, 8, visit));

    // A sustained stream remains a bounded walk over each supplied batch.
    std::array<unsigned char, 48 * 256> batch = {};
    for (size_t i = 0; i < 256; ++i) WriteMouse(batch.data() + i * 48, 24, 1, -1);
    int64_t totalX = 0;
    int64_t totalY = 0;
    for (int i = 0; i < 1000; ++i) {
        Check(raw_input::VisitRawInputPackets(batch.data(), batch.size(), 256, 24, 8,
            [&](const RAWMOUSE& mouse) { totalX += mouse.lLastX; totalY += mouse.lLastY; }));
    }
    Check(totalX == 256000 && totalY == -256000);
    return 0;
}
