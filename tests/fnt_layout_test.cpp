#include "fnt_layout.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace {
void Require(bool condition, int line) {
    if (!condition) { std::fprintf(stderr, "FNT layout regression at line %d\n", line); std::exit(1); }
}
#define Check(condition) Require((condition), __LINE__)
void Put(std::vector<unsigned char>& data, size_t offset, uint32_t value) {
    std::memcpy(data.data() + offset, &value, 4);
}
std::vector<unsigned char> Fixture(uint32_t side, uint32_t pages) {
    std::vector<unsigned char> result(fnt_layout::kMetadataBytes + static_cast<size_t>(side) * side * 4 * pages);
    Put(result, 0, 0x30544E46);
    Put(result, 4, 800);
    Put(result, 8, pages);
    Put(result, 12, static_cast<uint32_t>(-3));
    for (size_t i = 0; i < 224; ++i) {
        const fnt_layout::Glyph glyph{static_cast<uint32_t>(i % pages), 0, 0, 0.25f, 0.25f};
        std::memcpy(result.data() + 32 + i * 20, &glyph, 20);
    }
    return result;
}
void ValidateStock(const std::filesystem::path& folder) {
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != ".fnt") continue;
        std::ifstream input(entry.path(), std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
        fnt_layout::Layout layout;
        Check(fnt_layout::Detect(bytes.data(), bytes.size(), layout));
        Check(layout.side == 256);
        std::vector<unsigned char> mirror;
        Check(fnt_layout::MakeLogicalCopy(bytes.data(), bytes.size(), layout, mirror));
        Check(mirror == bytes);
        ++count;
    }
    Check(count == 23);
    std::puts("All 23 original FNT resources validated byte-for-byte.");
}
}
int main(int argc, char** argv) {
    using namespace fnt_layout;
    Layout layout;
    Check(!Detect(nullptr, 0, layout));
    // Alternating sizes and page counts exercise per-file detection without any
    // process-wide atlas setting. Payload bytes use different page markers.
    for (uint32_t side : {256u, 1024u, 512u, 2048u, 256u, 4096u}) {
        auto bytes = Fixture(side, 2);
        Check(Detect(bytes.data(), bytes.size(), layout));
        Check(layout.side == side && layout.pages == 2);
        Check(layout.pageBytes == static_cast<size_t>(side) * side * 4);
        Check(layout.PageOffset(1) == kMetadataBytes + layout.pageBytes);
        Put(bytes, kMetadataBytes, 0xFF112233);
        Put(bytes, layout.PageOffset(1), 0xFFAABBCC);
        const auto original = bytes;
        std::vector<unsigned char> logical;
        Check(MakeLogicalCopy(bytes.data(), bytes.size(), layout, logical));
        Check(logical.size() == kMetadataBytes + 2 * kLogicalPageBytes);
        Check(std::memcmp(bytes.data(), logical.data(), kMetadataBytes) == 0);
        Check(bytes == original);
        Check(!Detect(bytes.data(), bytes.size() - 1, layout));
        bytes.push_back(0);
        Check(!Detect(bytes.data(), bytes.size(), layout));
    }
    auto bytes = Fixture(1024, 2);
    Check(Detect(bytes.data(), bytes.size(), layout));
    // Each 4x4 source block has one opaque red pixel and transparent blue.
    // The mirror must be red with alpha 16, without blue color bleeding.
    for (uint32_t page = 0; page < 2; ++page)
        for (uint32_t y = 0; y < 1024; ++y) for (uint32_t x = 0; x < 1024; ++x)
            Put(bytes, layout.PageOffset(page) + (y * 1024 + x) * 4,
                x % 4 == 0 && y % 4 == 0 ? (page ? 0xFF00FF00 : 0xFFFF0000) : 0x000000FF);
    std::vector<unsigned char> logical;
    Check(MakeLogicalCopy(bytes.data(), bytes.size(), layout, logical));
    uint32_t first = 0, second = 0;
    std::memcpy(&first, logical.data() + kMetadataBytes, 4);
    std::memcpy(&second, logical.data() + kMetadataBytes + kLogicalPageBytes, 4);
    Check(first == 0x10FF0000 && second == 0x1000FF00);
    auto wrong = layout;
    wrong.pageBytes = 0;
    Check(!MakeLogicalCopy(bytes.data(), bytes.size(), wrong, logical));
    Put(bytes, 8, 17);
    Check(!Detect(bytes.data(), bytes.size(), layout));
    Put(bytes, 8, 0);
    Check(!Detect(bytes.data(), bytes.size(), layout));
    Put(bytes, 8, 2);
    Put(bytes, 4, 0);
    Check(!Detect(bytes.data(), bytes.size(), layout));
    Put(bytes, 4, 800);
    const Glyph invalid{0, 0, 0, std::numeric_limits<float>::quiet_NaN(), 1};
    std::memcpy(bytes.data() + 32, &invalid, 20);
    Check(!Detect(bytes.data(), bytes.size(), layout));
    bytes = Fixture(768, 1);
    Check(!Detect(bytes.data(), bytes.size(), layout));
    if (argc > 1) ValidateStock(argv[1]);
    std::puts("FNT layout tests passed.");
}
