#include "hud_scaling_math.h"

#include <cmath>

namespace hud_scaling {
namespace {
template <typename Anchor>
int32_t ScaleAnchored(int32_t coordinate, int32_t referenceExtent, int32_t outputExtent,
                      float scale, Anchor anchor, Anchor start, Anchor center) {
    float result = coordinate * scale;
    if (anchor == center) {
        result = outputExtent * 0.5f + (coordinate - referenceExtent * 0.5f) * scale;
    } else if (anchor != start) {
        result = outputExtent - (referenceExtent - coordinate) * scale;
    }
    return static_cast<int32_t>(std::lround(result));
}
}

int32_t ScalePosition(int32_t c, int32_t r, int32_t o, float s, HorizontalAnchor a) {
    return ScaleAnchored(c, r, o, s, a, HorizontalAnchor::Left, HorizontalAnchor::Center);
}
int32_t ScalePosition(int32_t c, int32_t r, int32_t o, float s, VerticalAnchor a) {
    return ScaleAnchored(c, r, o, s, a, VerticalAnchor::Top, VerticalAnchor::Center);
}
int32_t ScaleExtent(int32_t extent, float scale) {
    return static_cast<int32_t>(std::lround(extent * scale));
}
}  // namespace hud_scaling
