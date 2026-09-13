#pragma once

#include <cstdlib>

namespace cursor_clip {

struct RectEdges {
    long left;
    long top;
    long right;
    long bottom;
};

// Win32 coordinate conversion can differ by one pixel at a client edge.
constexpr long kClientEdgeTolerance = 1;

inline bool CoversFullClient(const RectEdges& clip, const RectEdges& client,
                             long tolerance = kClientEdgeTolerance) {
    if (tolerance < 0 || clip.right <= clip.left || clip.bottom <= clip.top ||
        client.right <= client.left || client.bottom <= client.top) {
        return false;
    }
    return std::labs(clip.left - client.left) <= tolerance &&
           std::labs(clip.top - client.top) <= tolerance &&
           std::labs(clip.right - client.right) <= tolerance &&
           std::labs(clip.bottom - client.bottom) <= tolerance;
}

}  // namespace cursor_clip
