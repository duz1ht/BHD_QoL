#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fnt_layout {
constexpr size_t kMetadataBytes = 32 + 224 * 20;
constexpr size_t kLogicalPageBytes = 256 * 256 * 4;
constexpr uint32_t kMaximumPages = 16;
struct Glyph {
    uint32_t page;
    float left, top, right, bottom;
};
struct Layout {
    uint32_t side = 0;
    uint32_t pages = 0;
    size_t pageBytes = 0;
    size_t fileBytes = 0;
    size_t PageOffset(uint32_t page) const { return kMetadataBytes + page * pageBytes; }
};
// FNT0 contains no physical page dimensions. Detect square 32-bit pages from
// the exact file length, never from heap capacity or VirtualQuery region size.
bool Detect(const void* data, size_t fileBytes, Layout& layout);
// The native renderer uses 256 logical units and normalized UVs regardless of
// physical texture resolution. Its optional CPU drawing path also needs a
// 256x256 mirror; this staging file supplies that mirror without shrinking GPU
// uploads. The backend substitutes the original HD page at texture creation.
bool MakeLogicalCopy(const void* data, size_t fileBytes, const Layout& layout,
                     std::vector<unsigned char>& copy);
}  // namespace fnt_layout
