#include "fnt_layout.h"

#include <cmath>
#include <cstring>

namespace fnt_layout {
namespace {
uint32_t Read32(const unsigned char* pointer) {
    uint32_t value;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}
}  // namespace

bool Detect(const void* data, size_t fileBytes, Layout& layout) {
    layout = {};
    if (!data || fileBytes < kMetadataBytes) return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (Read32(bytes) != 0x30544E46 || Read32(bytes + 4) == 0) return false;
    const uint32_t pages = Read32(bytes + 8);
    if (pages == 0 || pages > kMaximumPages) return false;
    const size_t payload = fileBytes - kMetadataBytes;
    if (payload % pages != 0) return false;
    const size_t pageBytes = payload / pages;
    uint32_t side = 256;
    while (side <= 4096 && static_cast<size_t>(side) * side * 4 != pageBytes) side *= 2;
    if (side > 4096) return false;
    static_assert(sizeof(Glyph) == 20, "FNT0 glyph entries are 20 bytes");
    for (size_t index = 0; index < 224; ++index) {
        Glyph glyph;
        std::memcpy(&glyph, bytes + 32 + index * sizeof(glyph), sizeof(glyph));
        if (glyph.page >= pages || !std::isfinite(glyph.left) || !std::isfinite(glyph.top) ||
            !std::isfinite(glyph.right) || !std::isfinite(glyph.bottom) ||
            glyph.left < 0 || glyph.top < 0 || glyph.right > 1 || glyph.bottom > 1 ||
            glyph.left > glyph.right || glyph.top > glyph.bottom) return false;
    }
    layout = {side, pages, pageBytes, fileBytes};
    return true;
}

bool MakeLogicalCopy(const void* data, size_t fileBytes, const Layout& layout,
                     std::vector<unsigned char>& copy) {
    copy.clear();
    // Check the supplied descriptor before using it to address input/output.
    Layout detected;
    if (!Detect(data, fileBytes, detected) || detected.side != layout.side ||
        detected.pages != layout.pages || detected.pageBytes != layout.pageBytes ||
        detected.fileBytes != layout.fileBytes) return false;
    copy.resize(kMetadataBytes + layout.pages * kLogicalPageBytes);
    const auto* source = static_cast<const unsigned char*>(data);
    std::memcpy(copy.data(), source, kMetadataBytes);
    if (layout.side == 256) {
        std::memcpy(copy.data() + kMetadataBytes, source + kMetadataBytes, fileBytes - kMetadataBytes);
        return true;
    }
    const uint32_t factor = layout.side / 256;
    const uint32_t samples = factor * factor;
    for (uint32_t page = 0; page < layout.pages; ++page) {
        const auto* pixels = source + layout.PageOffset(page);
        auto* target = copy.data() + kMetadataBytes + page * kLogicalPageBytes;
        for (uint32_t y = 0; y < 256; ++y) for (uint32_t x = 0; x < 256; ++x) {
            uint32_t alpha = 0, red = 0, green = 0, blue = 0;
            // Average premultiplied channels, then store straight alpha as the
            // original renderer expects. Transparent colors cannot bleed into
            // visible outlines in the legacy CPU mirror or GPU fallback.
            for (uint32_t dy = 0; dy < factor; ++dy) for (uint32_t dx = 0; dx < factor; ++dx) {
                const size_t index = static_cast<size_t>(y * factor + dy) * layout.side + x * factor + dx;
                const uint32_t color = Read32(pixels + index * 4);
                const uint32_t a = color >> 24;
                alpha += a;
                red += ((color >> 16) & 255) * a;
                green += ((color >> 8) & 255) * a;
                blue += (color & 255) * a;
            }
            uint32_t color = 0x00FFFFFF;
            if (alpha) color = (((alpha + samples / 2) / samples) << 24) |
                (((red + alpha / 2) / alpha) << 16) |
                (((green + alpha / 2) / alpha) << 8) | ((blue + alpha / 2) / alpha);
            std::memcpy(target + (y * 256 + x) * 4, &color, 4);
        }
    }
    return true;
}
}  // namespace fnt_layout
