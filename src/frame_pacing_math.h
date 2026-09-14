#pragma once

#include <cstdint>

namespace frame_pacing {

constexpr int64_t kStallThresholdUs = 250000;

constexpr bool IsFrameStall(int64_t intervalUs) {
    return intervalUs >= kStallThresholdUs;
}

constexpr int64_t AverageOrZero(int64_t total, int64_t samples) {
    return samples > 0 ? total / samples : 0;
}

constexpr int64_t RateMilliHz(int64_t events, int64_t elapsedMs) {
    return elapsedMs > 0 ? events * 1000000 / elapsedMs : 0;
}

}  // namespace frame_pacing
